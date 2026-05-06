#include "OTAUpdate.h"
#include <HTTPClient.h>
#include <SD_MMC.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

#define FIRMWARE_FILE "/firmware_update.bin"
#define OTA_BUFFER_SIZE 512

OTAUpdate::OTAUpdate() {
    _updating = false;
    _progress = 0;
    _status = "Not initialized";
    _updateCallback = nullptr;
    _progressCallback = nullptr;
    _httpClient = nullptr;  // Created on demand to avoid memory fragmentation
    _secureClient = nullptr;
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
        _httpClient->end();
        _updating = false;
        _status = "Update cancelled";
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

    SDLogger::getInstance().infof("Starting OTA: downloading to SD card from %s", firmwareURL);

    // Disable SD file logging to free memory during download
    // Must flush before unmounting to ensure the background writer task
    // has finished any in-progress file operations
    SDLogger::getInstance().setFileLoggingEnabled(false);
    SDLogger::getInstance().flush();

    // Unmount and remount SD_MMC to clear any state
    // This prevents timeout errors during write operations
    SD_MMC.end();
    delay(100);

#ifdef ESP32S3_CAM
    SD_MMC.setPins(39, 38, 40);  // CLK, CMD, D0 for ESP32-S3 CAM
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
    _status = "Downloading to SD card...";

    // Clean up any existing clients to free memory
    if (_httpClient) {
        delete _httpClient;
        _httpClient = nullptr;
    }
    if (_secureClient) {
        delete _secureClient;
        _secureClient = nullptr;
    }

    SDLogger::getInstance().infof("Free heap before SSL client creation: %d bytes", ESP.getFreeHeap());

    // Configure HTTP client - create fresh instances to avoid fragmentation
    String url = String(firmwareURL);
    if (url.startsWith("https://")) {
        SDLogger::getInstance().infof("Using HTTPS secure connection");

        // Create fresh SSL client
        _secureClient = new WiFiClientSecure();
        _secureClient->setInsecure(); // Skip certificate validation

        // Log memory state for debugging - check INTERNAL RAM specifically
        // (MALLOC_CAP_8BIT alone includes PSRAM which gives misleading results)
        SDLogger::getInstance().infof("Largest INTERNAL free block: %d bytes",
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        SDLogger::getInstance().infof("Free internal heap: %d bytes",
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        SDLogger::getInstance().infof("Free PSRAM: %d bytes",
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

        _httpClient = new HTTPClient();
        _httpClient->begin(*_secureClient, firmwareURL);

        SDLogger::getInstance().infof("Free heap after SSL client creation: %d bytes", ESP.getFreeHeap());
    } else {
        SDLogger::getInstance().infof("Using HTTP connection");
        _httpClient = new HTTPClient();
        WiFiClient client;
        _httpClient->begin(client, firmwareURL);
    }

    _httpClient->setTimeout(30000);  // 30 second timeout

    // Make HTTP GET request
    int httpCode = _httpClient->GET();

    if (httpCode != HTTP_CODE_OK) {
        SDLogger::getInstance().errorf("HTTP GET failed: %d", httpCode);
        _httpClient->end();
        SDLogger::getInstance().setFileLoggingEnabled(true);
        _updating = false;
        delay(2000);
        ESP.restart();
        return false;
    }

    // Get firmware size
    size_t firmwareSize = _httpClient->getSize();
    if (firmwareSize == 0 || firmwareSize == (size_t)-1) {
        SDLogger::getInstance().errorf("Invalid firmware size: %d", firmwareSize);
        _httpClient->end();
        SDLogger::getInstance().setFileLoggingEnabled(true);
        _updating = false;
        delay(2000);
        ESP.restart();
        return false;
    }

    _totalSize = firmwareSize;
    SDLogger::getInstance().infof("Firmware size: %d bytes", firmwareSize);

    // Open SD card file for writing
    File file = SD_MMC.open(FIRMWARE_FILE, FILE_WRITE);
    if (!file) {
        SDLogger::getInstance().errorf("Failed to open SD card file for writing");
        _httpClient->end();
        SDLogger::getInstance().setFileLoggingEnabled(true);
        _updating = false;
        delay(2000);
        ESP.restart();
        return false;
    }

    // Download firmware to SD card in chunks
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
                    SDLogger::getInstance().errorf("SD write failed: wrote %d of %d bytes", bytesWritten, bytesRead);
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

                // Call progress callback for BLE notifications
                if (_progressCallback) {
                    _progressCallback(_progress, written, firmwareSize);
                }

                // Log progress every 10%
                if (_progress >= lastProgress + 10) {
                    SDLogger::getInstance().infof("Download progress: %d%% (%d/%d bytes)", _progress, written, firmwareSize);
                    lastProgress = _progress;
                }
            }
        }
        delay(1);  // Yield to watchdog
    }

    file.close();
    _httpClient->end();

    if (written != firmwareSize) {
        SDLogger::getInstance().errorf("Download incomplete: %d of %d bytes", written, firmwareSize);
        SD_MMC.remove(FIRMWARE_FILE);
        SDLogger::getInstance().setFileLoggingEnabled(true);
        _updating = false;
        delay(2000);
        ESP.restart();
        return false;
    }

    SDLogger::getInstance().infof("Download complete: %d bytes written to SD card", written);

    // Set NVS flags for bootloader to pick up
    Preferences prefs;
    prefs.begin("ota", false);  // Read-write
    prefs.putBool("pending", true);
    prefs.putUInt("size", firmwareSize);
    prefs.end();
    SDLogger::getInstance().infof("NVS flags set for bootloader");

    // Send final progress update before reboot
    _progress = 100;
    _status = "Download complete - rebooting...";
    if (_progressCallback) {
        _progressCallback(100, firmwareSize, firmwareSize);
    }

    if (_updateCallback) {
        _updateCallback(true, "Download complete, rebooting to flash");
    }

    SDLogger::getInstance().infof("Rebooting to bootloader for flash...");
    Serial.flush();

    delay(2000);
    ESP.restart();

    return true;
}
