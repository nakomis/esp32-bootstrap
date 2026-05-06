# OTAUpdate

Over-the-Air (OTA) firmware update service for ESP32-based BootBoots CatCam device.

## Purpose

OTAUpdate provides comprehensive firmware update capabilities for ESP32 devices, supporting both local network-based updates (ArduinoOTA) and remote HTTP URL-based updates. Due to ESP32 memory constraints with BLE active, this library implements a **two-stage OTA architecture** that separates firmware download from flash operations.

## Features

- **Two-Stage OTA Architecture**: Memory-efficient OTA that works with BLE active
  - Stage 1: Download firmware to SD card
  - Stage 2: Flash from SD card on boot (before BLE starts)
- **Multiple Update Methods**:
  - Two-stage SD card-based updates (HTTP/HTTPS from S3)
  - ArduinoOTA for local network updates (WiFi)
  - Legacy direct HTTP updates (requires BLE disabled)
- **Progress Tracking**: Real-time progress reporting (0-100%)
- **Status Monitoring**: Detailed status messages throughout update process
- **Callback System**: Custom callbacks for update events
- **Error Handling**: Comprehensive error detection and automatic recovery
- **Security**: Optional password protection for network updates
- **Persistent State**: NVS-based flags for reliable multi-boot operations
- **mDNS Discovery**: Device discovery via `hostname.local`

## Two-Stage OTA Architecture

### Why Two-Stage?

ESP32 with BLE active has only ~38KB free heap. Direct HTTP OTA requires:
- HTTP client: ~10KB
- Update library: ~8KB
- Download buffer: ~1KB
- **Total: ~19KB minimum**, leaving insufficient memory for reliable operations

**Solution**: Separate memory-intensive operations across two boot cycles.

### Stage 1: Download Phase (Triggered by Web Interface)

1. Disable SD file logging to free ~2-3KB memory
2. Download firmware from HTTP URL to SD card (`/firmware_update.bin`)
3. Use 512-byte buffer to minimize memory usage
4. Write firmware to SD card in chunks
5. Set NVS flag `pending_ota=true` with firmware size
6. **Auto-reboot device**

### Stage 2: Flash Phase (Early in Boot, BEFORE BLE Starts)

1. Check NVS flag in `main.cpp` setup() **BEFORE** any peripheral initialization
2. If flag set, manually initialize SD_MMC (without SDLogger)
3. **Clear NVS flag IMMEDIATELY** (prevents boot loops on failure)
4. Read firmware from SD card
5. Flash to device using Update library (512-byte buffer)
6. Delete firmware file from SD card
7. **Auto-reboot with new firmware**
8. On normal boot (no pending OTA), initialize SDLogger normally

### Benefits

- Separates memory-intensive operations (HTTP download vs. flash)
- HTTP download happens with BLE active (no connectivity loss on failure)
- Flash happens early in boot with maximum available heap (~200KB+)
- Automatic recovery on failure (device reboots normally)
- No need for BLE deinitialization (which causes crashes)

## Usage

### Basic Initialization

```cpp
#include <OTAUpdate.h>

OTAUpdate otaUpdate;

void setup() {
    // Connect to WiFi first
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }

    // Initialize OTA with hostname and optional password
    otaUpdate.init("BootBoots-CatCam", "secure_password");
}

void loop() {
    // Call in main loop to handle OTA events
    otaUpdate.handle();
}
```

### Two-Stage OTA Update (Recommended)

```cpp
// Set callback to handle Stage 1 completion
otaUpdate.setUpdateCallback([](bool success, const char* error) {
    if (success) {
        // Download complete, device will reboot to flash
        // Serial.println("Download complete, rebooting to flash...");
    } else {
        // Serial.printf("Download failed: %s\n", error);
    }
});

// Start two-stage update from URL (e.g., S3 signed URL)
const char* firmwareURL = "http://example.com/firmware.bin";
bool started = otaUpdate.downloadToSD(firmwareURL);

if (started) {
    // Serial.println("Firmware download started...");
    // Device will automatically reboot after download completes
    // Flash happens on next boot
}
```

### Check for Pending OTA on Boot

```cpp
void setup() {
    Serial.begin(115200);

    // Check for pending OTA BEFORE initializing any peripherals
    if (OTAUpdate::hasPendingUpdate()) {
        // Serial.println("Pending OTA detected, initializing SD for flash...");

        // Manually initialize SD_MMC
        pinMode(14, PULLUP); pinMode(15, PULLUP); pinMode(2, PULLUP);
        pinMode(4, PULLUP); pinMode(12, PULLUP); pinMode(13, PULLUP);

        if (!SD_MMC.begin()) {
            // Serial.println("ERROR: Failed to initialize SD_MMC");
        } else {
            // Serial.println("SD_MMC initialized, flashing firmware...");
            if (!OTAUpdate::flashFromSD()) {
                // Serial.println("ERROR: Flash failed, continuing normal boot");
            }
            // If flash succeeds, device reboots automatically
        }
    }

    // Normal initialization continues here
    SDLogger::getInstance().init("/logs");
    // ... rest of setup
}
```

### Legacy Direct HTTP Update (Requires More Memory)

```cpp
// Only use if you have disabled BLE or have sufficient memory
bool started = otaUpdate.updateFromURL(firmwareURL);
```

### Network-Based OTA (ArduinoOTA)

```cpp
// ArduinoOTA is automatically enabled during init()
// Update using Arduino IDE or platformio:
// platformio run -t upload --upload-port BootBoots-CatCam.local
```

## API Reference

### OTAUpdate Class

#### Two-Stage OTA Methods

- `bool downloadToSD(const char* firmwareURL)` - Start Stage 1: Download to SD card
  - Downloads firmware from URL to `/firmware_update.bin`
  - Sets NVS flag for Stage 2
  - Auto-reboots on completion
  - Returns `true` if download started successfully

- `static bool flashFromSD()` - Execute Stage 2: Flash from SD card
  - Reads firmware from `/firmware_update.bin`
  - Clears NVS flag before flashing (prevents boot loops)
  - Auto-reboots on success
  - Returns `true` if flash successful
  - **Must be called before SDLogger initialization**

- `static bool hasPendingUpdate()` - Check if Stage 2 pending
  - Checks NVS flag `pending_ota`
  - Returns `true` if firmware ready to flash
  - Call in `setup()` before peripheral initialization

#### Legacy Methods

- `bool updateFromURL(const char* firmwareURL)` - Direct HTTP update (legacy)
  - Downloads and flashes in one operation
  - Requires more memory (~19KB minimum)
  - May fail with BLE active

#### Network OTA Methods

- `void init(const char* hostname, const char* password)` - Initialize service
- `void handle()` - Process OTA events (call in loop)
- `bool isUpdating()` - Check if update in progress
- `void setUpdateCallback(void (*callback)(bool, const char*))` - Set callback
- `void setPassword(const char* password)` - Update password
- `void enableSecureMode(bool enable)` - Enable/disable secure mode
- `String getStatus()` - Get current status message
- `int getProgress()` - Get progress percentage (0-100)
- `void cancelUpdate()` - Cancel HTTP update

## NVS Storage

The two-stage OTA uses NVS (Non-Volatile Storage) for persistent state:

**Namespace**: `"ota"`

**Keys**:
- `pending` (bool) - True if firmware downloaded and ready to flash
- `size` (uint32_t) - Expected firmware size in bytes

**Usage**:
```cpp
Preferences prefs;
prefs.begin("ota", true);  // read-only
bool pending = prefs.getBool("pending", false);
size_t size = prefs.getUInt("size", 0);
prefs.end();
```

## Update Process Flow

### Two-Stage OTA Flow

```
[Web Interface Triggers Update]
         |
         v
    downloadToSD()
         |
         v
  [Download to SD card]
  [Set NVS flag]
         |
         v
    [Reboot]
         |
         v
   setup() checks hasPendingUpdate()
         |
         v
  [Clear NVS flag]
  [Flash from SD]
         |
         v
    [Reboot]
         |
         v
  [New firmware running]
```

### Progress Stages

**Stage 1 (Download)**:
- `"Starting two-stage OTA..."` - Beginning
- `"Using HTTP connection"` - Protocol selected
- `"Sending HTTP GET request..."` - Requesting firmware
- `"Download progress: X%"` - Progress updates
- `"Download complete"` - Success
- `"OTA download complete - rebooting to flash firmware..."` - About to reboot

**Stage 2 (Flash)**:
- `"Pending OTA update detected"` - NVS flag found
- `"Starting firmware flash from SD card..."` - Flash beginning
- `"Flash progress: X%"` - Flash progress
- `"Firmware flash complete!"` - Success
- `"OTA update successful - rebooting with new firmware..."` - About to reboot

## Error Handling

### Stage 1 Errors (Download)

- **HTTP GET failed**: Server unreachable or URL invalid
- **Invalid firmware size**: Firmware file is 0 bytes or -1
- **SD card write failure**: SD card full or hardware error
- **Download incomplete**: Network disconnection during download

**Recovery**: Device automatically reboots to restore connectivity

### Stage 2 Errors (Flash)

- **Failed to open SD file**: File missing or SD card removed
- **File size mismatch**: Downloaded size doesn't match expected
- **Update.begin() failed**: Insufficient flash space
- **Update.write() failed**: Flash write error
- **Update.end() failed**: Flash verification error

**Recovery**:
- NVS flag cleared BEFORE flash attempt
- Device boots normally with old firmware
- No boot loops

### Critical Design Feature

**NVS flag is cleared BEFORE flash attempt** (line 601-603 in OTAUpdate.cpp):
```cpp
// CRITICAL: Clear pending flag BEFORE attempting flash
// This prevents boot loops if flash fails
prefs.begin("ota", false);
prefs.putBool("pending", false);
prefs.end();
```

This ensures the device never gets stuck in a boot loop, even if flash fails.

## Memory Considerations

### Two-Stage OTA Memory Usage

**Stage 1 (Download - with BLE active)**:
- Free heap: ~38KB
- HTTP client: ~10KB
- WiFiClient: ~2KB
- Download buffer (512B): <1KB
- File operations: ~2KB
- **Total used: ~15KB**
- **Margin: ~23KB**

**Stage 2 (Flash - before BLE starts)**:
- Free heap: ~200KB+
- Update library: ~8KB
- Read buffer (512B): <1KB
- File operations: ~2KB
- **Total used: ~11KB**
- **Margin: ~189KB**

### Heap Diagnostics

Free heap is logged at critical points:
```
[INFO] Free heap before OTA: 38680 bytes
[INFO] Starting two-stage OTA: downloading to SD card...
```

## Dependencies

### Internal
- **SDLogger**: Logging functionality (but disabled during Stage 1)

### External
- `Arduino.h`
- `WiFi.h`
- `WiFiClient.h`
- `WiFiClientSecure.h`
- `ArduinoOTA.h`
- `ESPmDNS.h`
- `Update.h` (ESP32 core)
- `HTTPClient.h`
- `SD_MMC.h`
- `Preferences.h` (NVS)

## Integration with BluetoothOTA

OTAUpdate is designed to work seamlessly with BluetoothOTA:

```cpp
#include <OTAUpdate.h>
#include <BluetoothOTA.h>

OTAUpdate otaUpdate;
BluetoothOTA bleOTA;

void setup() {
    otaUpdate.init("BootBoots-CatCam");
    bleOTA.init("BootBoots-CatCam");
    bleOTA.setOTAUpdate(&otaUpdate);  // Link services
}
```

BluetoothOTA automatically calls `downloadToSD()` instead of `updateFromURL()`.

## Known Issues

### ⚠️ CRITICAL: Stage 1 Crash After Download

**Current Status**: Stage 1 (download) succeeds, but device crashes before setting NVS flag.

**Symptom**:
- Firmware downloaded successfully to SD card (1.9MB file exists)
- Device reboots after download
- Stage 2 never happens (no "Pending OTA update detected" message)
- NVS flag `pending_ota` is never set

**Root Cause**: Device crashes somewhere between `file.close()` and `prefs.begin()`.

**Workarounds Under Investigation**:
1. Set NVS flag BEFORE download starts
2. Use SD card flag file instead of NVS
3. Add memory diagnostics
4. Investigate Preferences/SD_MMC conflict

See `PLAN.md` for detailed debugging information.

## Troubleshooting

### Two-Stage OTA Doesn't Complete

1. **Check SD card**: Verify `firmware_update.bin` exists after Stage 1
2. **Check NVS flag**: Use ESP32 NVS tools to inspect `"ota"` namespace
3. **Check logs**: SD card logs show download progress
4. **Check heap**: Ensure sufficient memory during download
5. **Try manual flash**: If Stage 2 fails, flash via USB

### Update Fails to Start

- Check WiFi connection
- Verify sufficient free heap memory (~38KB minimum)
- Ensure no other update in progress
- Check firmware URL accessibility

### Network OTA Not Discoverable

- Verify mDNS is working (ping `hostname.local`)
- Check firewall settings
- Ensure device and client on same network
- Verify hostname is unique

### HTTP Download Fails

- Check URL accessibility
- Verify firmware file is valid
- Check network stability during download
- Ensure SD card has space

## Diagnostics

### Serial Output Tags

All diagnostic messages are tagged with `[OTA]` for easy filtering:
```
[OTA] Starting two-stage OTA
[OTA] Configuring HTTP client...
[OTA] Using HTTP connection
[OTA] Sending HTTP GET request...
```

### SD Card Logs

During Stage 1, SD logging is disabled. After reboot, check logs for:
```
[INFO] Starting two-stage OTA: downloading to SD card from <URL>
```

During Stage 2, all operations are logged:
```
[INFO] Pending OTA update detected, flashing from SD card...
[INFO] Starting firmware flash from SD card...
[INFO] Flash progress: 50% (970496/1940992 bytes)
[INFO] Firmware flash complete!
```

## Implementation Notes

- Uses singleton pattern for ArduinoOTA callbacks
- Static `_instance` pointer for callback access
- Automatic reboot on successful Stage 1 and Stage 2
- WiFiClient properly managed (v1.0.8+ fix)
- NVS flag cleared before flash (prevents boot loops)
- 512-byte buffers for memory efficiency
- Serial.flush() used before critical operations

## Version History

- v1.0.3: Original direct HTTP OTA
- v1.0.4: Two-stage OTA architecture introduced
- v1.0.5: Fixed NVS flag management + logging
- v1.0.6: Fixed flash access conflict
- v1.0.8: Fixed WiFiClient memory leak
- v1.0.10: Added detailed diagnostics
- v1.0.11: Current - Known issue with NVS flag setting

## Security Considerations

### Password Protection

```cpp
// Enable password for network OTA
otaUpdate.init("hostname", "secure_password_here");
```

- Use strong passwords for production deployments
- Password protects ArduinoOTA (not HTTP updates)
- For HTTP updates, use signed URLs with expiration

### HTTPS Support

- HTTP client supports HTTPS via WiFiClientSecure
- Certificate validation disabled by default (`.setInsecure()`)
- S3 pre-signed URLs recommended for secure firmware distribution
- Signed URLs provide time-based access control

### Firmware Validation

Future enhancements:
- MD5 checksum verification
- Firmware signing
- Version validation

## License

Part of the BootBoots CatCam project.
