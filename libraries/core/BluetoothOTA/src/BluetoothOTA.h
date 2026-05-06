#ifndef BLUETOOTH_OTA_H
#define BLUETOOTH_OTA_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "../../SDLogger/src/SDLogger.h"
#include "../../OTAUpdate/src/OTAUpdate.h"
#include "../../MqttService/src/MqttService.h"

// Nakomis ESP32 OTA Service UUIDs (shared across all Nakomis ESP32 projects)
#define NAKOMIS_ESP32_SERVICE_UUID      "99db6ea6-27e4-434d-aafd-795cf95feb06"
#define NAKOMIS_ESP32_COMMAND_CHAR_UUID "1ac886a6-5fff-41ea-9b11-25a7dcb93a7e"
#define NAKOMIS_ESP32_STATUS_CHAR_UUID  "5f5979f3-f1a6-4ce7-8360-e249c2e9333d"

// Legacy aliases for backwards compatibility
#define OTA_SERVICE_UUID        NAKOMIS_ESP32_SERVICE_UUID
#define OTA_COMMAND_CHAR_UUID   NAKOMIS_ESP32_COMMAND_CHAR_UUID
#define OTA_STATUS_CHAR_UUID    NAKOMIS_ESP32_STATUS_CHAR_UUID

struct OTACommand {
    String action;
    String firmware_url;
    String version;
};

struct OTAStatus {
    String status;
    String message;
    int progress;
    String version;
};

class BluetoothOTACallbacks : public BLECharacteristicCallbacks {
public:
    BluetoothOTACallbacks(class BluetoothOTA* parent) : _parent(parent) {}
    void onWrite(BLECharacteristic* pCharacteristic) override;
    
private:
    class BluetoothOTA* _parent;
};

class BluetoothOTA {
    friend class BluetoothOTACallbacks;  // Allow callbacks to access private members

public:
    BluetoothOTA();
    ~BluetoothOTA();

    bool init(const char* deviceName = "BootBoots-CatCam");
    bool initWithExistingServer(BLEServer* pServer);  // NEW: Use existing server
    void handle();
    bool isConnected();
    void sendStatusUpdate(const String& status, const String& message, int progress = 0);
    void handleOTACommand(const String& commandJson);

    // OTA Update integration
    void setOTAUpdate(OTAUpdate* otaUpdate) { _otaUpdate = otaUpdate; }

    // MQTT service - paused before OTA to free SSL memory
    void setMqttService(MqttService* mqttService) { _mqttService = mqttService; }

private:
    BLEServer* _pServer;
    BLEService* _pService;
    BLECharacteristic* _pCommandCharacteristic;
    BLECharacteristic* _pStatusCharacteristic;
    BluetoothOTACallbacks* _pCallbacks;
    OTAUpdate* _otaUpdate;
    MqttService* _mqttService;
    
    bool _initialized;
    bool _deviceConnected;
    bool _wasConnected;  // Track previous connection state for advertising restart
    bool _pendingConnectNotify;  // Deferred connect notification (avoid stack overflow in callback)
    volatile bool _hasPendingCommand;  // Flag for deferred command processing
    // Raw buffer for incoming BLE data (avoid String operations in callback)
    static const int MAX_PENDING_SIZE = 4096;
    char _pendingBuffer[MAX_PENDING_SIZE];
    volatile int _pendingLength;
    String _deviceName;

    // URL chunking support (for long S3 signed URLs)
    String _urlChunks[10];  // Store up to 10 chunks
    int _totalChunks;
    int _receivedChunks;
    String _chunkVersion;
    
    // Helper methods
    OTACommand parseCommand(const String& json);
    String createStatusJson(const String& status, const String& message, int progress = 0);
    void processOTAUpdate(const OTACommand& command);
    
    // Server callbacks
    class ServerCallbacks : public BLEServerCallbacks {
    public:
        ServerCallbacks(BluetoothOTA* parent) : _parent(parent) {}
        void onConnect(BLEServer* pServer) override;
        void onDisconnect(BLEServer* pServer) override;
        
    private:
        BluetoothOTA* _parent;
    };
    
    ServerCallbacks* _pServerCallbacks;
};

#endif // BLUETOOTH_OTA_H
