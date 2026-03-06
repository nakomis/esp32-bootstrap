#pragma once

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>

// Forward declarations
class OTAUpdate;
class WifiConnect;

// Nakomis ESP32 OTA Service UUIDs (for sandbox app FirmwareManager compatibility)
#define NAKOMIS_ESP32_SERVICE_UUID      "99db6ea6-27e4-434d-aafd-795cf95feb06"
#define NAKOMIS_ESP32_COMMAND_CHAR_UUID "1ac886a6-5fff-41ea-9b11-25a7dcb93a7e"
#define NAKOMIS_ESP32_STATUS_CHAR_UUID  "5f5979f3-f1a6-4ce7-8360-e249c2e9333d"

// Legacy aliases for backwards compatibility
#define OTA_SERVICE_UUID        NAKOMIS_ESP32_SERVICE_UUID
#define OTA_COMMAND_CHAR_UUID   NAKOMIS_ESP32_COMMAND_CHAR_UUID
#define OTA_STATUS_CHAR_UUID    NAKOMIS_ESP32_STATUS_CHAR_UUID

class OTABleService : public BLECharacteristicCallbacks {
public:
    OTABleService();

    // Initialize the OTA service on an existing BLE server
    void init(BLEServer* pServer);
    void handle();

    void setOTAUpdate(OTAUpdate* ota) { _otaUpdate = ota; }
    void setWifi(WifiConnect* wifi) { _wifi = wifi; }
    void setVersionString(const char* version) { _versionString = version; }

    // BLECharacteristicCallbacks
    void onWrite(BLECharacteristic* pCharacteristic) override;

    // Send progress/status updates
    void sendProgress(int progress, const String& message);
    void sendError(const String& message);
    void sendSuccess(const String& message);

private:
    BLEService* _pService;
    BLECharacteristic* _pCommandChar;
    BLECharacteristic* _pStatusChar;

    OTAUpdate* _otaUpdate;
    WifiConnect* _wifi;
    String _versionString;

    volatile bool _hasPendingCommand;
    static const int MAX_PENDING_SIZE = 4096;
    char _pendingBuffer[MAX_PENDING_SIZE];
    volatile int _pendingLength;

    // URL chunking support (for long presigned URLs)
    static const int MAX_URL_SIZE = 2048;
    char _urlBuffer[MAX_URL_SIZE];
    int _urlLength;
    int _expectedChunks;
    int _receivedChunks;
    String _pendingVersion;

    void processCommand(const String& json);
    void handleOTAUpdate(const String& url, const String& version);
    void handleUrlChunk(int chunkIndex, int totalChunks, const String& chunkData, const String& version);
    void handleUrlChunkDirect(int chunkIndex, int totalChunks, const String& chunkData, const String& version);
    void handleGetStatus();
    void sendStatus(const String& status, const String& message, int progress = -1);

    // Flag to trigger OTA start from main loop (after all chunks received)
    volatile bool _startOtaPending;
    String _pendingOtaUrl;
};
