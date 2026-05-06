#include "MqttService.h"
#include "SystemState.h"
#include "SDLogger.h"
#include <WiFi.h>

// Static instance for callback routing
MqttService* MqttService::_instance = nullptr;

// ============================================================================
// MqttResponseSender Implementation
// ============================================================================

MqttResponseSender::MqttResponseSender(PubSubClient* client, const String& responseTopic)
    : _client(client)
    , _responseTopic(responseTopic)
{
}

void MqttResponseSender::sendResponse(const String& response) {
    if (_client && _client->connected()) {
        SDLogger::getInstance().debugf("MQTT publishing to %s: %s",
                                        _responseTopic.c_str(), response.c_str());
        if (!_client->publish(_responseTopic.c_str(), response.c_str())) {
            SDLogger::getInstance().debugf("MQTT publish failed");
        }
        SDLogger::getInstance().debugf("MQTT published to %s: %s",
                                        _responseTopic.c_str(), response.c_str());
    } else {
        SDLogger::getInstance().warnf("MQTT not connected, cannot send response");
    }
}

// ============================================================================
// MqttService Implementation
// ============================================================================

MqttService::MqttService()
    : _wifiClient(nullptr)
    , _client(nullptr)
    , _responseSender(nullptr)
    , _dispatcher(nullptr)
    , _systemState(nullptr)
    , _initialized(false)
    , _connected(false)
    , _lastReconnectAttempt(0)
    , _lastStatusPublish(0)
    , _caCert(nullptr)
    , _clientCert(nullptr)
    , _privateKey(nullptr)
{
    _instance = this;
}

MqttService::~MqttService() {
    if (_client) {
        _client->disconnect();
        delete _client;
    }
    delete _wifiClient;
    delete _responseSender;
    _instance = nullptr;
}

void MqttService::messageCallback(char* topic, byte* payload, unsigned int length) {
    if (_instance) {
        _instance->onMessage(topic, payload, length);
    }
}

bool MqttService::init(const char* endpoint, const char* caCert, const char* clientCert,
                       const char* privateKey, const char* thingName) {
    SDLogger::getInstance().infof("Initializing MQTT service...");
    SDLogger::getInstance().infof("  Endpoint: %s", endpoint);
    SDLogger::getInstance().infof("  Thing: %s", thingName);

    _endpoint = endpoint;
    _thingName = thingName;
    _caCert = caCert;
    _clientCert = clientCert;
    _privateKey = privateKey;
    setupTopics();

    // Create secure WiFi client
    _wifiClient = new WiFiClientSecure();
    _wifiClient->setCACert(caCert);
    _wifiClient->setCertificate(clientCert);
    _wifiClient->setPrivateKey(privateKey);

    // Create MQTT client
    _client = new PubSubClient(*_wifiClient);
    _client->setServer(endpoint, MQTT_PORT);
    _client->setCallback(messageCallback);
    _client->setBufferSize(2048);  // Large buffer for OTA URLs (S3 signed URLs can be ~700 bytes)

    // Create response sender
    _responseSender = new MqttResponseSender(_client, _responseTopic);

    _initialized = true;
    SDLogger::getInstance().infof("MQTT service initialized");
    SDLogger::getInstance().infof("  Command topic: %s", _commandTopic.c_str());
    SDLogger::getInstance().infof("  Response topic: %s", _responseTopic.c_str());

    return true;
}

void MqttService::setupTopics() {
    _commandTopic = "bootboots/" + _thingName + "/commands";
    _responseTopic = "bootboots/" + _thingName + "/responses";
    _statusTopic = "bootboots/" + _thingName + "/status";
}

bool MqttService::connect() {
    if (!_initialized || !_client) {
        return false;
    }

    SDLogger::getInstance().infof("MQTT connecting to %s...", _endpoint.c_str());

    // Use thing name as client ID
    if (_client->connect(_thingName.c_str())) {
        SDLogger::getInstance().infof("MQTT connected!");

        // Subscribe to command topic
        if (_client->subscribe(_commandTopic.c_str())) {
            SDLogger::getInstance().infof("MQTT subscribed to: %s", _commandTopic.c_str());
        } else {
            SDLogger::getInstance().errorf("MQTT subscribe failed");
        }

        _connected = true;
        return true;
    } else {
        int state = _client->state();
        SDLogger::getInstance().warnf("MQTT connection failed, state: %d", state);
        // PubSubClient states:
        // -4: MQTT_CONNECTION_TIMEOUT
        // -3: MQTT_CONNECTION_LOST
        // -2: MQTT_CONNECT_FAILED
        // -1: MQTT_DISCONNECTED
        //  0: MQTT_CONNECTED
        //  1: MQTT_CONNECT_BAD_PROTOCOL
        //  2: MQTT_CONNECT_BAD_CLIENT_ID
        //  3: MQTT_CONNECT_UNAVAILABLE
        //  4: MQTT_CONNECT_BAD_CREDENTIALS
        //  5: MQTT_CONNECT_UNAUTHORIZED
        _connected = false;
        return false;
    }
}

void MqttService::handle() {
    if (!_initialized) {
        return;
    }

    // Check WiFi first
    if (WiFi.status() != WL_CONNECTED) {
        if (_connected) {
            _connected = false;
            SDLogger::getInstance().warnf("MQTT: WiFi disconnected");
        }
        return;
    }

    // Handle MQTT connection
    if (!_client->connected()) {
        if (_connected) {
            _connected = false;
            SDLogger::getInstance().warnf("MQTT connection lost");
        }

        // Non-blocking reconnection
        unsigned long now = millis();
        if (now - _lastReconnectAttempt >= RECONNECT_INTERVAL_MS) {
            _lastReconnectAttempt = now;
            connect();
        }
    } else {
        // Process incoming messages
        _client->loop();

        // Periodic status publish
        unsigned long now = millis();
        if (now - _lastStatusPublish >= STATUS_INTERVAL_MS) {
            _lastStatusPublish = now;
            publishStatus();
        }
    }
}

void MqttService::onMessage(char* topic, byte* payload, unsigned int length) {
    // Convert payload to string
    String message;
    message.reserve(length + 1);
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    SDLogger::getInstance().infof("MQTT message on %s: %s", topic, message.c_str());

    // Check if this is a command
    if (String(topic) == _commandTopic) {
        if (_dispatcher) {
            _dispatcher->processCommand(message, _responseSender);
        } else {
            SDLogger::getInstance().warnf("MQTT: No command dispatcher set");
        }
    }
}

void MqttService::pause() {
    SDLogger::getInstance().infof("MQTT: Pausing connection to free SSL memory...");

    if (_client) {
        _client->disconnect();
        delete _client;
        _client = nullptr;
    }

    if (_wifiClient) {
        _wifiClient->stop();
        delete _wifiClient;
        _wifiClient = nullptr;
    }

    if (_responseSender) {
        delete _responseSender;
        _responseSender = nullptr;
    }

    _connected = false;
    _initialized = false;  // Will need to reinit on resume

    SDLogger::getInstance().infof("MQTT: Paused, free heap: %d bytes", ESP.getFreeHeap());
}

void MqttService::resume() {
    if (_initialized) {
        return;  // Already running
    }

    if (!_caCert || !_clientCert || !_privateKey) {
        SDLogger::getInstance().errorf("MQTT: Cannot resume - no stored certificates");
        return;
    }

    SDLogger::getInstance().infof("MQTT: Resuming service, free heap: %d bytes", ESP.getFreeHeap());

    // Recreate secure WiFi client with stored certificates
    _wifiClient = new WiFiClientSecure();
    _wifiClient->setCACert(_caCert);
    _wifiClient->setCertificate(_clientCert);
    _wifiClient->setPrivateKey(_privateKey);

    // Recreate MQTT client
    _client = new PubSubClient(*_wifiClient);
    _client->setServer(_endpoint.c_str(), MQTT_PORT);
    _client->setCallback(messageCallback);
    _client->setBufferSize(2048);

    // Recreate response sender
    _responseSender = new MqttResponseSender(_client, _responseTopic);

    _initialized = true;
    _lastReconnectAttempt = 0;  // Allow immediate reconnect

    SDLogger::getInstance().infof("MQTT: Resumed, will reconnect on next handle()");
}

void MqttService::publishStatus() {
    if (!_client || !_client->connected() || !_systemState) {
        return;
    }

    DynamicJsonDocument doc(512);
    unsigned long uptime = millis() - _systemState->systemStartTime;

    doc["device"] = "BootBoots-CatCam";
    doc["timestamp"] = millis();
    doc["uptime_seconds"] = uptime / 1000;
    doc["wifi_connected"] = _systemState->wifiConnected;
    doc["camera_ready"] = _systemState->cameraReady;
    doc["training_mode"] = _systemState->trainingMode;
    doc["total_detections"] = _systemState->totalDetections;

    String statusJson;
    serializeJson(doc, statusJson);

    if (_client->publish(_statusTopic.c_str(), statusJson.c_str())) {
        SDLogger::getInstance().tracef("MQTT status published");
    } else {
        SDLogger::getInstance().warnf("MQTT status publish failed");
    }
}
