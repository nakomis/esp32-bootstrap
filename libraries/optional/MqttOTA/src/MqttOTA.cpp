#include "MqttOTA.h"
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include "OTAUpdate.h"
#include "MqttService.h"
#include "CommandDispatcher.h"

MqttOTA::MqttOTA()
    : _otaUpdate(nullptr)
    , _mqttService(nullptr)
    , _stopBleOnOTA(false)
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

    Serial.printf("MqttOTA: OTA command handlers registered\n");
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

    Serial.printf("MqttOTA: OTA update requested via %s, URL length: %d\n",
                  ctx.sender->getName(), url.length());

    processOTAUpdate(ctx.sender, url, version);
    return true;
}

bool MqttOTA::handleUrlChunk(CommandContext& ctx) {
    int chunkIndex = ctx.request["chunk_index"] | -1;
    int totalChunks = ctx.request["total_chunks"] | 0;
    String chunkData = ctx.request["chunk_data"] | "";
    String version = ctx.request["version"] | "";

    Serial.printf("MqttOTA: URL chunk %d/%d (%d bytes)\n",
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

        Serial.printf("MqttOTA: URL reassembled (%d bytes)\n", fullUrl.length());

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
        Serial.printf("MqttOTA: Update cancelled via %s\n", ctx.sender->getName());
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

    Serial.printf("MqttOTA: Pausing MQTT to free memory for OTA download\n");

    // Pause MQTT to free ~40KB of SSL memory needed for firmware download
    if (_mqttService) {
        _mqttService->pause();
    }

    // Stop BLE advertising to free additional memory
    if (_stopBleOnOTA) {
        Serial.printf("MqttOTA: Stopping BLE advertising\n");
        BLEDevice::stopAdvertising();
    }

    delay(1000);
    Serial.printf("MqttOTA: Free heap before download: %d bytes\n", ESP.getFreeHeap());

    // Download firmware to SD card - device will reboot on completion
    bool started = _otaUpdate->downloadToSD(url.c_str());

    if (!started) {
        Serial.printf("MqttOTA: Failed to start OTA download\n");
        // Device will likely reboot due to OTAUpdate error handling
    }

    // Note: If download succeeds, device reboots automatically.
    // If it fails, OTAUpdate::downloadToSD() also reboots.
}
