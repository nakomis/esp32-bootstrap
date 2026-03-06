#pragma once

#include <Arduino.h>

// Forward declarations
class OTAUpdate;
class MqttService;
class CommandDispatcher;
struct CommandContext;
class IResponseSender;

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
    void setStopBleOnOTA(bool stop) { _stopBleOnOTA = stop; }

    /**
     * Register OTA command handlers with the dispatcher
     */
    void registerCommands(CommandDispatcher* dispatcher);

private:
    OTAUpdate* _otaUpdate;
    MqttService* _mqttService;
    bool _stopBleOnOTA = false;

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
