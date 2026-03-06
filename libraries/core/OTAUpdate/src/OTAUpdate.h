#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include "SDLogger.h"

class HTTPClient;

class OTAUpdate {
public:
    OTAUpdate();
    ~OTAUpdate();

    bool isUpdating();

    void setUpdateCallback(void (*callback)(bool success, const char* error));
    void setProgressCallback(void (*callback)(int progress, size_t downloaded, size_t total));

    void cancelUpdate();

    // Direct to flash OTA (no SD card required)
    bool downloadAndFlash(const char* firmwareURL);

    // Legacy SD card based OTA (requires SD card)
    bool downloadToSD(const char* firmwareURL);

    String getStatus();
    int getProgress();

private:
    bool _updating;
    int _progress;
    String _status;
    void (*_updateCallback)(bool success, const char* error);
    void (*_progressCallback)(int progress, size_t downloaded, size_t total);

    HTTPClient* _httpClient;
    WiFiClientSecure* _secureClient;
    size_t _totalSize;
    size_t _downloadedSize;
};
