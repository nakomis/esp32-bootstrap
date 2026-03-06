#include "OTAUpdate.h"
#include "SDLogger.h"
#include <HTTPClient.h>
#include <SD_MMC.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_heap_caps.h>

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
    _httpClient = nullptr;   // Created on demand to avoid memory fragmentation
    _secureClient = nullptr; // Created on demand
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
        SDLogger::getInstance().warnf("Cancelling OTA update");
        if (_httpClient) {
            _httpClient->end();
            delete _httpClient;
            _httpClient = nullptr;
        }
        if (_secureClient) {
            delete _secureClient;
            _secureClient = nullptr;
        }
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
        SDLogger::getInstance().warnf("Update already in progress");
        return false;
    }

    // Clean up any existing clients to avoid fragmentation
    if (_httpClient) { delete _httpClient; _httpClient = nullptr; }
    if (_secureClient) { delete _secureClient; _secureClient = nullptr; }

    SDLogger::getInstance().infof("Starting OTA download from %s", firmwareURL);

    // Disable SD file logging to free memory and prevent DMA conflicts during download
    // Must flush before unmounting to ensure the background writer task
    // has finished any in-progress file operations
    SDLogger::getInstance().setFileLoggingEnabled(false);
    SDLogger::getInstance().flush();

    // Remount SD card
    SD_MMC.end();
    delay(100);

#ifdef ESP32S3_CAM
    // Re-enable internal pull-ups (SD_MMC.end() may reset GPIO state)
    pinMode(SD_CLK_PIN, INPUT_PULLUP);
    pinMode(SD_CMD_PIN, INPUT_PULLUP);
    pinMode(SD_D0_PIN, INPUT_PULLUP);
    delay(10);  // Let pull-ups stabilise
    SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN);
    if (!SD_MMC.begin("/sdcard", true)) {  // 1-bit mode for ESP32-S3
#else
    if (!SD_MMC.begin()) {
#endif
        SDLogger::getInstance().errorf("Failed to remount SD_MMC for OTA download");
        delay(2000);
        ESP.restart();
        return false;
    }

    _updating = true;
    _progress = 0;
    _status = "Downloading...";

    // Configure HTTP client - create fresh instances to avoid fragmentation
    String url = String(firmwareURL);
    if (url.startsWith("https://")) {
        SDLogger::getInstance().infof("Largest INTERNAL free block: %d bytes",
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        SDLogger::getInstance().infof("Free internal heap: %d bytes",
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        SDLogger::getInstance().infof("Free PSRAM: %d bytes",
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        _secureClient = new WiFiClientSecure();
        _secureClient->setInsecure();  // Skip certificate validation for S3 presigned URLs
        _httpClient = new HTTPClient();
        _httpClient->begin(*_secureClient, firmwareURL);
    } else {
        _httpClient = new HTTPClient();
        WiFiClient client;
        _httpClient->begin(client, firmwareURL);
    }

    _httpClient->setTimeout(30000);

    int httpCode = _httpClient->GET();

    if (httpCode != HTTP_CODE_OK) {
        SDLogger::getInstance().errorf("HTTP GET failed: %d", httpCode);
        _httpClient->end();
        delete _httpClient; _httpClient = nullptr;
        if (_secureClient) { delete _secureClient; _secureClient = nullptr; }
        SDLogger::getInstance().setFileLoggingEnabled(true);
        _updating = false;
        delay(2000);
        ESP.restart();
        return false;
    }

    size_t firmwareSize = _httpClient->getSize();
    if (firmwareSize == 0 || firmwareSize == (size_t)-1) {
        SDLogger::getInstance().errorf("Invalid firmware size: %u", firmwareSize);
        _httpClient->end();
        delete _httpClient; _httpClient = nullptr;
        if (_secureClient) { delete _secureClient; _secureClient = nullptr; }
        SDLogger::getInstance().setFileLoggingEnabled(true);
        _updating = false;
        delay(2000);
        ESP.restart();
        return false;
    }

    _totalSize = firmwareSize;
    SDLogger::getInstance().infof("Firmware size: %u bytes", firmwareSize);

    File file = SD_MMC.open(FIRMWARE_FILE, FILE_WRITE);
    if (!file) {
        SDLogger::getInstance().errorf("Failed to open SD card file for writing");
        _httpClient->end();
        delete _httpClient; _httpClient = nullptr;
        if (_secureClient) { delete _secureClient; _secureClient = nullptr; }
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
                    SDLogger::getInstance().errorf("SD write failed");
                    file.close();
                    SD_MMC.remove(FIRMWARE_FILE);
                    _httpClient->end();
                    delete _httpClient; _httpClient = nullptr;
                    if (_secureClient) { delete _secureClient; _secureClient = nullptr; }
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
                    SDLogger::getInstance().infof("Download: %d%% (%u/%u bytes)", _progress, written, firmwareSize);
                    lastProgress = _progress;
                }
            }
        }
        delay(1);
    }

    file.close();
    _httpClient->end();
    delete _httpClient; _httpClient = nullptr;
    if (_secureClient) { delete _secureClient; _secureClient = nullptr; }

    if (written != firmwareSize) {
        SDLogger::getInstance().errorf("Download incomplete: %u of %u bytes", written, firmwareSize);
        SD_MMC.remove(FIRMWARE_FILE);
        SDLogger::getInstance().setFileLoggingEnabled(true);
        _updating = false;
        delay(2000);
        ESP.restart();
        return false;
    }

    SDLogger::getInstance().infof("Download complete: %u bytes", written);

    // Set NVS flags for bootloader
    Preferences prefs;
    prefs.begin("ota", false);
    prefs.putBool("pending", true);
    prefs.putUInt("size", firmwareSize);
    prefs.end();
    SDLogger::getInstance().infof("NVS flags set for bootloader");

    _progress = 100;
    _status = "Download complete - rebooting...";

    if (_progressCallback) {
        _progressCallback(100, firmwareSize, firmwareSize);
    }

    if (_updateCallback) {
        _updateCallback(true, "Download complete, rebooting to flash");
    }

    SDLogger::getInstance().infof("Rebooting to bootloader...");
    Serial.flush();
    delay(2000);
    ESP.restart();

    return true;
}

bool OTAUpdate::downloadAndFlash(const char* firmwareURL) {
    if (_updating) {
        SDLogger::getInstance().warnf("Update already in progress");
        return false;
    }

    // Clean up any existing clients to avoid fragmentation
    if (_httpClient) { delete _httpClient; _httpClient = nullptr; }
    if (_secureClient) { delete _secureClient; _secureClient = nullptr; }

    SDLogger::getInstance().infof("Starting direct-to-flash OTA from %s", firmwareURL);

    _updating = true;
    _progress = 0;
    _status = "Connecting...";

    // Configure HTTP client - create fresh instances to avoid fragmentation
    String url = String(firmwareURL);
    if (url.startsWith("https://")) {
        _secureClient = new WiFiClientSecure();
        _secureClient->setInsecure();  // Skip certificate validation for S3 presigned URLs
        _httpClient = new HTTPClient();
        _httpClient->begin(*_secureClient, firmwareURL);
    } else {
        _httpClient = new HTTPClient();
        WiFiClient client;
        _httpClient->begin(client, firmwareURL);
    }

    _httpClient->setTimeout(30000);

    int httpCode = _httpClient->GET();

    if (httpCode != HTTP_CODE_OK) {
        SDLogger::getInstance().errorf("HTTP GET failed: %d", httpCode);
        _httpClient->end();
        delete _httpClient; _httpClient = nullptr;
        if (_secureClient) { delete _secureClient; _secureClient = nullptr; }
        _updating = false;
        _status = "HTTP error";
        if (_updateCallback) {
            _updateCallback(false, "HTTP download failed");
        }
        return false;
    }

    size_t firmwareSize = _httpClient->getSize();
    if (firmwareSize == 0 || firmwareSize == (size_t)-1) {
        SDLogger::getInstance().errorf("Invalid firmware size: %u", firmwareSize);
        _httpClient->end();
        delete _httpClient; _httpClient = nullptr;
        if (_secureClient) { delete _secureClient; _secureClient = nullptr; }
        _updating = false;
        _status = "Invalid size";
        if (_updateCallback) {
            _updateCallback(false, "Invalid firmware size");
        }
        return false;
    }

    _totalSize = firmwareSize;
    SDLogger::getInstance().infof("Firmware size: %u bytes", firmwareSize);

    _status = "Flashing...";

    // Begin OTA update
    if (!Update.begin(firmwareSize, U_FLASH)) {
        SDLogger::getInstance().errorf("Not enough space for OTA: %s", Update.errorString());
        _httpClient->end();
        delete _httpClient; _httpClient = nullptr;
        if (_secureClient) { delete _secureClient; _secureClient = nullptr; }
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
                    SDLogger::getInstance().errorf("Flash write failed");
                    Update.abort();
                    _httpClient->end();
                    delete _httpClient; _httpClient = nullptr;
                    if (_secureClient) { delete _secureClient; _secureClient = nullptr; }
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
                    SDLogger::getInstance().infof("Flash: %d%% (%u/%u bytes)", _progress, written, firmwareSize);
                    lastProgress = _progress;
                }
            }
        }
        delay(1);
    }

    _httpClient->end();
    delete _httpClient; _httpClient = nullptr;
    if (_secureClient) { delete _secureClient; _secureClient = nullptr; }

    if (written != firmwareSize) {
        SDLogger::getInstance().errorf("Download incomplete: %u of %u bytes", written, firmwareSize);
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
        SDLogger::getInstance().errorf("Update finalization failed: %s", Update.errorString());
        _updating = false;
        _status = "Finalize failed";
        if (_updateCallback) {
            _updateCallback(false, Update.errorString());
        }
        return false;
    }

    SDLogger::getInstance().infof("OTA complete: %u bytes flashed", written);

    _progress = 100;
    _status = "Complete - rebooting...";

    if (_progressCallback) {
        _progressCallback(100, firmwareSize, firmwareSize);
    }

    if (_updateCallback) {
        _updateCallback(true, "Update complete, rebooting");
    }

    SDLogger::getInstance().infof("Rebooting...");
    Serial.flush();
    delay(2000);
    ESP.restart();

    return true;
}
