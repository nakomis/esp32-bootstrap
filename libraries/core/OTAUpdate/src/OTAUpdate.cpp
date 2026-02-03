#include "OTAUpdate.h"
#include "SDLogger.h"
#include <HTTPClient.h>
#include <SD_MMC.h>
#include <Preferences.h>
#include <Update.h>

static const char* TAG = "OTA";

#define FIRMWARE_FILE "/firmware_update.bin"
#define OTA_BUFFER_SIZE 512

// SD card pins for ESP32-S3 (SDMMC 1-bit mode)
#define SD_CLK_PIN 39
#define SD_CMD_PIN 38
#define SD_D0_PIN  40

OTAUpdate::OTAUpdate() {
    _updating = false;
    _progress = 0;
    _status = "Ready";
    _updateCallback = nullptr;
    _progressCallback = nullptr;
    _httpClient = new HTTPClient();
    _secureClient = new WiFiClientSecure();
    _secureClient->setInsecure();  // Skip certificate validation
    _totalSize = 0;
    _downloadedSize = 0;
}

OTAUpdate::~OTAUpdate() {
    if (_httpClient) {
        delete _httpClient;
        _httpClient = nullptr;
    }
    if (_secureClient) {
        delete _secureClient;
        _secureClient = nullptr;
    }
}

bool OTAUpdate::isUpdating() {
    return _updating;
}

void OTAUpdate::setUpdateCallback(void (*callback)(bool success, const char* error)) {
    _updateCallback = callback;
}

void OTAUpdate::setProgressCallback(void (*callback)(int progress, size_t downloaded, size_t total)) {
    _progressCallback = callback;
}

String OTAUpdate::getStatus() {
    return _status;
}

int OTAUpdate::getProgress() {
    return _progress;
}

void OTAUpdate::cancelUpdate() {
    if (_updating) {
        log_w("Cancelling OTA update");
        _httpClient->end();
        _updating = false;
        _status = "Cancelled";
        _progress = 0;

        if (_updateCallback) {
            _updateCallback(false, "Update cancelled by user");
        }
    }
}

bool OTAUpdate::downloadToSD(const char* firmwareURL) {
    if (_updating) {
        log_w("Update already in progress");
        return false;
    }

    log_i("Starting OTA download from %s", firmwareURL);

    // Disable SD file logging to free memory and prevent DMA conflicts during download
    // Must flush before unmounting to ensure the background writer task
    // has finished any in-progress file operations
    SDLogger::getInstance().setFileLoggingEnabled(false);
    SDLogger::getInstance().flush();

    // Remount SD card
    SD_MMC.end();
    delay(100);

    // Re-enable internal pull-ups on SD card pins (SD_MMC.end() may have reset them)
    pinMode(SD_CLK_PIN, INPUT_PULLUP);
    pinMode(SD_CMD_PIN, INPUT_PULLUP);
    pinMode(SD_D0_PIN, INPUT_PULLUP);
    delay(10);  // Let pull-ups stabilize

    SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN);
    if (!SD_MMC.begin("/sdcard", true)) {
        log_e("Failed to remount SD card");
        SDLogger::getInstance().setFileLoggingEnabled(true);
        delay(2000);
        ESP.restart();
        return false;
    }

    _updating = true;
    _progress = 0;
    _status = "Downloading...";

    // Configure HTTP client
    String url = String(firmwareURL);
    if (url.startsWith("https://")) {
        log_i("Using HTTPS");
        _httpClient->begin(*_secureClient, firmwareURL);
    } else {
        log_i("Using HTTP");
        WiFiClient client;
        _httpClient->begin(client, firmwareURL);
    }

    _httpClient->setTimeout(30000);

    int httpCode = _httpClient->GET();

    if (httpCode != HTTP_CODE_OK) {
        log_e("HTTP GET failed: %d", httpCode);
        _httpClient->end();
        SDLogger::getInstance().setFileLoggingEnabled(true);
        _updating = false;
        delay(2000);
        ESP.restart();
        return false;
    }

    size_t firmwareSize = _httpClient->getSize();
    if (firmwareSize == 0 || firmwareSize == (size_t)-1) {
        log_e("Invalid firmware size: %d", firmwareSize);
        _httpClient->end();
        SDLogger::getInstance().setFileLoggingEnabled(true);
        _updating = false;
        delay(2000);
        ESP.restart();
        return false;
    }

    _totalSize = firmwareSize;
    log_i("Firmware size: %d bytes", firmwareSize);

    File file = SD_MMC.open(FIRMWARE_FILE, FILE_WRITE);
    if (!file) {
        log_e("Failed to open SD card file for writing");
        _httpClient->end();
        SDLogger::getInstance().setFileLoggingEnabled(true);
        _updating = false;
        delay(2000);
        ESP.restart();
        return false;
    }

    WiFiClient* stream = _httpClient->getStreamPtr();
    uint8_t buffer[OTA_BUFFER_SIZE];
    size_t written = 0;
    size_t lastProgress = 0;

    while (_httpClient->connected() && written < firmwareSize) {
        size_t available = stream->available();
        if (available) {
            size_t bytesToRead = min(available, sizeof(buffer));
            size_t bytesRead = stream->readBytes(buffer, bytesToRead);

            if (bytesRead > 0) {
                size_t bytesWritten = file.write(buffer, bytesRead);
                if (bytesWritten != bytesRead) {
                    log_e("SD write failed");
                    file.close();
                    SD_MMC.remove(FIRMWARE_FILE);
                    _httpClient->end();
                    SDLogger::getInstance().setFileLoggingEnabled(true);
                    _updating = false;
                    delay(2000);
                    ESP.restart();
                    return false;
                }

                written += bytesRead;
                _downloadedSize = written;
                _progress = (written * 100) / firmwareSize;

                if (_progressCallback) {
                    _progressCallback(_progress, written, firmwareSize);
                }

                if (_progress >= lastProgress + 10) {
                    log_i("Download: %d%% (%d/%d bytes)", _progress, written, firmwareSize);
                    lastProgress = _progress;
                }
            }
        }
        delay(1);
    }

    file.close();
    _httpClient->end();

    if (written != firmwareSize) {
        log_e("Download incomplete: %d of %d bytes", written, firmwareSize);
        SD_MMC.remove(FIRMWARE_FILE);
        SDLogger::getInstance().setFileLoggingEnabled(true);
        _updating = false;
        delay(2000);
        ESP.restart();
        return false;
    }

    log_i("Download complete: %d bytes", written);

    // Set NVS flags for bootloader
    Preferences prefs;
    prefs.begin("ota", false);
    prefs.putBool("pending", true);
    prefs.putUInt("size", firmwareSize);
    prefs.end();
    log_i("NVS flags set for bootloader");

    _progress = 100;
    _status = "Download complete - rebooting...";

    if (_progressCallback) {
        _progressCallback(100, firmwareSize, firmwareSize);
    }

    if (_updateCallback) {
        _updateCallback(true, "Download complete, rebooting to flash");
    }

    log_i("Rebooting to bootloader...");
    Serial.flush();
    delay(2000);
    ESP.restart();

    return true;
}

bool OTAUpdate::downloadAndFlash(const char* firmwareURL) {
    if (_updating) {
        log_w("Update already in progress");
        return false;
    }

    log_i("Starting direct-to-flash OTA from %s", firmwareURL);

    _updating = true;
    _progress = 0;
    _status = "Connecting...";

    // Configure HTTP client
    String url = String(firmwareURL);
    if (url.startsWith("https://")) {
        log_i("Using HTTPS");
        _httpClient->begin(*_secureClient, firmwareURL);
    } else {
        log_i("Using HTTP");
        WiFiClient client;
        _httpClient->begin(client, firmwareURL);
    }

    _httpClient->setTimeout(30000);

    int httpCode = _httpClient->GET();

    if (httpCode != HTTP_CODE_OK) {
        log_e("HTTP GET failed: %d", httpCode);
        _httpClient->end();
        _updating = false;
        _status = "HTTP error";
        if (_updateCallback) {
            _updateCallback(false, "HTTP download failed");
        }
        return false;
    }

    size_t firmwareSize = _httpClient->getSize();
    if (firmwareSize == 0 || firmwareSize == (size_t)-1) {
        log_e("Invalid firmware size: %d", firmwareSize);
        _httpClient->end();
        _updating = false;
        _status = "Invalid size";
        if (_updateCallback) {
            _updateCallback(false, "Invalid firmware size");
        }
        return false;
    }

    _totalSize = firmwareSize;
    log_i("Firmware size: %d bytes", firmwareSize);

    _status = "Flashing...";

    // Begin OTA update
    if (!Update.begin(firmwareSize, U_FLASH)) {
        log_e("Not enough space for OTA: %s", Update.errorString());
        _httpClient->end();
        _updating = false;
        _status = "No space";
        if (_updateCallback) {
            _updateCallback(false, Update.errorString());
        }
        return false;
    }

    WiFiClient* stream = _httpClient->getStreamPtr();
    uint8_t buffer[OTA_BUFFER_SIZE];
    size_t written = 0;
    size_t lastProgress = 0;

    while (_httpClient->connected() && written < firmwareSize) {
        size_t available = stream->available();
        if (available) {
            size_t bytesToRead = min(available, sizeof(buffer));
            size_t bytesRead = stream->readBytes(buffer, bytesToRead);

            if (bytesRead > 0) {
                size_t bytesWritten = Update.write(buffer, bytesRead);
                if (bytesWritten != bytesRead) {
                    log_e("Flash write failed");
                    Update.abort();
                    _httpClient->end();
                    _updating = false;
                    _status = "Write failed";
                    if (_updateCallback) {
                        _updateCallback(false, "Flash write failed");
                    }
                    return false;
                }

                written += bytesRead;
                _downloadedSize = written;
                _progress = (written * 100) / firmwareSize;

                if (_progressCallback) {
                    _progressCallback(_progress, written, firmwareSize);
                }

                if (_progress >= lastProgress + 10) {
                    log_i("Flash: %d%% (%d/%d bytes)", _progress, written, firmwareSize);
                    lastProgress = _progress;
                }
            }
        }
        delay(1);
    }

    _httpClient->end();

    if (written != firmwareSize) {
        log_e("Download incomplete: %d of %d bytes", written, firmwareSize);
        Update.abort();
        _updating = false;
        _status = "Incomplete";
        if (_updateCallback) {
            _updateCallback(false, "Download incomplete");
        }
        return false;
    }

    // Finalize update
    if (!Update.end(true)) {
        log_e("Update finalization failed: %s", Update.errorString());
        _updating = false;
        _status = "Finalize failed";
        if (_updateCallback) {
            _updateCallback(false, Update.errorString());
        }
        return false;
    }

    log_i("OTA complete: %d bytes flashed", written);

    _progress = 100;
    _status = "Complete - rebooting...";

    if (_progressCallback) {
        _progressCallback(100, firmwareSize, firmwareSize);
    }

    if (_updateCallback) {
        _updateCallback(true, "Update complete, rebooting");
    }

    log_i("Rebooting...");
    Serial.flush();
    delay(2000);
    ESP.restart();

    return true;
}
