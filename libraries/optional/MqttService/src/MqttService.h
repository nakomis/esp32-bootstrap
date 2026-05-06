#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "CommandDispatcher.h"

// Forward declarations
class CommandDispatcher;
struct SystemState;

/**
 * MQTT Response Sender - Implements IResponseSender for MQTT transport
 */
class MqttResponseSender : public IResponseSender {
public:
    MqttResponseSender(PubSubClient* client, const String& responseTopic);

    void sendResponse(const String& response);
    bool supportsChunking() const override { return false; }
    const char* getName() const override { return "MQTT"; }

private:
    PubSubClient* _client;
    String _responseTopic;
};

/**
 * MqttService - AWS IoT Core MQTT client for remote device control
 *
 * Uses mTLS with device certificates for secure connection.
 * Subscribes to command topic and publishes responses.
 *
 * Topics:
 *   catcam/{thingName}/commands  - Subscribe for incoming commands
 *   catcam/{thingName}/responses - Publish command responses
 *   catcam/{thingName}/status    - Publish periodic status updates
 */
class MqttService {
public:
    MqttService();
    ~MqttService();

    /**
     * Initialize MQTT client with certificates
     * @param endpoint AWS IoT endpoint (e.g., "xxx-ats.iot.us-east-1.amazonaws.com")
     * @param caCert Amazon Root CA certificate
     * @param clientCert Device certificate
     * @param privateKey Device private key
     * @param thingName IoT Thing name (used for topics and client ID)
     * @return true if initialization succeeded
     */
    bool init(const char* endpoint, const char* caCert, const char* clientCert,
              const char* privateKey, const char* thingName);

    /**
     * Set the command dispatcher for processing incoming commands
     */
    void setCommandDispatcher(CommandDispatcher* dispatcher) { _dispatcher = dispatcher; }

    /**
     * Set system state reference for status publishing
     */
    void setSystemState(SystemState* state) { _systemState = state; }

    /**
     * Call in main loop - handles reconnection and message processing
     */
    void handle();

    /**
     * Publish device status to the status topic
     */
    void publishStatus();

    /**
     * Check if connected to MQTT broker
     */
    bool isConnected() const { return _client && _client->connected(); }

    /**
     * Get connection state for status reporting
     */
    bool getConnectionState() const { return _connected; }

    /**
     * Pause MQTT connection and free SSL resources
     * Call this before memory-intensive operations like OTA updates.
     * The connection will automatically resume on next handle() call.
     */
    void pause();

    void resume();

private:
    WiFiClientSecure* _wifiClient;
    PubSubClient* _client;
    MqttResponseSender* _responseSender;
    CommandDispatcher* _dispatcher;
    SystemState* _systemState;

    String _endpoint;
    String _thingName;
    String _commandTopic;
    String _responseTopic;
    String _statusTopic;

    // Stored for resume after pause
    const char* _caCert;
    const char* _clientCert;
    const char* _privateKey;

    bool _initialized;
    bool _connected;
    unsigned long _lastReconnectAttempt;
    unsigned long _lastStatusPublish;

    static const unsigned long RECONNECT_INTERVAL_MS = 5000;
    static const unsigned long STATUS_INTERVAL_MS = 60000;
    static const int MQTT_PORT = 8883;

    void setupTopics();
    bool connect();
    void onMessage(char* topic, byte* payload, unsigned int length);

    // Static callback wrapper for PubSubClient
    static MqttService* _instance;
    static void messageCallback(char* topic, byte* payload, unsigned int length);
};
