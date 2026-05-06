# BluetoothOTA

Bluetooth Low Energy (BLE) interface for triggering and monitoring over-the-air (OTA) firmware updates on the BootBoots CatCam device.

## Purpose

BluetoothOTA provides a BLE-based wrapper around the OTAUpdate service, allowing mobile applications to remotely trigger firmware updates via Bluetooth. This enables wireless firmware updates without requiring the device to be on the same WiFi network as the update server.

## Features

- **BLE Command Interface**: Accepts JSON-formatted commands via BLE characteristic writes
- **Status Notifications**: Sends real-time update progress and status to connected clients
- **Firmware URL Support**: Downloads firmware from HTTP/HTTPS URLs (e.g., S3 signed URLs)
- **Update Management**: Start, cancel, and monitor firmware updates
- **BLE Server Sharing**: Shares BLE server with BluetoothService (ESP32 supports only one server)
- **Initial Status Value**: Sets initial status on characteristic for immediate version display

## BLE Service Specification

### Service UUID
```
99db6ea6-27e4-434d-aafd-795cf95feb06
```

### Characteristics

#### Command Characteristic (Write)
**UUID**: `1ac886a6-5fff-41ea-9b11-25a7dcb93a7e`

Accepts JSON commands:
```json
{
  "action": "ota_update",
  "firmware_url": "https://example.com/firmware.bin",
  "version": "1.2.3"
}
```

**Supported Actions**:
- `ota_update` - Start firmware update from URL
- `get_status` - Request current update status
- `cancel_update` - Cancel in-progress update

#### Status Characteristic (Read/Notify)
**UUID**: `5f5979f3-f1a6-4ce7-8360-e249c2e9333d`

Returns JSON status:
```json
{
  "status": "downloading",
  "message": "Downloading firmware...",
  "progress": 45,
  "version": "1.0.0"
}
```

**Status Values**:
- `ready` - Service initialized and ready
- `connected` - Client connected
- `starting` - Update initialization
- `downloading` - Firmware download in progress
- `error` - Update failed
- `cancelled` - Update cancelled by user

## Usage

### Initialization (Shared BLE Server - Recommended)

```cpp
#include <BluetoothOTA.h>
#include <BluetoothService.h>
#include <OTAUpdate.h>

BluetoothOTA* bluetoothOTA;
BootBootsBluetoothService* bluetoothService;
OTAUpdate* otaUpdate;

void setup() {
    // Initialize main Bluetooth service first (creates BLE server)
    bluetoothService = new BootBootsBluetoothService();
    bluetoothService->init("BootBoots-CatCam");

    // Initialize OTA update service
    otaUpdate = new OTAUpdate();
    otaUpdate->init("BootBoots-CatCam");

    // Initialize Bluetooth OTA using the SHARED BLE server
    bluetoothOTA = new BluetoothOTA();
    if (bluetoothOTA->initWithExistingServer(bluetoothService->getServer())) {
        bluetoothOTA->setOTAUpdate(otaUpdate);
        // Serial.println("Bluetooth OTA initialized with shared BLE server");
    }
}

void loop() {
    if (bluetoothOTA) {
        bluetoothOTA->handle();
    }
    otaUpdate->handle();
}
```

### Standalone Initialization (Not Recommended)

```cpp
#include <BluetoothOTA.h>
#include <OTAUpdate.h>

BluetoothOTA bleOTA;
OTAUpdate otaUpdate;

void setup() {
    // Initialize OTA update service first
    otaUpdate.init("BootBoots-CatCam");

    // Initialize Bluetooth OTA with its own BLE server
    // NOTE: Cannot be used if BluetoothService is also active
    bleOTA.init("BootBoots-CatCam");
    bleOTA.setOTAUpdate(&otaUpdate);
}

void loop() {
    bleOTA.handle();
    otaUpdate.handle();
}
```

### Check Connection Status

```cpp
if (bleOTA.isConnected()) {
    // Client is connected
}
```

### Send Status Updates

```cpp
bleOTA.sendStatusUpdate("downloading", "Firmware download at 50%", 50);
```

## Dependencies

### Internal
- **OTAUpdate**: Core firmware update implementation (required)
- **SDLogger**: Logging functionality

### External
- ESP32 BLE Arduino library (`BLEDevice.h`, `BLEServer.h`, `BLEUtils.h`)
- ArduinoJson (for command/status parsing)

## Relationship to Other Components

```
Mobile App (BLE Client)
        ↓
  BluetoothOTA ──→ OTAUpdate.downloadToSD() ──→ SD Card
        ↓                                            ↓
  Status Updates                            Device Reboots
                                                     ↓
                                       OTAUpdate.flashFromSD()
                                                     ↓
                                              ESP32 Update Module
```

**BluetoothOTA** acts as a bridge between BLE clients and the **OTAUpdate** service. It does not perform firmware updates directly; instead, it:

1. Receives OTA commands via BLE from web interface
2. Calls `OTAUpdate::downloadToSD()` to download firmware to SD card (Stage 1)
3. Relays status updates back to client
4. Device automatically reboots after download
5. `OTAUpdate::flashFromSD()` runs on next boot (Stage 2)

This two-stage approach allows OTA updates to work with BLE active and limited memory.

## BLE Server Sharing

ESP32 can only have **ONE BLE server** instance active at a time. If your application uses both BluetoothService and BluetoothOTA, they must share the same BLE server.

### Why Server Sharing?

Attempting to create multiple BLE servers will cause crashes:
```cpp
// ❌ WRONG - Creates two servers (will crash)
bluetoothService->init("BootBoots-CatCam");  // Creates BLE server
bluetoothOTA->init("BootBoots-CatCam");       // Tries to create another server - CRASH!
```

### Correct Usage with Shared Server

```cpp
// ✅ CORRECT - Shares one server
bluetoothService->init("BootBoots-CatCam");                           // Creates BLE server
bluetoothOTA->initWithExistingServer(bluetoothService->getServer()); // Uses existing server
```

### How It Works

1. **BluetoothService** creates and owns the primary BLE server
2. **BluetoothService** exposes the server via `getServer()`
3. **BluetoothOTA** uses `initWithExistingServer()` to add its service to the existing server
4. Both services advertise their UUIDs on the same server
5. Clients can connect and access both services simultaneously

### Implementation Details

When using `initWithExistingServer()`:
- BluetoothOTA does NOT set server callbacks (BluetoothService manages them)
- BluetoothOTA creates its own service and characteristics on the shared server
- Initial status value is set on the status characteristic for immediate reads
- Service UUID is added to advertising

## Implementation Notes

- Device name defaults to "BootBoots-CatCam"
- Requires WiFi connection for HTTP firmware downloads
- OTAUpdate must be initialized before BluetoothOTA can function
- All status messages are logged via SDLogger
- **BLE Server Sharing**: Use `initWithExistingServer()` when using with BluetoothService
- **Initial Status**: Status characteristic has initial value set for immediate version display
- **No Early Status Updates**: sendStatusUpdate() not called during initialization to prevent crashes

## Security Considerations

- No authentication required for BLE connection (consider adding in production)
- Firmware URLs should use HTTPS with signed URLs for security
- OTAUpdate supports password protection for network-based OTA

## API Reference

### BluetoothOTA Class

#### Methods

- `bool init(const char* deviceName)` - Initialize BLE service with its own server (standalone mode)
- `bool initWithExistingServer(BLEServer* pServer)` - Initialize using shared BLE server (recommended)
- `void handle()` - Process BLE events (call in loop)
- `bool isConnected()` - Check if client is connected
- `void sendStatusUpdate(const String& status, const String& message, int progress)` - Send status to client
- `void handleOTACommand(const String& commandJson)` - Process OTA command
- `void setOTAUpdate(OTAUpdate* otaUpdate)` - Link to OTAUpdate instance

### Callbacks

Internal server and characteristic callbacks are handled automatically.
