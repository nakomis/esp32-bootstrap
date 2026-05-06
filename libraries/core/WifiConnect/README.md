# WifiConnect

Simple WiFi connection utility for ESP32-based BootBoots CatCam device.

## Purpose

WifiConnect provides a straightforward interface for establishing WiFi connections with timeout handling and status reporting. It reads WiFi credentials from a secure configuration file and manages the connection lifecycle with detailed connection information output.

## Features

- **Simple API**: Single method to connect to WiFi
- **Timeout Protection**: 20-second timeout prevents indefinite blocking
- **Connection Status**: Returns success/failure codes
- **Detailed Output**: Displays IP address, MAC, SSID, and signal strength
- **Already-Connected Detection**: Skips connection if already connected
- **Automatic Disconnect**: Cleans up on timeout

## Usage

### Basic Connection

```cpp
#include <WifiConnect.h>

WifiConnect wifi;

void setup() {
    Serial.begin(115200);

    int result = wifi.connect();

    if (result == 0) {
        // Serial.println("WiFi connection successful!");
    } else {
        // Serial.println("WiFi connection failed!");
    }
}
```

### With Error Handling

```cpp
WifiConnect wifi;

void setup() {
    Serial.begin(115200);

    int attempts = 0;
    int maxAttempts = 3;

    while (attempts < maxAttempts) {
        int result = wifi.connect();

        if (result == 0) {
            // Serial.println("Successfully connected to WiFi");
            break;
        }

        attempts++;
        // Serial.printf("Connection attempt %d of %d failed\n", attempts, maxAttempts);

        if (attempts < maxAttempts) {
            // Serial.println("Retrying in 5 seconds...");
            delay(5000);
        }
    }

    if (WiFi.status() != WL_CONNECTED) {
        // Serial.println("Failed to connect after multiple attempts");
        // Serial.println("Continuing in offline mode...");
    }
}
```

### Quick Status Check

```cpp
if (WiFi.status() == WL_CONNECTED) {
    // Serial.println("Already connected");
} else {
    wifi.connect();
}
```

## Return Values

The `connect()` method returns an integer status code:

- `0` - **Success**: WiFi connected successfully or already connected
- `1` - **Timeout**: Connection timeout after 20 seconds

## Configuration

### Credentials File

WifiConnect reads credentials from `include/secrets.h`:

```cpp
// include/secrets.h
#ifndef SECRETS_H
#define SECRETS_H

#define WIFI_SSID "YourNetworkName"
#define WIFI_PASSWORD "YourNetworkPassword"

#endif
```

### Template File

A template is typically provided at `include/secrets.h.template`:

```cpp
// include/secrets.h.template
#ifndef SECRETS_H
#define SECRETS_H

#define WIFI_SSID "your_ssid_here"
#define WIFI_PASSWORD "your_password_here"

// Add other secrets as needed:
// #define AWS_ACCESS_KEY "your_key"
// #define AWS_SECRET_KEY "your_secret"

#endif
```

**Setup Instructions**:
1. Copy `secrets.h.template` to `secrets.h`
2. Update with your actual credentials
3. Add `secrets.h` to `.gitignore` to prevent committing credentials

## Connection Output

During connection, WifiConnect provides detailed status information:

```
Connecting to Wi-Fi....................
Wi-Fi Connected.

IP Address: 192.168.1.100
MAC Address: AA:BB:CC:DD:EE:FF
SSID: MyHomeNetwork
Signal Strength: -45
```

### Output Details

- **IP Address**: Device's assigned IP address on the network
- **MAC Address**: Device's hardware MAC address
- **SSID**: Connected network name
- **Signal Strength**: RSSI value in dBm (lower negative numbers = stronger signal)

### Signal Strength Guide

RSSI values interpretation:
- `-30 to -50 dBm`: Excellent signal
- `-50 to -60 dBm`: Good signal
- `-60 to -70 dBm`: Fair signal
- `-70 to -80 dBm`: Weak signal
- `-80+ dBm`: Very weak signal

## Timeout Behavior

The connection timeout is set to 20 seconds:

```cpp
const unsigned long WIFI_CONNECT_TIMEOUT_MILLIS = 20 * 1000;
```

If connection is not established within 20 seconds:
1. Timeout message is printed: `"Bailing out of WiFi Connect"`
2. WiFi is disconnected: `WiFi.disconnect()`
3. Function returns `1` (failure code)

## Integration Examples

### With SDLogger

```cpp
#include <WifiConnect.h>
#include <SDLogger.h>

WifiConnect wifi;

void setup() {
    SDLogger::getInstance().init();

    SDLogger::getInstance().info("Attempting WiFi connection...");
    int result = wifi.connect();

    if (result == 0) {
        SDLogger::getInstance().infof("WiFi connected: %s",
                                      WiFi.localIP().toString().c_str());
    } else {
        SDLogger::getInstance().error("WiFi connection failed");
    }
}
```

### With OTA Updates

```cpp
#include <WifiConnect.h>
#include <OTAUpdate.h>

WifiConnect wifi;
OTAUpdate ota;

void setup() {
    // Connect to WiFi first
    if (wifi.connect() == 0) {
        // Initialize OTA updates (requires WiFi)
        ota.init("BootBoots-CatCam", "ota_password");
        // Serial.println("OTA updates enabled");
    } else {
        // Serial.println("OTA updates disabled - no WiFi");
    }
}

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        ota.handle();
    } else {
        // Reconnect if disconnected
        wifi.connect();
    }
}
```

### With Connection Monitoring

```cpp
WifiConnect wifi;
unsigned long lastCheck = 0;
const unsigned long CHECK_INTERVAL = 30000;  // 30 seconds

void loop() {
    if (millis() - lastCheck > CHECK_INTERVAL) {
        if (WiFi.status() != WL_CONNECTED) {
            // Serial.println("WiFi disconnected, reconnecting...");
            wifi.connect();
        }
        lastCheck = millis();
    }

    // ... rest of loop
}
```

## Dependencies

### External
- `WiFi.h` - ESP32 WiFi library (Arduino core)

### Internal
- `secrets.h` - WiFi credentials (user-provided)

## WiFi Mode

WifiConnect sets the WiFi mode to Station (STA):
```cpp
WiFi.mode(WIFI_STA);
```

This mode connects the ESP32 to an existing WiFi network (as opposed to AP mode which creates a network).

## Best Practices

### 1. Initialize Serial First
```cpp
void setup() {
    Serial.begin(115200);  // Initialize Serial before WifiConnect
    delay(100);            // Brief delay for Serial to stabilize

    WifiConnect wifi;
    wifi.connect();
}
```

### 2. Check Connection Status
```cpp
// Before attempting WiFi operations
if (WiFi.status() == WL_CONNECTED) {
    // Safe to use WiFi
}
```

### 3. Handle Failures Gracefully
```cpp
if (wifi.connect() != 0) {
    // Continue in offline mode
    // or implement retry logic
    // or enter deep sleep and retry later
}
```

### 4. Secure Your Credentials
```bash
# Add to .gitignore
echo "include/secrets.h" >> .gitignore
```

### 5. Use Connection Monitoring
```cpp
// Periodically check and reconnect if needed
void checkWiFi() {
    static WifiConnect wifi;

    if (WiFi.status() != WL_CONNECTED) {
        wifi.connect();
    }
}
```

## Troubleshooting

### Connection Always Fails

**Possible causes**:
1. **Wrong credentials**: Double-check SSID and password in `secrets.h`
2. **Out of range**: Move device closer to router
3. **2.4GHz vs 5GHz**: ESP32 only supports 2.4GHz networks
4. **Router issues**: Restart your router
5. **MAC filtering**: Add device MAC to router's whitelist

### Connection Timeout

**Solutions**:
1. Check router settings for max clients
2. Verify network is not hidden (or configure for hidden networks)
3. Check for special characters in SSID/password
4. Increase timeout value if needed

### Weak Signal

**Solutions**:
1. Move device closer to router
2. Use external antenna if supported
3. Reduce interference (microwave, other devices)
4. Use WiFi extender or mesh network

### Connection Drops

**Solutions**:
1. Implement connection monitoring
2. Check power supply stability
3. Verify router firmware is up to date
4. Check for WiFi channel congestion

## Advanced Usage

### Custom Timeout

Modify the timeout constant:
```cpp
// In WifiConnect.cpp
const unsigned long WIFI_CONNECT_TIMEOUT_MILLIS = 30 * 1000;  // 30 seconds
```

### Hidden Network Support

For hidden networks, explicitly configure:
```cpp
// Would need to modify WifiConnect to support this:
WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 0, NULL, true);  // true = hidden
```

### Static IP Configuration

Add static IP support:
```cpp
// Before WiFi.begin():
IPAddress local_IP(192, 168, 1, 100);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);

WiFi.config(local_IP, gateway, subnet, primaryDNS);
```

## Limitations

- Only supports WPA/WPA2 networks
- No support for enterprise WiFi (WPA2-Enterprise)
- No support for captive portals
- Single network only (no failover to alternate networks)
- No connection quality monitoring
- 2.4GHz only (ESP32 hardware limitation)

## Future Enhancements

Potential improvements:
- Multiple network support with priority
- Connection quality metrics
- Automatic reconnection logic
- WiFi manager for runtime configuration
- Support for hidden networks
- Static IP configuration option
- Connection event callbacks
- Network scanning and selection

## API Reference

### WifiConnect Class

#### Constructor
```cpp
WifiConnect()
```
Creates a new WifiConnect instance.

#### Methods
```cpp
int connect()
```
Attempts to connect to WiFi using credentials from `secrets.h`.

**Returns**:
- `0`: Success (connected or already connected)
- `1`: Failure (timeout after 20 seconds)

**Behavior**:
- Returns immediately if already connected
- Sets WiFi mode to STA
- Attempts connection for up to 20 seconds
- Prints connection status to Serial
- Displays connection details on success
- Disconnects and returns on timeout

## Security Considerations

### Credential Protection

- Never commit `secrets.h` to version control
- Use `.gitignore` to exclude credentials
- Use environment-specific configuration files
- Consider encrypted credential storage for production

### Network Security

- Use WPA2 or better on your router
- Change default router passwords
- Use strong WiFi passwords
- Enable router firewall
- Regularly update router firmware

## License

Part of the BootBoots CatCam project.
