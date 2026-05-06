#ifndef CATCAM_OTAUPDATE_H
#define CATCAM_OTAUPDATE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include "../../SDLogger/src/SDLogger.h"

// Forward declaration
class HTTPClient;

/**
 * OTAUpdate - Handles firmware updates via SD card download
 *
 * The update process is two-stage:
 * 1. downloadToSD() downloads firmware to SD card and sets NVS flags
 * 2. Device reboots, bootloader reads NVS flags and flashes from SD card
 */
class OTAUpdate {
public:
    OTAUpdate();
    ~OTAUpdate();

    /**
     * Check if an update is currently in progress
     */
    bool isUpdating();

    /**
     * Set callback for update completion
     */
    void setUpdateCallback(void (*callback)(bool success, const char* error));

    /**
     * Set callback for download progress
     */
    void setProgressCallback(void (*callback)(int progress, size_t downloaded, size_t total));

    /**
     * Cancel an in-progress update
     */
    void cancelUpdate();

    /**
     * Download firmware to SD card for bootloader to flash
     * Sets NVS flags and reboots when complete
     * @param firmwareURL URL to download firmware from (HTTP or HTTPS)
     * @return true if download started successfully (will reboot on completion)
     */
    bool downloadToSD(const char* firmwareURL);

    /**
     * Get current status message
     */
    String getStatus();

    /**
     * Get current progress percentage (0-100)
     */
    int getProgress();

private:
    bool _initialized;
    bool _updating;
    String _hostname;
    int _progress;
    String _status;
    void (*_updateCallback)(bool success, const char* error);
    void (*_progressCallback)(int progress, size_t downloaded, size_t total);

    // HTTP client members
    HTTPClient* _httpClient;
    WiFiClientSecure* _secureClient;
    size_t _totalSize;
    size_t _downloadedSize;
};

#endif
