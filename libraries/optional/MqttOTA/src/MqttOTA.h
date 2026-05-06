#pragma once

#include <Arduino.h>
#include "../../CommandDispatcher/src/CommandDispatcher.h"

// Forward declarations
class OTAUpdate;
class MqttService;
class BluetoothOTA;

/**
 * MqttOTA - Handles OTA firmware updates via MQTT commands
 *
 * Registers ota_update, url_chunk, and ota_cancel command handlers
 * with CommandDispatcher so OTA can be triggered remotely via MQTT.
 *
 * Since MQTT must be paused to free SSL memory for the firmware download,
 * progress cannot be sent back via MQTT. The device sends an acknowledgment
 * before pausing, then downloads and reboots.
 */
class MqttOTA {
public:
    MqttOTA();

    void setOTAUpdate(OTAUpdate* otaUpdate) { _otaUpdate = otaUpdate; }
    void setMqttService(MqttService* mqttService) { _mqttService = mqttService; }
    void setBluetoothOTA(BluetoothOTA* bleOTA) { _bleOTA = bleOTA; }

    /**
     * Register OTA command handlers with the dispatcher
     */
    void registerCommands(CommandDispatcher* dispatcher);

private:
    OTAUpdate* _otaUpdate;
    MqttService* _mqttService;
    BluetoothOTA* _bleOTA;

    // URL chunking state (for long S3 signed URLs)
    static const int MAX_CHUNKS = 10;
    String _urlChunks[MAX_CHUNKS];
    int _totalChunks;
    int _receivedChunks;
    String _chunkVersion;

    // Command handlers
    bool handleOtaUpdate(CommandContext& ctx);
    bool handleUrlChunk(CommandContext& ctx);
    bool handleOtaCancel(CommandContext& ctx);

    // Shared OTA processing
    void processOTAUpdate(IResponseSender* sender, const String& url, const String& version);
};
