#include "BluetoothOTA.h"
#include <ArduinoJson.h>
#include <esp_bt.h>
#include "../../../include/version.h"

// Static instance for progress callbacks
static BluetoothOTA* _bleOtaInstance = nullptr;

// Static progress callback that forwards to the instance
static void otaProgressCallback(int progress, size_t downloaded, size_t total) {
    if (_bleOtaInstance && _bleOtaInstance->isConnected()) {
        // Send progress update via BLE - limit updates to every 5%
        static int lastReportedProgress = -1;
        if (progress != lastReportedProgress && (progress % 5 == 0 || progress == 100)) {
            char msg[96];
            if (progress == 100) {
                snprintf(msg, sizeof(msg), "Download complete! Rebooting to flash firmware...");
            } else {
                snprintf(msg, sizeof(msg), "Downloading: %d%% (%d/%d bytes)", progress, (int)downloaded, (int)total);
            }
            _bleOtaInstance->sendStatusUpdate("progress", msg, progress);
            lastReportedProgress = progress;
        }
    }
}

BluetoothOTA::BluetoothOTA() {
    _pServer = nullptr;
    _pService = nullptr;
    _pCommandCharacteristic = nullptr;
    _pStatusCharacteristic = nullptr;
    _pCallbacks = nullptr;
    _pServerCallbacks = nullptr;
    _otaUpdate = nullptr;
    _mqttService = nullptr;
    _initialized = false;
    _deviceConnected = false;
    _wasConnected = false;
    _pendingConnectNotify = false;
    _hasPendingCommand = false;
    _pendingLength = 0;
    memset(_pendingBuffer, 0, MAX_PENDING_SIZE);
    _deviceName = "BootBoots-CatCam";
    _totalChunks = 0;
    _receivedChunks = 0;
    _chunkVersion = "";

    // Store instance for static callbacks
    _bleOtaInstance = this;
}

BluetoothOTA::~BluetoothOTA() {
    if (_pCallbacks) delete _pCallbacks;
    if (_pServerCallbacks) delete _pServerCallbacks;
}

bool BluetoothOTA::init(const char* deviceName) {
    if (_initialized) {
        SDLogger::getInstance().warnf("Bluetooth OTA already initialized");
        return true;
    }

    _deviceName = String(deviceName);

    SDLogger::getInstance().infof("Initializing Bluetooth OTA service...");

    // Initialize BLE
    BLEDevice::init(_deviceName.c_str());

    // Create BLE Server
    _pServer = BLEDevice::createServer();
    if (!_pServer) {
        SDLogger::getInstance().errorf("Failed to create BLE server");
        return false;
    }

    // Set server callbacks
    _pServerCallbacks = new ServerCallbacks(this);
    _pServer->setCallbacks(_pServerCallbacks);

    // Create BLE Service
    _pService = _pServer->createService(OTA_SERVICE_UUID);
    if (!_pService) {
        SDLogger::getInstance().errorf("Failed to create BLE service");
        return false;
    }

    // Create Command Characteristic (Write)
    // Use PROPERTY_WRITE_NR (Write Without Response) for better reliability with large payloads
    _pCommandCharacteristic = _pService->createCharacteristic(
        OTA_COMMAND_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );

    if (!_pCommandCharacteristic) {
        SDLogger::getInstance().errorf("Failed to create command characteristic");
        return false;
    }

    // Set command characteristic callbacks
    _pCallbacks = new BluetoothOTACallbacks(this);
    _pCommandCharacteristic->setCallbacks(_pCallbacks);

    // Set maximum value length to accommodate large URLs (up to 512 bytes)
    _pCommandCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);

    // Create Status Characteristic (Read/Notify)
    _pStatusCharacteristic = _pService->createCharacteristic(
        OTA_STATUS_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );

    if (!_pStatusCharacteristic) {
        SDLogger::getInstance().errorf("Failed to create status characteristic");
        return false;
    }

    // Add descriptor for notifications
    _pStatusCharacteristic->addDescriptor(new BLE2902());

    // Set initial status value so clients can read the version
    String initialStatus = createStatusJson("ready", "Bluetooth OTA service ready", 0);
    _pStatusCharacteristic->setValue(initialStatus.c_str());

    // Start the service
    _pService->start();

    // Add service UUID to advertising (but don't start advertising yet)
    // Advertising will be started in main.cpp after all services are initialized
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(OTA_SERVICE_UUID);
    pAdvertising->setScanResponse(false);
    pAdvertising->setMinPreferred(0x0);

    _initialized = true;

    SDLogger::getInstance().infof("Bluetooth OTA service initialized successfully");
    SDLogger::getInstance().infof("Device name: %s", _deviceName.c_str());
    SDLogger::getInstance().infof("Service UUID: %s", OTA_SERVICE_UUID);

    return true;
}

bool BluetoothOTA::initWithExistingServer(BLEServer* pServer) {
    if (_initialized) {
        SDLogger::getInstance().warnf("Bluetooth OTA already initialized");
        return true;
    }

    if (!pServer) {
        SDLogger::getInstance().errorf("Invalid BLE server provided");
        return false;
    }

    SDLogger::getInstance().infof("Initializing Bluetooth OTA service with existing BLE server...");

    // Use the provided server instead of creating a new one
    _pServer = pServer;

    // Note: Don't set server callbacks - the main service already has them

    // Create BLE Service for OTA
    _pService = _pServer->createService(OTA_SERVICE_UUID);
    if (!_pService) {
        SDLogger::getInstance().errorf("Failed to create OTA BLE service");
        return false;
    }

    // Create Command Characteristic (Write)
    // Use PROPERTY_WRITE_NR (Write Without Response) for better reliability with large payloads
    _pCommandCharacteristic = _pService->createCharacteristic(
        OTA_COMMAND_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );

    if (!_pCommandCharacteristic) {
        SDLogger::getInstance().errorf("Failed to create command characteristic");
        return false;
    }

    // Set command characteristic callbacks
    _pCallbacks = new BluetoothOTACallbacks(this);
    _pCommandCharacteristic->setCallbacks(_pCallbacks);

    // Set maximum value length to accommodate large URLs (up to 512 bytes)
    _pCommandCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);

    // Create Status Characteristic (Read/Notify)
    _pStatusCharacteristic = _pService->createCharacteristic(
        OTA_STATUS_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );

    if (!_pStatusCharacteristic) {
        SDLogger::getInstance().errorf("Failed to create status characteristic");
        return false;
    }

    // Add descriptor for notifications
    _pStatusCharacteristic->addDescriptor(new BLE2902());

    // Set initial status value so clients can read the version
    String initialStatus = createStatusJson("ready", "Bluetooth OTA service ready", 0);
    _pStatusCharacteristic->setValue(initialStatus.c_str());

    // Start the service
    _pService->start();

    // Add service UUID to advertising
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(OTA_SERVICE_UUID);

    _initialized = true;

    SDLogger::getInstance().infof("Bluetooth OTA service initialized successfully with shared server");
    SDLogger::getInstance().infof("Service UUID: %s", OTA_SERVICE_UUID);

    return true;
}

void BluetoothOTA::handle() {
    if (!_initialized) return;

    // Handle deferred connect notification (avoid stack overflow in BLE callback)
    if (_pendingConnectNotify) {
        _pendingConnectNotify = false;
        sendStatusUpdate("connected", "Client connected to BootBoots");
    }

    // Handle deferred command processing (avoid stack overflow in BLE callback)
    if (_hasPendingCommand && _pendingLength > 0) {
        _hasPendingCommand = false;

        // Copy buffer to local String now that we're in main loop context
        String commandCopy = String(_pendingBuffer);
        _pendingLength = 0;
        _pendingBuffer[0] = '\0';

        // Process multiple commands if delimited by newlines (from queued chunks)
        int startPos = 0;
        int newlinePos;
        while ((newlinePos = commandCopy.indexOf('\n', startPos)) != -1) {
            String singleCommand = commandCopy.substring(startPos, newlinePos);
            if (singleCommand.length() > 0) {
                handleOTACommand(singleCommand);
            }
            startPos = newlinePos + 1;
        }
        // Process the last (or only) command
        if (startPos < (int)commandCopy.length()) {
            String lastCommand = commandCopy.substring(startPos);
            if (lastCommand.length() > 0) {
                handleOTACommand(lastCommand);
            }
        }
    }

    // Only restart advertising after a disconnection (not continuously)
    // This prevents aborting incoming connection attempts
    bool currentlyConnected = _pServer && _pServer->getConnectedCount() > 0;

    if (_wasConnected && !currentlyConnected) {
        // Just disconnected - restart advertising after a short delay
        SDLogger::getInstance().infof("BluetoothOTA: Client disconnected, restarting advertising");
        delay(500);
        _pServer->startAdvertising();
    }

    _wasConnected = currentlyConnected;
}

bool BluetoothOTA::isConnected() {
    // When using shared server (initWithExistingServer), _deviceConnected may not be set
    // because the server callbacks are on the main service, not this one.
    // So we primarily check getConnectedCount() which works regardless of callback setup.
    return _pServer && _pServer->getConnectedCount() > 0;
}

void BluetoothOTA::sendStatusUpdate(const String& status, const String& message, int progress) {
    if (!_pStatusCharacteristic || !isConnected()) {
        return;
    }
    
    String statusJson = createStatusJson(status, message, progress);
    _pStatusCharacteristic->setValue(statusJson.c_str());
    _pStatusCharacteristic->notify();
    
    SDLogger::getInstance().infof("Sent status update: %s", statusJson.c_str());
}

void BluetoothOTA::handleOTACommand(const String& commandJson) {
    SDLogger::getInstance().infof("Received OTA command: %s", commandJson.c_str());
    
    OTACommand command = parseCommand(commandJson);
    
    if (command.action == "ota_update") {
        processOTAUpdate(command);
    } else if (command.action == "url_chunk") {
        // Handle chunked URL transfer for long S3 signed URLs
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, commandJson);

        int chunkIndex = doc["chunk_index"] | -1;
        int totalChunks = doc["total_chunks"] | 0;
        String chunkData = doc["chunk_data"].as<String>();
        String version = doc["version"].as<String>();

        SDLogger::getInstance().infof("Received URL chunk %d/%d (%d bytes)",
                                      chunkIndex + 1, totalChunks, chunkData.length());

        if (chunkIndex < 0 || chunkIndex >= 10 || totalChunks > 10) {
            sendStatusUpdate("error", "Invalid chunk parameters");
            return;
        }

        // Reset if this is the first chunk or total changed
        if (chunkIndex == 0 || totalChunks != _totalChunks) {
            _totalChunks = totalChunks;
            _receivedChunks = 0;
            _chunkVersion = version;
            for (int i = 0; i < 10; i++) {
                _urlChunks[i] = "";
            }
        }

        // Store the chunk
        _urlChunks[chunkIndex] = chunkData;
        _receivedChunks++;

        // Check if all chunks received
        if (_receivedChunks >= _totalChunks) {
            // Reassemble the URL
            String fullUrl = "";
            for (int i = 0; i < _totalChunks; i++) {
                fullUrl += _urlChunks[i];
            }

            SDLogger::getInstance().infof("URL reassembled (%d bytes), starting OTA update", fullUrl.length());

            // Create OTA command with full URL
            OTACommand otaCmd;
            otaCmd.action = "ota_update";
            otaCmd.firmware_url = fullUrl;
            otaCmd.version = _chunkVersion;

            // Reset chunk state
            _totalChunks = 0;
            _receivedChunks = 0;

            // Process the OTA update
            processOTAUpdate(otaCmd);
        } else {
            sendStatusUpdate("chunk_received", "Chunk " + String(chunkIndex + 1) + "/" + String(totalChunks) + " received");
        }
    } else if (command.action == "get_status") {
        // Send current status
        if (_otaUpdate) {
            String status = _otaUpdate->getStatus();
            int progress = _otaUpdate->getProgress();
            sendStatusUpdate("status", status, progress);
        } else {
            sendStatusUpdate("status", "OTA service not available");
        }
    } else if (command.action == "cancel_update") {
        if (_otaUpdate) {
            _otaUpdate->cancelUpdate();
            sendStatusUpdate("cancelled", "OTA update cancelled");
        }
    } else {
        sendStatusUpdate("error", "Unknown command: " + command.action);
    }
}

OTACommand BluetoothOTA::parseCommand(const String& json) {
    OTACommand command;
    
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
        SDLogger::getInstance().errorf("Failed to parse command JSON: %s", error.c_str());
        command.action = "error";
        return command;
    }
    
    command.action = doc["action"].as<String>();
    command.firmware_url = doc["firmware_url"].as<String>();
    command.version = doc["version"].as<String>();
    
    return command;
}

String BluetoothOTA::createStatusJson(const String& status, const String& message, int progress) {
    DynamicJsonDocument doc(512);
    
    doc["status"] = status;
    doc["message"] = message;
    doc["progress"] = progress;
    doc["version"] = FIRMWARE_VERSION; // From version.h
    
    String json;
    serializeJson(doc, json);
    return json;
}

void BluetoothOTA::processOTAUpdate(const OTACommand& command) {
    if (!_otaUpdate) {
        sendStatusUpdate("error", "OTA update service not available");
        return;
    }

    if (command.firmware_url.isEmpty()) {
        sendStatusUpdate("error", "No firmware URL provided");
        return;
    }

    SDLogger::getInstance().infof("Starting OTA update from URL: %s", command.firmware_url.c_str());
    sendStatusUpdate("starting", "Starting OTA update...");

    // Free MQTT SSL resources first - this releases ~40KB of heap
    if (_mqttService) {
        _mqttService->pause();
    }

    // Fully shut down the BLE stack to recover ~50-80KB of internal SRAM.
    // Stopping advertising alone only frees ~2KB, leaving the internal heap too
    // fragmented for the SSL handshake (~40KB contiguous block needed).
    // btStop() + esp_bt_mem_release() bypass the Arduino BLE wrapper (which hangs
    // on deinit per esp32-snippets #1155) and go direct to ESP-IDF. The device
    // always reboots after OTA so losing BLE here is fine.
    SDLogger::getInstance().infof("Stopping BLE advertising for OTA update");
    if (_pServer) {
        _pServer->getAdvertising()->stop();
    }
    BLEDevice::stopAdvertising();

    // Give any in-flight BLE packets (e.g. the "starting" status update) time to flush
    delay(500);

    SDLogger::getInstance().infof("Deinitialising BLE stack to free internal SRAM for SSL...");
    btStop();
    esp_bt_mem_release(ESP_BT_MODE_BLE);

    // Null out BLE characteristic pointers so any progress callbacks that fire
    // during the download don't try to call notify() on the dead BLE stack.
    // sendStatusUpdate() guards on _pStatusCharacteristic being non-null.
    _pStatusCharacteristic = nullptr;
    _pCommandCharacteristic = nullptr;

    // Give the BLE stack time to fully shut down
    delay(500);

    SDLogger::getInstance().infof("Free heap after BLE deinit: %d bytes", ESP.getFreeHeap());
    SDLogger::getInstance().infof("Largest free block: %d bytes", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    // Set up progress callback to send BLE notifications during download
    _otaUpdate->setProgressCallback(otaProgressCallback);

    // Set up completion callback
    _otaUpdate->setUpdateCallback([](bool success, const char* error) {
        // This callback will be called when the update completes
        // Note: We can't access 'this' in a static callback, so we'll handle status in the main loop
    });

    // Start bootloader-based OTA update (download to SD, bootloader flashes on reboot)
    // BLE is now fully disabled, so we have enough memory
    bool started = _otaUpdate->downloadToSD(command.firmware_url.c_str());

    if (!started) {
        sendStatusUpdate("error", "Failed to start OTA update");
        SDLogger::getInstance().errorf("OTA update failed to start");
        return;
    }

    sendStatusUpdate("updating", "Updating firmware...", 0);
    // Note: Device will reboot automatically after flash completes
}

// Server Callbacks Implementation
void BluetoothOTA::ServerCallbacks::onConnect(BLEServer* pServer) {
    _parent->_deviceConnected = true;
    // Defer status update to handle() to avoid stack overflow in BLE callback context
    // The BTC_TASK has limited stack and sendStatusUpdate uses too much
    _parent->_pendingConnectNotify = true;
}

void BluetoothOTA::ServerCallbacks::onDisconnect(BLEServer* pServer) {
    _parent->_deviceConnected = false;
    SDLogger::getInstance().infof("BluetoothOTA client disconnected");
}

// Characteristic Callbacks Implementation
void BluetoothOTACallbacks::onWrite(BLECharacteristic* pCharacteristic) {
    // With reduced debug logging (CORE_DEBUG_LEVEL=2), we have more stack space
    // Copy data to buffer here so we don't lose chunks between handle() calls
    uint8_t* data = pCharacteristic->getData();
    size_t len = pCharacteristic->getLength();

    if (data && len > 0) {
        int currentLen = _parent->_pendingLength;
        int needed = currentLen + len + 1;  // +1 for newline delimiter

        if (needed < BluetoothOTA::MAX_PENDING_SIZE - 1) {
            // Add newline delimiter if not first command
            if (currentLen > 0) {
                _parent->_pendingBuffer[currentLen] = '\n';
                currentLen++;
            }
            // Copy raw bytes
            memcpy(_parent->_pendingBuffer + currentLen, data, len);
            _parent->_pendingLength = currentLen + len;
            _parent->_pendingBuffer[_parent->_pendingLength] = '\0';
            _parent->_hasPendingCommand = true;
        }
    }
}
