#include "WiFi.h"
#include "WifiConnect.h"
#include "../../../include/secrets.h"

const unsigned long WIFI_CONNECT_TIMEOUT_MILLIS = 20 * 1000;

WifiConnect::WifiConnect()
{
}

int WifiConnect::connect()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        return 0;
    }
    
    Serial.print("Connecting to Wi-Fi.");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long startMillis = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        if (millis() - startMillis > WIFI_CONNECT_TIMEOUT_MILLIS)
        {
            // Serial.println();
            // Serial.println("Bailing out of WiFi Connect");
            WiFi.disconnect();
            return 1;
        }
        Serial.print(".");
        delay(100);
    }
    // Serial.println("\nWi-Fi Connected.\n");
    Serial.print("IP Address: ");
    // Serial.println(WiFi.localIP());
    Serial.print("MAC Address: ");
    // Serial.println(WiFi.macAddress());
    Serial.print("SSID: ");
    // Serial.println(WiFi.SSID());
    Serial.print("Signal Strength: ");
    // Serial.println(WiFi.RSSI()); 
    return 0;
}