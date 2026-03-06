#include "OTABleService.h"
#include "OTAUpdate.h"
#include "WifiConnect.h"
#include "SDLogger.h"

static const char* TAG = "OTA_BLE";

OTABleService::OTABleService()
    : _pService(nullptr)
    , _pCommandChar(nullptr)
    , _pStatusChar(nullptr)
    , _otaUpdate(nullptr)
    , _wifi(nullptr)
    , _versionString("0.0.0")
    , _hasPendingCommand(false)
    , _pendingLength(0)
    , _urlLength(0)
    , _expectedChunks(0)
    , _receivedChunks(0)
    , _startOtaPending(false)
{
    memset(_pendingBuffer, 0, MAX_PENDING_SIZE);
    memset(_urlBuffer, 0, MAX_URL_SIZE);
}

void OTABleService::init(BLEServer* pServer) {
    if (!pServer) {
        SDLogger::getInstance().error("Cannot init OTA service: no BLE server");
        return;
    }

    SDLogger::getInstance().info("Initializing OTA BLE service");

    _pService = pServer->createService(OTA_SERVICE_UUID);

    // Command characteristic (write)
    _pCommandChar = _pService->createCharacteristic(
        OTA_COMMAND_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    _pCommandChar->setCallbacks(this);

    // Status characteristic (read/notify) - for progress updates
    _pStatusChar = _pService->createCharacteristic(
        OTA_STATUS_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    _pStatusChar->addDescriptor(new BLE2902());

    // Set initial status with version
    DynamicJsonDocument doc(256);
    doc["status"] = "success";
    doc["message"] = "Ready";
    doc["version"] = _versionString;
    String json;
    serializeJson(doc, json);
    _pStatusChar->setValue(json.c_str());

    _pService->start();

    // Add OTA service UUID to advertising so Web Bluetooth can discover by service
    // Must stop/restart advertising since it's already running from KappaWarmerBLE
    // Use scan response for the second UUID since advertising packet is limited to 31 bytes
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->stop();
    pAdvertising->addServiceUUID(NAKOMIS_ESP32_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // For iOS compatibility
    pAdvertising->start();

    SDLogger::getInstance().infof("OTA BLE service started (UUID: %s)", NAKOMIS_ESP32_SERVICE_UUID);
}

void OTABleService::handle() {
    // Handle deferred OTA start (triggered after all URL chunks received)
    if (_startOtaPending) {
        _startOtaPending = false;
        String url = _pendingOtaUrl;
        String version = _pendingVersion;
        _pendingOtaUrl = "";
        handleOTAUpdate(url, version);
    }

    // Handle deferred command processing
    if (_hasPendingCommand && _pendingLength > 0) {
        _hasPendingCommand = false;

        String commandCopy = String(_pendingBuffer);
        _pendingLength = 0;
        _pendingBuffer[0] = '\0';

        processCommand(commandCopy);
    }
}

void OTABleService::onWrite(BLECharacteristic* pCharacteristic) {
    uint8_t* data = pCharacteristic->getData();
    size_t len = pCharacteristic->getLength();

    if (!data || len == 0 || len >= MAX_PENDING_SIZE - 1) {
        return;
    }

    // Copy to temp buffer for parsing
    char tempBuffer[MAX_PENDING_SIZE];
    memcpy(tempBuffer, data, len);
    tempBuffer[len] = '\0';

    // Quick check if this is a url_chunk command - process immediately to avoid race conditions
    // url_chunk commands arrive rapidly and would overwrite each other if deferred
    if (strstr(tempBuffer, "\"action\":\"url_chunk\"") != nullptr ||
        strstr(tempBuffer, "\"action\": \"url_chunk\"") != nullptr) {
        // Process URL chunks immediately in callback context
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, tempBuffer);
        if (!error) {
            int chunkIndex = doc["chunk_index"] | -1;
            int totalChunks = doc["total_chunks"] | 0;
            String chunkData = doc["chunk_data"] | "";
            String version = doc["version"] | "";

            if (chunkIndex >= 0 && totalChunks > 0 && !chunkData.isEmpty()) {
                // Directly handle the chunk (this is fast - just memory copies)
                handleUrlChunkDirect(chunkIndex, totalChunks, chunkData, version);
            }
        }
        return;
    }

    // For other commands, defer to handle() in main loop
    memcpy(_pendingBuffer, tempBuffer, len + 1);
    _pendingLength = len;
    _hasPendingCommand = true;
}

void OTABleService::processCommand(const String& json) {
    SDLogger::getInstance().infof("OTA command received: %s", json.c_str());

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        sendError("Invalid JSON");
        return;
    }

    String action = doc["action"] | "";

    if (action == "ota_update") {
        String url = doc["firmware_url"] | "";
        String version = doc["version"] | "";
        handleOTAUpdate(url, version);
    }
    else if (action == "url_chunk") {
        int chunkIndex = doc["chunk_index"] | -1;
        int totalChunks = doc["total_chunks"] | 0;
        String chunkData = doc["chunk_data"] | "";
        String version = doc["version"] | "";
        handleUrlChunk(chunkIndex, totalChunks, chunkData, version);
    }
    else if (action == "get_status") {
        handleGetStatus();
    }
    else if (action == "cancel_update") {
        // Reset chunk state
        _urlLength = 0;
        _expectedChunks = 0;
        _receivedChunks = 0;
        _urlBuffer[0] = '\0';
        sendStatus("success", "Update cancelled");
    }
    else {
        sendError("Unknown action: " + action);
    }
}

void OTABleService::handleOTAUpdate(const String& url, const String& version) {
    if (!_otaUpdate) {
        sendError("OTA not available");
        return;
    }

    if (url.isEmpty()) {
        sendError("URL required");
        return;
    }

    if (!_wifi) {
        sendError("WiFi not available");
        return;
    }

    // Auto-connect WiFi if not connected but credentials are available
    if (!_wifi->isConnected()) {
        if (!_wifi->hasCredentials()) {
            sendError("WiFi not configured");
            return;
        }

        SDLogger::getInstance().info("WiFi not connected, attempting to connect...");
        sendProgress(0, "Connecting to WiFi...");

        int result = _wifi->connect();
        if (result != 0 || !_wifi->isConnected()) {
            sendError("WiFi connection failed");
            return;
        }

        SDLogger::getInstance().info("WiFi connected successfully");
    }

    SDLogger::getInstance().infof("Starting OTA update from: %s (version: %s)", url.c_str(), version.c_str());
    sendProgress(5, "Starting download...");

    // Use SD card based OTA (with bootloader)
    _otaUpdate->downloadToSD(url.c_str());
}

void OTABleService::handleUrlChunk(int chunkIndex, int totalChunks, const String& chunkData, const String& version) {
    // This version is called from deferred processing - uses full logging and error reporting
    handleUrlChunkDirect(chunkIndex, totalChunks, chunkData, version);
}

void OTABleService::handleUrlChunkDirect(int chunkIndex, int totalChunks, const String& chunkData, const String& version) {
    // Called directly from BLE callback - must be fast, minimal logging
    // This prevents race conditions where chunks overwrite each other before processing

    // Initialize state on first chunk we receive or reset on chunk 0
    if (_expectedChunks == 0 || chunkIndex == 0) {
        _expectedChunks = totalChunks;
        _receivedChunks = 0;
        _pendingVersion = version;
        memset(_urlBuffer, 0, MAX_URL_SIZE);
        _urlLength = 0;
    }

    // If chunk count changed mid-stream, reinitialize
    if (totalChunks != _expectedChunks) {
        _expectedChunks = totalChunks;
        _receivedChunks = 0;
        memset(_urlBuffer, 0, MAX_URL_SIZE);
        _urlLength = 0;
    }

    int chunkLen = chunkData.length();

    // Check buffer space
    if (_urlLength + chunkLen >= MAX_URL_SIZE - 1) {
        _urlLength = 0;
        _expectedChunks = 0;
        return;
    }

    // Append chunk data
    memcpy(_urlBuffer + _urlLength, chunkData.c_str(), chunkLen);
    _urlLength += chunkLen;
    _urlBuffer[_urlLength] = '\0';
    _receivedChunks++;

    // All chunks received - defer OTA start to main loop
    if (_receivedChunks >= _expectedChunks) {
        _pendingOtaUrl = String(_urlBuffer);

        // Reset chunk state
        _urlLength = 0;
        _expectedChunks = 0;
        _receivedChunks = 0;

        // Signal main loop to start OTA
        _startOtaPending = true;
    }
}

void OTABleService::handleGetStatus() {
    DynamicJsonDocument doc(256);
    doc["status"] = "success";
    doc["message"] = "Ready";
    doc["version"] = _versionString;

    String json;
    serializeJson(doc, json);
    _pStatusChar->setValue(json.c_str());
    _pStatusChar->notify();

    SDLogger::getInstance().infof("Status sent: %s", json.c_str());
}

void OTABleService::sendStatus(const String& status, const String& message, int progress) {
    if (!_pStatusChar) return;

    DynamicJsonDocument doc(256);
    doc["status"] = status;
    doc["message"] = message;
    doc["version"] = _versionString;
    if (progress >= 0) {
        doc["progress"] = progress;
    }

    String json;
    serializeJson(doc, json);
    _pStatusChar->setValue(json.c_str());
    _pStatusChar->notify();

    SDLogger::getInstance().infof("OTA status: %s", json.c_str());
}

void OTABleService::sendProgress(int progress, const String& message) {
    sendStatus("progress", message, progress);
}

void OTABleService::sendError(const String& message) {
    sendStatus("error", message);
}

void OTABleService::sendSuccess(const String& message) {
    sendStatus("success", message);
}
