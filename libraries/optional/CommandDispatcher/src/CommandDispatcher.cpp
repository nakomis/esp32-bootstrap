#include "CommandDispatcher.h"
#include "SystemState.h"
#include "SDLogger.h"
#include "../../../include/version.h"

// Commands that require chunking - only work via BLE
const char* CommandDispatcher::CHUNKED_COMMANDS[] = {
    "get_image",
    "get_logs",
    "request_logs",
    "list_images",
    "list_logs",
    "get_log_file",
    nullptr  // Sentinel
};

CommandDispatcher::CommandDispatcher()
    : _systemState(nullptr)
    , _photoCaptureCallback(nullptr)
    , _trainingModeCallback(nullptr)
    , _cameraSettingCallback(nullptr)
    , _rebootCallback(nullptr)
{
    // Register built-in handlers
    registerHandler("ping", [this](CommandContext& ctx) { return handlePing(ctx); });
    registerHandler("get_status", [this](CommandContext& ctx) { return handleGetStatus(ctx); });
    registerHandler("get_settings", [this](CommandContext& ctx) { return handleGetSettings(ctx); });
    registerHandler("get_camera_settings", [this](CommandContext& ctx) { return handleGetCameraSettings(ctx); });
    registerHandler("set_setting", [this](CommandContext& ctx) { return handleSetSetting(ctx); });
    registerHandler("take_photo", [this](CommandContext& ctx) { return handleTakePhoto(ctx); });
    registerHandler("reboot", [this](CommandContext& ctx) { return handleReboot(ctx); });
    registerHandler("get_version", [this](CommandContext& ctx) { return handleGetVersion(ctx); });
}

void CommandDispatcher::registerHandler(const String& command, CommandHandler handler) {
    _handlers[command] = handler;
}

bool CommandDispatcher::requiresChunking(const String& command) const {
    for (int i = 0; CHUNKED_COMMANDS[i] != nullptr; i++) {
        if (command == CHUNKED_COMMANDS[i]) {
            return true;
        }
    }
    return false;
}

bool CommandDispatcher::processCommand(const String& jsonCommand, IResponseSender* sender) {
    if (!sender) {
        SDLogger::getInstance().errorf("CommandDispatcher: null sender");
        return false;
    }

    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, jsonCommand);

    if (error) {
        SDLogger::getInstance().warnf("CommandDispatcher: Invalid JSON: %s", jsonCommand.c_str());
        sendError(sender, "Invalid JSON command");
        return false;
    }

    String command = doc["command"] | "";
    if (command.isEmpty()) {
        SDLogger::getInstance().warnf("CommandDispatcher: Missing command field");
        sendError(sender, "Missing 'command' field");
        return false;
    }

    SDLogger::getInstance().infof("CommandDispatcher [%s]: %s", sender->getName(), command.c_str());

    // Check if command requires chunking but sender doesn't support it
    if (requiresChunking(command) && !sender->supportsChunking()) {
        SDLogger::getInstance().warnf("CommandDispatcher: Command '%s' requires chunking, not supported by %s",
                                       command.c_str(), sender->getName());
        sendError(sender, "Command requires chunked transfer (use Bluetooth)");
        return false;
    }

    // Find handler
    auto it = _handlers.find(command);
    if (it == _handlers.end()) {
        SDLogger::getInstance().warnf("CommandDispatcher: Unknown command: %s", command.c_str());
        sendError(sender, "Unknown command: " + command);
        return false;
    }

    // Execute handler
    CommandContext ctx(doc, sender, _systemState);
    return it->second(ctx);
}

void CommandDispatcher::sendError(IResponseSender* sender, const String& message) {
    DynamicJsonDocument errorDoc(256);
    errorDoc["type"] = "error";
    errorDoc["message"] = message;

    String errorJson;
    serializeJson(errorDoc, errorJson);
    sender->sendResponse(errorJson);
}

bool CommandDispatcher::handlePing(CommandContext& ctx) {
    DynamicJsonDocument response(256);
    response["type"] = "pong";
    response["timestamp"] = millis();

    String responseStr;
    serializeJson(response, responseStr);
    SDLogger::getInstance().infof("Ping received, sending pong to %s", ctx.sender->getName());
    ctx.sender->sendResponse(responseStr);
    SDLogger::getInstance().infof("Pong sent to %s", ctx.sender->getName());
    return true;
}

bool CommandDispatcher::handleGetStatus(CommandContext& ctx) {
    if (!_systemState) {
        sendError(ctx.sender, "System state not available");
        return false;
    }

    DynamicJsonDocument response(1024);
    unsigned long uptime = millis() - _systemState->systemStartTime;

    response["type"] = "status";
    response["device"] = "BootBoots-CatCam";
    response["timestamp"] = millis();
    response["uptime_seconds"] = uptime / 1000;

    JsonObject sys = response.createNestedObject("system");
    sys["initialized"] = _systemState->initialized;
    sys["camera_ready"] = _systemState->cameraReady;
    sys["wifi_connected"] = _systemState->wifiConnected;
    sys["sd_card_ready"] = _systemState->sdCardReady;
    sys["i2c_ready"] = _systemState->i2cReady;
    sys["pcf8574_ready"] = _systemState->pcf8574Ready;
    sys["atomizer_enabled"] = _systemState->atomizerEnabled;
    sys["training_mode"] = _systemState->trainingMode;

    JsonObject stats = response.createNestedObject("statistics");
    stats["total_detections"] = _systemState->totalDetections;
    stats["boots_detections"] = _systemState->bootsDetections;
    stats["atomizer_activations"] = _systemState->atomizerActivations;
    stats["false_positives_avoided"] = _systemState->falsePositivesAvoided;

    JsonObject peripherals = response.createNestedObject("peripherals");
    peripherals["pir_active"] = _systemState->pirActive;
    peripherals["flash_led_on"] = _systemState->flashLedOn;
    peripherals["led_strip_on"] = _systemState->ledStripOn;
    peripherals["spray_on"] = _systemState->sprayOn;

    String responseStr;
    serializeJson(response, responseStr);
    ctx.sender->sendResponse(responseStr);

    SDLogger::getInstance().infof("Status request via %s", ctx.sender->getName());
    return true;
}

bool CommandDispatcher::handleGetSettings(CommandContext& ctx) {
    if (!_systemState) {
        sendError(ctx.sender, "System state not available");
        return false;
    }

    DynamicJsonDocument response(256);
    response["type"] = "settings";
    response["training_mode"] = _systemState->trainingMode;
    response["trigger_threshold"] = _systemState->triggerThresh;
    response["dry_run"] = _systemState->dryRun;
    response["claude_infer"] = _systemState->claudeInfer;

    String responseStr;
    serializeJson(response, responseStr);
    ctx.sender->sendResponse(responseStr);

    SDLogger::getInstance().infof("Get settings request via %s", ctx.sender->getName());
    return true;
}

bool CommandDispatcher::handleGetCameraSettings(CommandContext& ctx) {
    if (!_systemState) {
        sendError(ctx.sender, "System state not available");
        return false;
    }

    DynamicJsonDocument response(768);
    response["type"] = "camera_settings";

    JsonObject cam = response.createNestedObject("camera");
    cam["frame_size"] = _systemState->cameraSettings.frameSize;
    cam["jpeg_quality"] = _systemState->cameraSettings.jpegQuality;
    cam["fb_count"] = _systemState->cameraSettings.fbCount;
    cam["brightness"] = _systemState->cameraSettings.brightness;
    cam["contrast"] = _systemState->cameraSettings.contrast;
    cam["saturation"] = _systemState->cameraSettings.saturation;
    cam["special_effect"] = _systemState->cameraSettings.specialEffect;
    cam["white_balance"] = _systemState->cameraSettings.whiteBalance;
    cam["awb_gain"] = _systemState->cameraSettings.awbGain;
    cam["wb_mode"] = _systemState->cameraSettings.wbMode;
    cam["exposure_ctrl"] = _systemState->cameraSettings.exposureCtrl;
    cam["aec2"] = _systemState->cameraSettings.aec2;
    cam["ae_level"] = _systemState->cameraSettings.aeLevel;
    cam["aec_value"] = _systemState->cameraSettings.aecValue;
    cam["gain_ctrl"] = _systemState->cameraSettings.gainCtrl;
    cam["agc_gain"] = _systemState->cameraSettings.agcGain;
    cam["gain_ceiling"] = _systemState->cameraSettings.gainCeiling;
    cam["bpc"] = _systemState->cameraSettings.bpc;
    cam["wpc"] = _systemState->cameraSettings.wpc;
    cam["raw_gma"] = _systemState->cameraSettings.rawGma;
    cam["lenc"] = _systemState->cameraSettings.lenc;
    cam["hmirror"] = _systemState->cameraSettings.hmirror;
    cam["vflip"] = _systemState->cameraSettings.vflip;
    cam["dcw"] = _systemState->cameraSettings.dcw;
    cam["colorbar"] = _systemState->cameraSettings.colorbar;
    cam["led_delay_millis"] = _systemState->cameraSettings.ledDelayMillis;

    String responseStr;
    serializeJson(response, responseStr);
    ctx.sender->sendResponse(responseStr);

    SDLogger::getInstance().infof("Get camera settings request via %s", ctx.sender->getName());
    return true;
}

bool CommandDispatcher::handleSetSetting(CommandContext& ctx) {
    if (!_systemState) {
        sendError(ctx.sender, "System state not available");
        return false;
    }

    String setting = ctx.request["setting"] | "";
    if (setting.isEmpty()) {
        sendError(ctx.sender, "Missing 'setting' field");
        return false;
    }

    if (setting == "training_mode") {
        bool value = ctx.request["value"] | false;
        SDLogger::getInstance().infof("Setting training_mode to %s via %s",
                                       value ? "true" : "false", ctx.sender->getName());
        _systemState->trainingMode = value;

        if (_trainingModeCallback) {
            _trainingModeCallback(value);
        }

        DynamicJsonDocument response(256);
        response["type"] = "setting_updated";
        response["setting"] = "training_mode";
        response["value"] = value;
        String responseStr;
        serializeJson(response, responseStr);
        ctx.sender->sendResponse(responseStr);
        return true;
    }

    if (setting.startsWith("camera_")) {
        String camSetting = setting.substring(7);  // Strip "camera_"
        int intValue = ctx.request["value"] | 0;
        bool boolValue = ctx.request["value"] | false;
        bool handled = true;

        if (camSetting == "frame_size") { _systemState->cameraSettings.frameSize = intValue; }
        else if (camSetting == "jpeg_quality") { _systemState->cameraSettings.jpegQuality = intValue; }
        else if (camSetting == "fb_count") { _systemState->cameraSettings.fbCount = intValue; }
        else if (camSetting == "brightness") { _systemState->cameraSettings.brightness = intValue; }
        else if (camSetting == "contrast") { _systemState->cameraSettings.contrast = intValue; }
        else if (camSetting == "saturation") { _systemState->cameraSettings.saturation = intValue; }
        else if (camSetting == "special_effect") { _systemState->cameraSettings.specialEffect = intValue; }
        else if (camSetting == "white_balance") { _systemState->cameraSettings.whiteBalance = boolValue; }
        else if (camSetting == "awb_gain") { _systemState->cameraSettings.awbGain = boolValue; }
        else if (camSetting == "wb_mode") { _systemState->cameraSettings.wbMode = intValue; }
        else if (camSetting == "exposure_ctrl") { _systemState->cameraSettings.exposureCtrl = boolValue; }
        else if (camSetting == "aec2") { _systemState->cameraSettings.aec2 = boolValue; }
        else if (camSetting == "ae_level") { _systemState->cameraSettings.aeLevel = intValue; }
        else if (camSetting == "aec_value") { _systemState->cameraSettings.aecValue = intValue; }
        else if (camSetting == "gain_ctrl") { _systemState->cameraSettings.gainCtrl = boolValue; }
        else if (camSetting == "agc_gain") { _systemState->cameraSettings.agcGain = intValue; }
        else if (camSetting == "gain_ceiling") { _systemState->cameraSettings.gainCeiling = intValue; }
        else if (camSetting == "bpc") { _systemState->cameraSettings.bpc = boolValue; }
        else if (camSetting == "wpc") { _systemState->cameraSettings.wpc = boolValue; }
        else if (camSetting == "raw_gma") { _systemState->cameraSettings.rawGma = boolValue; }
        else if (camSetting == "lenc") { _systemState->cameraSettings.lenc = boolValue; }
        else if (camSetting == "hmirror") { _systemState->cameraSettings.hmirror = boolValue; }
        else if (camSetting == "vflip") { _systemState->cameraSettings.vflip = boolValue; }
        else if (camSetting == "dcw") { _systemState->cameraSettings.dcw = boolValue; }
        else if (camSetting == "colorbar") { _systemState->cameraSettings.colorbar = boolValue; }
        else if (camSetting == "led_delay_millis") { _systemState->cameraSettings.ledDelayMillis = intValue; }
        else { handled = false; }

        if (handled) {
            SDLogger::getInstance().infof("Camera setting %s updated via %s",
                                           camSetting.c_str(), ctx.sender->getName());

            if (_cameraSettingCallback) {
                _cameraSettingCallback(camSetting, intValue);
            }

            DynamicJsonDocument response(256);
            response["type"] = "setting_updated";
            response["setting"] = setting;
            response["value"] = ctx.request["value"];
            String responseStr;
            serializeJson(response, responseStr);
            ctx.sender->sendResponse(responseStr);
            return true;
        }

        sendError(ctx.sender, "Unknown camera setting: " + camSetting);
        return false;
    }

    sendError(ctx.sender, "Unknown setting: " + setting);
    return false;
}

bool CommandDispatcher::handleTakePhoto(CommandContext& ctx) {
    if (!_photoCaptureCallback) {
        sendError(ctx.sender, "Photo capture not available");
        return false;
    }

    SDLogger::getInstance().infof("Take photo request via %s", ctx.sender->getName());

    // Send acknowledgment
    DynamicJsonDocument ackDoc(128);
    ackDoc["type"] = "photo_started";
    ackDoc["message"] = "Capturing photo...";
    String ackJson;
    serializeJson(ackDoc, ackJson);
    ctx.sender->sendResponse(ackJson);

    // Capture the photo
    String newFilename = _photoCaptureCallback();

    // Send completion
    DynamicJsonDocument completeDoc(256);
    completeDoc["type"] = "photo_complete";
    completeDoc["message"] = "Photo captured and saved";
    completeDoc["filename"] = newFilename;
    String completeJson;
    serializeJson(completeDoc, completeJson);
    ctx.sender->sendResponse(completeJson);

    return true;
}

bool CommandDispatcher::handleReboot(CommandContext& ctx) {
    SDLogger::getInstance().infof("Reboot requested via %s", ctx.sender->getName());

    DynamicJsonDocument response(128);
    response["type"] = "reboot_ack";
    response["message"] = "Rebooting...";
    String responseStr;
    serializeJson(response, responseStr);
    ctx.sender->sendResponse(responseStr);

    // Delay to allow response to be sent
    delay(500);

    if (_rebootCallback) {
        _rebootCallback();
    } else {
        ESP.restart();
    }

    return true;
}

bool CommandDispatcher::handleGetVersion(CommandContext& ctx) {
    DynamicJsonDocument response(256);
    response["type"] = "version";
    response["version"] = FIRMWARE_VERSION;
    response["project"] = PROJECT_NAME;

    String responseStr;
    serializeJson(response, responseStr);
    ctx.sender->sendResponse(responseStr);

    SDLogger::getInstance().infof("Version request via %s: %s", ctx.sender->getName(), FIRMWARE_VERSION);
    return true;
}
