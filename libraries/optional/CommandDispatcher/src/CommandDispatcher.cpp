#include "CommandDispatcher.h"

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
    : _rebootCallback(nullptr)
{
    // Register built-in handlers
    registerHandler("ping", [this](CommandContext& ctx) { return handlePing(ctx); });
    registerHandler("reboot", [this](CommandContext& ctx) { return handleReboot(ctx); });
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
        Serial.printf("CommandDispatcher: null sender\n");
        return false;
    }

    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, jsonCommand);

    if (error) {
        Serial.printf("CommandDispatcher: Invalid JSON: %s\n", jsonCommand.c_str());
        sendError(sender, "Invalid JSON command");
        return false;
    }

    String command = doc["command"] | "";
    if (command.isEmpty()) {
        Serial.printf("CommandDispatcher: Missing command field\n");
        sendError(sender, "Missing 'command' field");
        return false;
    }

    Serial.printf("CommandDispatcher [%s]: %s\n", sender->getName(), command.c_str());

    // Check if command requires chunking but sender doesn't support it
    if (requiresChunking(command) && !sender->supportsChunking()) {
        Serial.printf("CommandDispatcher: Command '%s' requires chunking, not supported by %s\n",
                      command.c_str(), sender->getName());
        sendError(sender, "Command requires chunked transfer (use Bluetooth)");
        return false;
    }

    // Find handler
    auto it = _handlers.find(command);
    if (it == _handlers.end()) {
        Serial.printf("CommandDispatcher: Unknown command: %s\n", command.c_str());
        sendError(sender, "Unknown command: " + command);
        return false;
    }

    // Execute handler
    CommandContext ctx(doc, sender);
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
    Serial.printf("Ping received, sending pong to %s\n", ctx.sender->getName());
    ctx.sender->sendResponse(responseStr);
    Serial.printf("Pong sent to %s\n", ctx.sender->getName());
    return true;
}

bool CommandDispatcher::handleReboot(CommandContext& ctx) {
    Serial.printf("Reboot requested via %s\n", ctx.sender->getName());

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
