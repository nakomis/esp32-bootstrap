#include "MqttService.h"
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
        Serial.printf("MQTT publishing to %s: %s\n", _responseTopic.c_str(), response.c_str());
        if (!_client->publish(_responseTopic.c_str(), response.c_str())) {
            Serial.printf("MQTT publish failed\n");
        } else {
            Serial.printf("MQTT published to %s: %s\n", _responseTopic.c_str(), response.c_str());
        }
    } else {
        Serial.printf("MQTT not connected, cannot send response\n");
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
    , _topicPrefix("device/")
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
    Serial.printf("Initializing MQTT service...\n");
    Serial.printf("  Endpoint: %s\n", endpoint);
    Serial.printf("  Thing: %s\n", thingName);

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
    Serial.printf("MQTT service initialized\n");
    Serial.printf("  Command topic: %s\n", _commandTopic.c_str());
    Serial.printf("  Response topic: %s\n", _responseTopic.c_str());

    return true;
}

void MqttService::setupTopics() {
    _commandTopic  = _topicPrefix + _thingName + "/commands";
    _responseTopic = _topicPrefix + _thingName + "/responses";
    _statusTopic   = _topicPrefix + _thingName + "/status";
}

bool MqttService::connect() {
    if (!_initialized || !_client) {
        return false;
    }

    Serial.printf("MQTT connecting to %s...\n", _endpoint.c_str());

    // Use thing name as client ID
    if (_client->connect(_thingName.c_str())) {
        Serial.printf("MQTT connected!\n");

        // Subscribe to command topic
        if (_client->subscribe(_commandTopic.c_str())) {
            Serial.printf("MQTT subscribed to: %s\n", _commandTopic.c_str());
        } else {
            Serial.printf("MQTT subscribe failed\n");
        }

        _connected = true;
        return true;
    } else {
        int state = _client->state();
        Serial.printf("MQTT connection failed, state: %d\n", state);
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
            Serial.printf("MQTT: WiFi disconnected\n");
        }
        return;
    }

    // Handle MQTT connection
    if (!_client->connected()) {
        if (_connected) {
            _connected = false;
            Serial.printf("MQTT connection lost\n");
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

    Serial.printf("MQTT message on %s: %s\n", topic, message.c_str());

    // Check if this is a command
    if (String(topic) == _commandTopic) {
        if (_dispatcher) {
            _dispatcher->processCommand(message, _responseSender);
        } else {
            Serial.printf("MQTT: No command dispatcher set\n");
        }
    }
}

void MqttService::pause() {
    Serial.printf("MQTT: Pausing connection to free SSL memory...\n");

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

    Serial.printf("MQTT: Paused, free heap: %d bytes\n", ESP.getFreeHeap());
}

void MqttService::resume() {
    if (_initialized) {
        return;  // Already running
    }

    if (!_caCert || !_clientCert || !_privateKey) {
        Serial.printf("MQTT: Cannot resume - no stored certificates\n");
        return;
    }

    Serial.printf("MQTT: Resuming service, free heap: %d bytes\n", ESP.getFreeHeap());

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

    Serial.printf("MQTT: Resumed, will reconnect on next handle()\n");
}

void MqttService::publishStatus() {
    if (!isConnected() || !_statusCallback) return;
    String payload = _statusCallback();
    if (!payload.isEmpty()) {
        _client->publish(_statusTopic.c_str(), payload.c_str());
    }
}
