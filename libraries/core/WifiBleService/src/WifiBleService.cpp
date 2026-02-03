#include "WifiBleService.h"
#include "WifiConnect.h"
#include <WiFi.h>

static const char* TAG = "WIFI_BLE";

WifiBleService::WifiBleService()
    : _pService(nullptr)
    , _pCommandChar(nullptr)
    , _pStatusChar(nullptr)
    , _wifi(nullptr)
    , _hasPendingCommand(false)
    , _pendingLength(0)
{
    memset(_pendingBuffer, 0, MAX_PENDING_SIZE);
}

void WifiBleService::init(BLEServer* pServer) {
    if (!pServer) {
        log_e("Cannot init WiFi BLE service: no BLE server");
        return;
    }

    log_i("Initializing WiFi BLE service");

    _pService = pServer->createService(WIFI_BLE_SERVICE_UUID);

    // Command characteristic (write)
    _pCommandChar = _pService->createCharacteristic(
        WIFI_COMMAND_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    _pCommandChar->setCallbacks(this);

    // Status characteristic (read/notify) - for status updates
    _pStatusChar = _pService->createCharacteristic(
        WIFI_STATUS_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    _pStatusChar->addDescriptor(new BLE2902());

    // Set initial status
    DynamicJsonDocument doc(256);
    doc["status"] = "success";
    doc["message"] = "Ready";
    doc["connected"] = false;
    String json;
    serializeJson(doc, json);
    _pStatusChar->setValue(json.c_str());

    _pService->start();

    // Add service UUID to advertising
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(WIFI_BLE_SERVICE_UUID);

    log_i("WiFi BLE service started (UUID: %s)", WIFI_BLE_SERVICE_UUID);
}

void WifiBleService::handle() {
    // Handle deferred command processing
    if (_hasPendingCommand && _pendingLength > 0) {
        _hasPendingCommand = false;

        String commandCopy = String(_pendingBuffer);
        _pendingLength = 0;
        _pendingBuffer[0] = '\0';

        processCommand(commandCopy);
    }
}

void WifiBleService::onWrite(BLECharacteristic* pCharacteristic) {
    uint8_t* data = pCharacteristic->getData();
    size_t len = pCharacteristic->getLength();

    if (!data || len == 0 || len >= MAX_PENDING_SIZE - 1) {
        return;
    }

    // Copy to pending buffer and defer to handle() in main loop
    memcpy(_pendingBuffer, data, len);
    _pendingBuffer[len] = '\0';
    _pendingLength = len;
    _hasPendingCommand = true;
}

void WifiBleService::processCommand(const String& json) {
    log_i("WiFi command received: %s", json.c_str());

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        sendError("Invalid JSON");
        return;
    }

    String command = doc["command"] | "";

    if (command == "wifi_config") {
        String ssid = doc["ssid"] | "";
        String password = doc["password"] | "";
        handleWifiConfig(ssid, password);
    }
    else if (command == "wifi_connect") {
        handleWifiConnect();
    }
    else if (command == "wifi_status") {
        handleWifiStatus();
    }
    else if (command == "wifi_disconnect") {
        handleWifiDisconnect();
    }
    else if (command == "wifi_clear") {
        handleWifiClear();
    }
    else {
        sendError("Unknown command: " + command);
    }
}

void WifiBleService::handleWifiConfig(const String& ssid, const String& password) {
    if (!_wifi) {
        sendError("WiFi not available");
        return;
    }

    if (ssid.isEmpty()) {
        sendError("SSID required");
        return;
    }

    if (password.isEmpty()) {
        sendError("Password required");
        return;
    }

    if (_wifi->setCredentials(ssid.c_str(), password.c_str())) {
        log_i("WiFi credentials saved for '%s'", ssid.c_str());
        sendSuccess("Credentials saved for " + ssid);
    } else {
        sendError("Failed to save credentials");
    }
}

void WifiBleService::handleWifiConnect() {
    if (!_wifi) {
        sendError("WiFi not available");
        return;
    }

    if (!_wifi->hasCredentials()) {
        sendError("No credentials configured");
        return;
    }

    if (_wifi->isConnected()) {
        sendSuccess("Already connected");
        return;
    }

    log_i("Connecting to WiFi...");
    sendStatus("progress", "Connecting...");

    int result = _wifi->connect();
    if (result == 0 && _wifi->isConnected()) {
        sendSuccess("Connected to " + _wifi->getSSID());
    } else {
        sendError("Connection failed");
    }
}

void WifiBleService::handleWifiStatus() {
    if (!_wifi) {
        sendError("WiFi not available");
        return;
    }

    if (!_pStatusChar) return;

    DynamicJsonDocument doc(256);
    doc["status"] = "success";
    doc["connected"] = _wifi->isConnected();
    doc["ssid"] = _wifi->getSSID();
    doc["has_credentials"] = _wifi->hasCredentials();

    if (_wifi->isConnected()) {
        doc["ip"] = WiFi.localIP().toString();
        doc["rssi"] = WiFi.RSSI();
    }

    String json;
    serializeJson(doc, json);
    _pStatusChar->setValue(json.c_str());
    _pStatusChar->notify();

    log_i("WiFi status sent: %s", json.c_str());
}

void WifiBleService::handleWifiDisconnect() {
    if (!_wifi) {
        sendError("WiFi not available");
        return;
    }

    _wifi->disconnect();
    sendSuccess("Disconnected");
}

void WifiBleService::handleWifiClear() {
    if (!_wifi) {
        sendError("WiFi not available");
        return;
    }

    _wifi->clearCredentials();
    sendSuccess("Credentials cleared");
}

void WifiBleService::sendStatus(const String& status, const String& message) {
    if (!_pStatusChar) return;

    DynamicJsonDocument doc(256);
    doc["status"] = status;
    doc["message"] = message;
    if (_wifi) {
        doc["connected"] = _wifi->isConnected();
    }

    String json;
    serializeJson(doc, json);
    _pStatusChar->setValue(json.c_str());
    _pStatusChar->notify();

    log_i("WiFi BLE status: %s", json.c_str());
}

void WifiBleService::sendError(const String& message) {
    sendStatus("error", message);
}

void WifiBleService::sendSuccess(const String& message) {
    sendStatus("success", message);
}
