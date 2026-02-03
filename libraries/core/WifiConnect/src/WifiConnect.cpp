#include "WifiConnect.h"
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>

static const char* TAG = "WiFi";
static const char* NVS_NAMESPACE = "wifi";

WifiConnect::WifiConnect()
    : _timeoutMs(DEFAULT_TIMEOUT_MS)
{
}

void WifiConnect::begin() {
    loadCredentials();
    WiFi.mode(WIFI_STA);
    log_i("WiFi initialized (SSID: %s)", _ssid.isEmpty() ? "<not set>" : _ssid.c_str());
}

int WifiConnect::connect() {
    if (WiFi.status() == WL_CONNECTED) {
        return 0;
    }

    if (!hasCredentials()) {
        log_w("No WiFi credentials configured");
        return 1;
    }

    log_i("Connecting to WiFi '%s'...", _ssid.c_str());
    WiFi.begin(_ssid.c_str(), _password.c_str());

    unsigned long startMillis = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startMillis > _timeoutMs) {
            log_w("WiFi connection timeout");
            WiFi.disconnect();
            return 1;
        }
        delay(100);
    }

    log_i("WiFi connected - IP: %s, RSSI: %d dBm",
          WiFi.localIP().toString().c_str(), WiFi.RSSI());

    // Sync time with NTP server
    syncNTP();

    return 0;
}

void WifiConnect::disconnect() {
    WiFi.disconnect();
    log_i("WiFi disconnected");
}

bool WifiConnect::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool WifiConnect::hasCredentials() {
    return !_ssid.isEmpty() && !_password.isEmpty();
}

String WifiConnect::getSSID() {
    return _ssid;
}

bool WifiConnect::setCredentials(const char* ssid, const char* password) {
    if (!ssid || !password || strlen(ssid) == 0) {
        log_w("Invalid WiFi credentials");
        return false;
    }

    _ssid = String(ssid);
    _password = String(password);

    if (!saveCredentials()) {
        log_e("Failed to save WiFi credentials");
        return false;
    }

    log_i("WiFi credentials saved for '%s'", ssid);
    return true;
}

void WifiConnect::clearCredentials() {
    _ssid = "";
    _password = "";

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();

    log_i("WiFi credentials cleared");
}

bool WifiConnect::loadCredentials() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {
        return false;
    }

    _ssid = prefs.getString("ssid", "");
    _password = prefs.getString("password", "");
    prefs.end();

    return hasCredentials();
}

bool WifiConnect::saveCredentials() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        return false;
    }

    prefs.putString("ssid", _ssid);
    prefs.putString("password", _password);
    prefs.end();

    return true;
}

void WifiConnect::syncNTP() {
    log_i("Syncing time with NTP server...");

    // Configure time with NTP servers (timezone offset = 0 for UTC)
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    // Wait for time to be set (up to 10 seconds)
    time_t now = 0;
    int attempts = 0;
    const int maxAttempts = 20;  // 20 * 500ms = 10 seconds

    while (now < 1700000000 && attempts < maxAttempts) {  // Wait until time is reasonable (after Nov 2023)
        delay(500);
        time(&now);
        attempts++;
    }

    if (now >= 1700000000) {
        struct tm* timeinfo = localtime(&now);
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
        log_i("NTP time synced: %s UTC", buffer);
    } else {
        log_w("NTP time sync failed after %d attempts", attempts);
    }
}
