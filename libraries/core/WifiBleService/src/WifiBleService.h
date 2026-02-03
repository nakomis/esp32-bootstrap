#pragma once

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>

// Forward declarations
class WifiConnect;

// WiFi BLE Service UUIDs (separate service for WiFi configuration)
#define WIFI_BLE_SERVICE_UUID       "a1b2c3d4-e5f6-4789-abcd-ef0123456789"
#define WIFI_COMMAND_CHAR_UUID      "a1b2c3d5-e5f6-4789-abcd-ef0123456789"
#define WIFI_STATUS_CHAR_UUID       "a1b2c3d6-e5f6-4789-abcd-ef0123456789"

class WifiBleService : public BLECharacteristicCallbacks {
public:
    WifiBleService();

    // Initialize the WiFi service on an existing BLE server
    void init(BLEServer* pServer);
    void handle();

    void setWifi(WifiConnect* wifi) { _wifi = wifi; }

    // BLECharacteristicCallbacks
    void onWrite(BLECharacteristic* pCharacteristic) override;

    // Send status updates
    void sendStatus(const String& status, const String& message);
    void sendError(const String& message);
    void sendSuccess(const String& message);

private:
    BLEService* _pService;
    BLECharacteristic* _pCommandChar;
    BLECharacteristic* _pStatusChar;

    WifiConnect* _wifi;

    volatile bool _hasPendingCommand;
    static const int MAX_PENDING_SIZE = 512;
    char _pendingBuffer[MAX_PENDING_SIZE];
    volatile int _pendingLength;

    void processCommand(const String& json);
    void handleWifiConfig(const String& ssid, const String& password);
    void handleWifiConnect();
    void handleWifiStatus();
    void handleWifiDisconnect();
    void handleWifiClear();
};
