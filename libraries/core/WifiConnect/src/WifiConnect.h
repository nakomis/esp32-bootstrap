#pragma once

#include <Arduino.h>

class WifiConnect {
public:
    static constexpr uint32_t DEFAULT_TIMEOUT_MS = 20000;

    WifiConnect();

    void begin();
    int connect();
    void disconnect();
    bool isConnected();

    bool setCredentials(const char* ssid, const char* password);
    bool hasCredentials();
    void clearCredentials();
    String getSSID();

    void setTimeout(uint32_t timeoutMs) { _timeoutMs = timeoutMs; }

private:
    uint32_t _timeoutMs;
    String _ssid;
    String _password;

    bool loadCredentials();
    bool saveCredentials();
    void syncNTP();
};
