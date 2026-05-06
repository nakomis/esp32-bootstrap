#include "MqttOTA.h"
#include <ArduinoJson.h>
#include <esp_bt.h>
#include "../../OTAUpdate/src/OTAUpdate.h"
#include "../../MqttService/src/MqttService.h"
#include "../../BluetoothOTA/src/BluetoothOTA.h"
#include "../../SDLogger/src/SDLogger.h"
#include "../../../include/version.h"

MqttOTA::MqttOTA()
    : _otaUpdate(nullptr)
    , _mqttService(nullptr)
    , _bleOTA(nullptr)
    , _totalChunks(0)
    , _receivedChunks(0)
{
}

void MqttOTA::registerCommands(CommandDispatcher* dispatcher) {
    dispatcher->registerHandler("ota_update", [this](CommandContext& ctx) {
        return handleOtaUpdate(ctx);
    });
    dispatcher->registerHandler("url_chunk", [this](CommandContext& ctx) {
        return handleUrlChunk(ctx);
    });
    dispatcher->registerHandler("ota_cancel", [this](CommandContext& ctx) {
        return handleOtaCancel(ctx);
    });

    SDLogger::getInstance().infof("MqttOTA: OTA command handlers registered");
}

bool MqttOTA::handleOtaUpdate(CommandContext& ctx) {
    String url = ctx.request["url"] | "";

    if (url.isEmpty()) {
        DynamicJsonDocument errorDoc(256);
        errorDoc["type"] = "ota_error";
        errorDoc["message"] = "No firmware URL provided";
        String errorJson;
        serializeJson(errorDoc, errorJson);
        ctx.sender->sendResponse(errorJson);
        return false;
    }

    String version = ctx.request["version"] | "";

    SDLogger::getInstance().infof("MqttOTA: OTA update requested via %s, URL length: %d",
                                   ctx.sender->getName(), url.length());

    processOTAUpdate(ctx.sender, url, version);
    return true;
}

bool MqttOTA::handleUrlChunk(CommandContext& ctx) {
    int chunkIndex = ctx.request["chunk_index"] | -1;
    int totalChunks = ctx.request["total_chunks"] | 0;
    String chunkData = ctx.request["chunk_data"] | "";
    String version = ctx.request["version"] | "";

    SDLogger::getInstance().infof("MqttOTA: URL chunk %d/%d (%d bytes)",
                                   chunkIndex + 1, totalChunks, chunkData.length());

    if (chunkIndex < 0 || chunkIndex >= MAX_CHUNKS || totalChunks > MAX_CHUNKS || totalChunks <= 0) {
        DynamicJsonDocument errorDoc(256);
        errorDoc["type"] = "ota_error";
        errorDoc["message"] = "Invalid chunk parameters";
        String errorJson;
        serializeJson(errorDoc, errorJson);
        ctx.sender->sendResponse(errorJson);
        return false;
    }

    // Reset if this is the first chunk or total changed
    if (chunkIndex == 0 || totalChunks != _totalChunks) {
        _totalChunks = totalChunks;
        _receivedChunks = 0;
        _chunkVersion = version;
        for (int i = 0; i < MAX_CHUNKS; i++) {
            _urlChunks[i] = "";
        }
    }

    _urlChunks[chunkIndex] = chunkData;
    _receivedChunks++;

    if (_receivedChunks >= _totalChunks) {
        // Reassemble URL
        String fullUrl = "";
        for (int i = 0; i < _totalChunks; i++) {
            fullUrl += _urlChunks[i];
        }

        SDLogger::getInstance().infof("MqttOTA: URL reassembled (%d bytes)", fullUrl.length());

        // Reset chunk state
        _totalChunks = 0;
        _receivedChunks = 0;

        processOTAUpdate(ctx.sender, fullUrl, _chunkVersion);
    } else {
        // Acknowledge chunk
        DynamicJsonDocument ackDoc(256);
        ackDoc["type"] = "ota_progress";
        ackDoc["progress"] = 0;
        ackDoc["status"] = "Chunk " + String(chunkIndex + 1) + "/" + String(totalChunks) + " received";
        String ackJson;
        serializeJson(ackDoc, ackJson);
        ctx.sender->sendResponse(ackJson);
    }

    return true;
}

bool MqttOTA::handleOtaCancel(CommandContext& ctx) {
    if (_otaUpdate && _otaUpdate->isUpdating()) {
        _otaUpdate->cancelUpdate();
        SDLogger::getInstance().infof("MqttOTA: Update cancelled via %s", ctx.sender->getName());
    }

    // Reset chunk state
    _totalChunks = 0;
    _receivedChunks = 0;

    DynamicJsonDocument response(256);
    response["type"] = "ota_progress";
    response["progress"] = 0;
    response["status"] = "Update cancelled";
    String responseStr;
    serializeJson(response, responseStr);
    ctx.sender->sendResponse(responseStr);

    return true;
}

void MqttOTA::processOTAUpdate(IResponseSender* sender, const String& url, const String& version) {
    if (!_otaUpdate) {
        DynamicJsonDocument errorDoc(256);
        errorDoc["type"] = "ota_error";
        errorDoc["message"] = "OTA update service not available";
        String errorJson;
        serializeJson(errorDoc, errorJson);
        sender->sendResponse(errorJson);
        return;
    }

    // Send acknowledgment before pausing MQTT
    {
        DynamicJsonDocument ackDoc(256);
        ackDoc["type"] = "ota_progress";
        ackDoc["progress"] = 0;
        ackDoc["status"] = "Starting OTA update - device will reboot when complete";
        ackDoc["version"] = version;
        String ackJson;
        serializeJson(ackDoc, ackJson);
        sender->sendResponse(ackJson);
    }

    // Give time for the response to be sent
    delay(500);

    SDLogger::getInstance().infof("MqttOTA: Pausing MQTT to free memory for OTA download");

    // Pause MQTT to free ~40KB of SSL memory needed for firmware download
    if (_mqttService) {
        _mqttService->pause();
    }

    // Fully shut down the BLE stack to recover ~50-80KB of internal SRAM.
    // Stopping advertising alone only frees ~2KB, leaving the internal heap too
    // fragmented for the SSL handshake (~40KB contiguous block needed).
    // btStop() + esp_bt_mem_release() bypass the Arduino BLE wrapper (which hangs
    // on deinit per esp32-snippets #1155) and go direct to ESP-IDF. The device
    // always reboots after OTA so losing BLE here is fine.
    if (_bleOTA) {
        SDLogger::getInstance().infof("MqttOTA: Stopping BLE advertising");
        BLEDevice::stopAdvertising();
    }

    delay(500);  // Let any in-flight BLE packets flush

    SDLogger::getInstance().infof("MqttOTA: Deinitialising BLE stack to free internal SRAM for SSL...");
    btStop();
    esp_bt_mem_release(ESP_BT_MODE_BLE);

    delay(500);  // Let the BLE stack fully shut down

    SDLogger::getInstance().infof("MqttOTA: Free heap before download: %d bytes", ESP.getFreeHeap());
    SDLogger::getInstance().infof("MqttOTA: Largest free block: %d bytes", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    // Download firmware to SD card - device will reboot on completion
    bool started = _otaUpdate->downloadToSD(url.c_str());

    if (!started) {
        SDLogger::getInstance().errorf("MqttOTA: Failed to start OTA download");
        // Device will likely reboot due to OTAUpdate error handling
    }

    // Note: If download succeeds, device reboots automatically.
    // If it fails, OTAUpdate::downloadToSD() also reboots.
}
