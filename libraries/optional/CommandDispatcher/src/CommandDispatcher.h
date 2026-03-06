#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>
#include <functional>

/**
 * Interface for sending responses back to the command source.
 * Implementations handle transport-specific details (BLE chunking, MQTT, etc.)
 */
class IResponseSender {
public:
    virtual ~IResponseSender() = default;

    /**
     * Send a complete response (for small responses that fit in one message)
     */
    virtual void sendResponse(const String& response) = 0;

    /**
     * Check if this sender supports chunked transfers (e.g., BLE does, MQTT doesn't)
     */
    virtual bool supportsChunking() const { return false; }

    /**
     * Get a human-readable name for logging
     */
    virtual const char* getName() const = 0;
};

/**
 * Context passed to command handlers with everything needed to process commands
 */
struct CommandContext {
    const JsonDocument& request;      // Parsed JSON request
    IResponseSender* sender;          // Response sender for this request

    CommandContext(const JsonDocument& req, IResponseSender* s)
        : request(req), sender(s) {}
};

/**
 * Command handler function signature
 * Returns true if command was handled successfully
 */
using CommandHandler = std::function<bool(CommandContext& ctx)>;

/**
 * Callback types for commands that need external functionality
 */
using RebootCallback = std::function<void()>;

/**
 * CommandDispatcher - Central command routing for device control
 *
 * Both BluetoothService and MqttService delegate to this dispatcher
 * for unified command handling. Transport-specific features (like
 * BLE chunking) are handled by the IResponseSender implementation.
 */
class CommandDispatcher {
public:
    CommandDispatcher();

    /**
     * Register a callback for the reboot command
     */
    void setRebootCallback(RebootCallback cb) { _rebootCallback = cb; }

    /**
     * Register a custom command handler.
     * Built-in handlers (ping, reboot) are registered in the constructor.
     * Use this to add project-specific handlers.
     */
    void registerHandler(const String& command, CommandHandler handler);

    /**
     * Process a command from any source
     * @param jsonCommand Raw JSON command string
     * @param sender Response sender for this request
     * @return true if command was processed successfully
     */
    bool processCommand(const String& jsonCommand, IResponseSender* sender);

    /**
     * Check if a command requires chunking (and thus is BLE-only)
     */
    bool requiresChunking(const String& command) const;

private:
    std::map<String, CommandHandler> _handlers;

    // External callbacks
    RebootCallback _rebootCallback;

    // Commands that require chunking (BLE-only)
    static const char* CHUNKED_COMMANDS[];

    // Built-in command handlers
    bool handlePing(CommandContext& ctx);
    bool handleReboot(CommandContext& ctx);

    // Helper to send error response
    void sendError(IResponseSender* sender, const String& message);
};
