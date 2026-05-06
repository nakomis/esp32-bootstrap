# SDLogger

Thread-safe SD card logging service for ESP32-based BootBoots CatCam device.

## Purpose

SDLogger provides a robust, thread-safe logging system that writes structured log entries to an SD card. It implements a singleton pattern to ensure consistent logging across the entire application, supports multiple log levels, automatic log rotation, and specialized logging for deterrent system events.

## Features

- **Thread-Safe**: FreeRTOS mutex protection for multi-threaded environments
- **Multiple Log Levels**: DEBUG, INFO, WARN, ERROR, CRITICAL
- **Formatted Logging**: Printf-style formatting support
- **Log Rotation**: Automatic rotation when files exceed size limit
- **Timestamp Precision**: Millisecond-precision timestamps
- **Dual Output**: Writes to both SD card and Serial monitor
- **Boot Counter**: NVS-based boot counter for reliable log file ordering
- **File Logging Toggle**: Can disable SD file logging while keeping Serial output
- **Convenience Macros**: Simple macros for quick logging
- **Specialized Methods**: Domain-specific logging for deterrent events
- **Configurable**: Adjustable log level, file size, and retention
- **Log Retrieval**: Read recent log entries or all entries from current log file
- **BLE Integration**: Log retrieval API used by BluetoothService for remote access

## Log Levels

```cpp
LOG_DEBUG    = 0  // Detailed debugging information
LOG_INFO     = 1  // General informational messages (default)
LOG_WARN     = 2  // Warning messages
LOG_ERROR    = 3  // Error messages
LOG_CRITICAL = 4  // Critical issues requiring immediate attention
```

## Usage

### Basic Initialization

```cpp
#include <SDLogger.h>

void setup() {
    // Initialize with default log directory (/logs)
    if (SDLogger::getInstance().init()) {
        // Serial.println("Logger initialized successfully");
    } else {
        // Serial.println("Logger initialization failed");
    }
}
```

### Custom Log Directory

```cpp
// Initialize with custom directory
SDLogger::getInstance().init("/custom/logs");
```

### Simple Logging

```cpp
SDLogger::getInstance().debug("Debug message");
SDLogger::getInstance().info("Informational message");
SDLogger::getInstance().warn("Warning message");
SDLogger::getInstance().error("Error message");
SDLogger::getInstance().critical("Critical issue");
```

### Formatted Logging

```cpp
int value = 42;
float temperature = 23.5;
const char* status = "active";

SDLogger::getInstance().debugf("Value: %d", value);
SDLogger::getInstance().infof("Temperature: %.1f°C", temperature);
SDLogger::getInstance().warnf("System status: %s", status);
SDLogger::getInstance().errorf("Error code: %d - %s", errorCode, errorMsg);
SDLogger::getInstance().criticalf("Critical failure at %lu ms", millis());
```

### Convenience Macros

```cpp
// Simple messages
LOG_D("Debug message");
LOG_I("Info message");
LOG_W("Warning message");
LOG_E("Error message");
LOG_C("Critical message");

// Formatted messages
LOG_DF("Counter: %d", count);
LOG_IF("WiFi connected: %s", WiFi.localIP().toString().c_str());
LOG_WF("Free heap: %d bytes", ESP.getFreeHeap());
LOG_EF("Failed to read sensor: %s", sensorName);
LOG_CF("System crash at %lu", millis());
```

### Specialized Deterrent Logging

```cpp
// Log deterrent activation with all probabilities
float probabilities[6] = {0.95, 0.02, 0.01, 0.01, 0.01, 0.00};
SDLogger::getInstance().logDeterrentActivation("Boots", 0.95, probabilities);
// Output: DETERRENT_ACTIVATED: Boots (95.0%) - Probs:[95.0,2.0,1.0,1.0,1.0,0.0]

// Log deterrent rejection
SDLogger::getInstance().logDeterrentRejection("Unknown", 0.45, "Confidence too low");
// Output: DETERRENT_REJECTED: Unknown (45.0%) - Confidence too low

// Log detection without activation
SDLogger::getInstance().logDetection("Boots", 0.72, 42);
// Output: DETECTION: Boots (72.0%) - Picture #42
```

## Configuration

### Set Minimum Log Level

```cpp
// Only log WARN and above (filters out DEBUG and INFO)
SDLogger::getInstance().setLogLevel(LOG_WARN);
```

### Configure Log Rotation

```cpp
// Set maximum file size (in bytes)
SDLogger::getInstance().setMaxFileSize(2 * 1024 * 1024);  // 2MB

// Set maximum number of log files to keep
SDLogger::getInstance().setMaxFiles(20);
```

### Enable/Disable File Logging

```cpp
// Disable SD file logging (Serial output continues)
SDLogger::getInstance().setFileLoggingEnabled(false);

// Re-enable SD file logging
SDLogger::getInstance().setFileLoggingEnabled(true);
```

**Use Cases**:
- Temporarily disable during OTA updates to free memory
- Reduce SD card wear during non-critical operations
- Serial-only logging mode for debugging

### Manual Log Rotation

```cpp
// Force log rotation
SDLogger::getInstance().rotateLogs();
```

### Flush to Disk

```cpp
// Ensure all buffered data is written
SDLogger::getInstance().flush();
```

### Retrieve Log Entries

```cpp
// Get last 10 log entries as JSON array string
String logs = SDLogger::getInstance().getRecentLogEntries(10);
// Returns: ["2023-01-01 00:00:01.234 [INFO] Message 1", "2023-01-01 00:00:02.345 [INFO] Message 2", ...]

// Get ALL log entries from current log file
String allLogs = SDLogger::getInstance().getRecentLogEntries(-1);

// Parse and display
// Note: logs are returned as a JSON array string, can be parsed with ArduinoJson
```

**Use Cases**:
- Remote log retrieval via BLE (used by BluetoothService)
- Diagnostic log viewing on web interface
- Log analysis and debugging

## Log Format

Each log entry follows this format:
```
YYYY-MM-DD HH:MM:SS.mmm [LEVEL] Message
```

Example:
```
1970-01-01 00:00:05.742 [INFO] SDLogger initialized successfully
1970-01-01 00:00:05.758 [INFO] Boot count: 42
1970-01-01 00:00:05.763 [INFO] Log directory: /logs
1970-01-01 00:00:05.780 [INFO] Current log file: /logs/0042_catcam_19700101_000005.log
1970-01-01 00:00:20.102 [WARN] WiFi connection unstable
1970-01-01 00:00:25.431 [ERROR] Camera initialization failed
1970-01-01 00:01:10.892 [CRIT] DETERRENT_ACTIVATED: Boots (95.0%)
```

**Note**: Device timestamps reset to 1970-01-01 on boot (no RTC/NTP configured). Boot counter provides reliable ordering.

## File Management

### Log File Naming

Files are automatically named with boot counter and timestamps:
```
BBBB_catcam_YYYYMMDD_HHMMSS.log
```

Where:
- `BBBB` = 4-digit boot counter (0001, 0002, etc.)
- `YYYYMMDD` = Date (usually 19700101 without RTC)
- `HHMMSS` = Time at initialization

Examples:
```
0001_catcam_19700101_000005.log  (First boot)
0002_catcam_19700101_000008.log  (Second boot)
0003_catcam_19700101_000012.log  (Third boot)
```

**Boot Counter Benefits**:
- Reliable ordering when timestamps reset to 1970
- Easy identification of reboot sequence
- Persistent across power cycles (stored in NVS)
- Automatically incremented on each initialization

### Automatic Rotation

When a log file exceeds the configured maximum size (default 1MB), SDLogger automatically:
1. Closes the current log file
2. Creates a new log file with current timestamp
3. Continues logging to the new file
4. Warns if total file count exceeds maximum

### Directory Structure

```
/logs/
├── 0001_catcam_19700101_000005.log
├── 0002_catcam_19700101_000008.log
├── 0003_catcam_19700101_000012.log
├── 0004_catcam_19700101_000015.log
└── 0005_catcam_19700101_000020.log (current)
```

Files are easily sortable by boot sequence using standard `ls` or `sort` commands.

## Thread Safety

SDLogger uses FreeRTOS mutexes to ensure thread-safe operation:

```cpp
// Safe to call from multiple tasks
void task1(void* param) {
    LOG_I("Task 1 logging");
}

void task2(void* param) {
    LOG_I("Task 2 logging");
}
```

- 1 second timeout for mutex acquisition
- All file operations are protected
- Safe for use in ISRs (if logging calls are brief)

## Boot Counter Implementation

SDLogger uses NVS (Non-Volatile Storage) to maintain a persistent boot counter across device reboots.

**NVS Namespace**: `"sdlogger"`

**Key**: `boot_count` (uint32_t)

### How It Works

1. On `init()`, read current boot count from NVS
2. Increment boot count
3. Store updated count back to NVS
4. Use count in log filename generation

### Manual Boot Counter Access

```cpp
// Read current boot counter (doesn't increment)
Preferences prefs;
prefs.begin("sdlogger", true);  // read-only
uint32_t bootCount = prefs.getUInt("boot_count", 0);
prefs.end();

// Reset boot counter (use with caution)
prefs.begin("sdlogger", false);  // read-write
prefs.putUInt("boot_count", 0);
prefs.end();
```

**Note**: Boot counter is separate from OTA Update's NVS namespace (`"ota"`).

## Dependencies

### Internal
- None (standalone library)

### External
- `Arduino.h`
- `SD.h` / `SD_MMC.h` - SD card interface
- `SPI.h` - SPI communication
- `freertos/FreeRTOS.h` - RTOS support
- `freertos/semphr.h` - Semaphore/mutex support
- `Preferences.h` - NVS (Non-Volatile Storage) for boot counter

## Hardware Requirements

### SD Card Interface

SDLogger uses SD_MMC interface. Pin configuration depends on the board:

**ESP32-S3 CAM (Primary Target)**:
```cpp
// SDMMC 1-bit mode
SD_MMC_CLK  = GPIO 39
SD_MMC_CMD  = GPIO 38
SD_MMC_D0   = GPIO 40

// Must call before SD_MMC.begin():
SD_MMC.setPins(39, 38, 40);
SD_MMC.begin("/sdcard", true);  // true = 1-bit mode
```

**Legacy ESP32-CAM (AI-Thinker)**:
```cpp
pinMode(14, PULLUP);  // CLK
pinMode(15, PULLUP);  // CMD
pinMode(2, PULLUP);   // DATA0
pinMode(4, PULLUP);   // DATA1 (optional for 4-bit mode)
pinMode(12, PULLUP);  // DATA2 (optional for 4-bit mode)
pinMode(13, PULLUP);  // DATA3 (optional for 4-bit mode)
```

Ensure your hardware has these pins connected appropriately for SD_MMC mode.

## Error Handling

### Initialization Failures

```cpp
if (!SDLogger::getInstance().init()) {
    // Serial.println("SD card initialization failed");
    // Serial.println("Check SD card insertion and formatting");
    // Continue without logging or implement fallback
}
```

Common causes:
- SD card not inserted
- SD card not formatted (use FAT32)
- Hardware connection issues
- Insufficient power

### Runtime Behavior

- If SD card becomes unavailable, logs continue to Serial but not to file
- Failed file operations return false but don't crash
- Mutex timeout (1 second) prevents indefinite blocking

## Performance Considerations

### Buffer Size

Formatted log messages are limited to 512 bytes:
```cpp
char buffer[512];  // Maximum formatted message size
```

For longer messages, split into multiple log calls.

### File Operations

- Each log call opens, writes, and closes the file
- Synchronous operation - may block briefly during writes
- Consider calling frequency in time-critical code

### Memory Usage

- Singleton pattern: ~200 bytes static memory
- Mutex: ~100 bytes
- Temporary buffers during logging

## Best Practices

### 1. Initialize Early
```cpp
void setup() {
    Serial.begin(115200);
    SDLogger::getInstance().init();  // Initialize before other components
    // ... rest of setup
}
```

### 2. Use Appropriate Log Levels
```cpp
LOG_D("Entering loop iteration");     // DEBUG - verbose, disable in production
LOG_I("WiFi connected");              // INFO - normal operations
LOG_W("Retry attempt 3 of 5");        // WARN - potential issues
LOG_E("Failed to read sensor");       // ERROR - failures
LOG_C("Out of memory - rebooting");   // CRITICAL - system-threatening
```

### 3. Check Initialization
```cpp
if (SDLogger::getInstance().isInitialized()) {
    LOG_I("Starting application");
}
```

### 4. Set Production Log Level
```cpp
#ifdef DEBUG_BUILD
    SDLogger::getInstance().setLogLevel(LOG_DEBUG);
#else
    SDLogger::getInstance().setLogLevel(LOG_INFO);
#endif
```

### 5. Use Formatted Logging
```cpp
// Good - formatted
LOG_IF("Uptime: %lu seconds", millis() / 1000);

// Avoid - string concatenation
String msg = "Uptime: " + String(millis() / 1000) + " seconds";
LOG_I(msg.c_str());
```

## Troubleshooting

### No Logs Written to SD Card

1. Check SD card formatting (must be FAT32)
2. Verify SD card is inserted properly
3. Check serial output for initialization errors
4. Verify sufficient free space on SD card
5. Test SD card in another device

### Logs Stop Being Written

1. SD card may be full - check free space
2. SD card may have been removed
3. File system corruption - reformat SD card
4. Power issues - check power supply stability

### Missing Log Entries

1. Ensure `flush()` is called before critical operations
2. Check if log level filters are too restrictive
3. Verify mutex isn't causing timeouts (check for deadlocks)

### Timestamps Incorrect

System time must be set via NTP or RTC:
```cpp
// Set time from NTP
configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
```

## Integration with Other Services

SDLogger is used throughout the CatCam project:

```cpp
// BluetoothOTA
SDLogger::getInstance().infof("Bluetooth client connected");

// OTAUpdate
SDLogger::getInstance().criticalf("*** OTA UPDATE STARTED ***");

// WifiConnect (could be integrated)
LOG_IF("WiFi connected: %s", WiFi.localIP().toString().c_str());

// BluetoothService - Log retrieval
String logs = SDLogger::getInstance().getRecentLogEntries(50);
// Sends logs to mobile app via BLE
```

All services should use SDLogger for consistent logging.

### BLE Log Retrieval Integration

SDLogger's `getRecentLogEntries()` method is used by BluetoothService to provide remote log access:

**BluetoothService Integration**:
```cpp
// In BluetoothService.cpp - handling "get_logs" command
String getLatestLogEntries(int maxEntries) {
    return SDLogger::getInstance().getRecentLogEntries(maxEntries);
}

// Client requests logs via BLE command
// {"command": "get_logs", "entries": -1}
// Service retrieves all logs and sends in chunks
String logData = getLatestLogEntries(-1);
// ... chunk and send via BLE notifications
```

**Key Features for BLE**:
- Returns JSON array string for easy parsing by web app
- Supports retrieving all entries (`maxLines = -1`) for complete logs
- Thread-safe for concurrent access during BLE operations
- Memory-efficient circular buffer for limited entry counts

## API Reference

### Singleton Access
```cpp
static SDLogger& getInstance()
```

### Initialization
```cpp
bool init(const char* logDir = "/logs")
```

### Basic Logging
```cpp
void debug(const char* message)
void info(const char* message)
void warn(const char* message)
void error(const char* message)
void critical(const char* message)
```

### Formatted Logging
```cpp
void debugf(const char* format, ...)
void infof(const char* format, ...)
void warnf(const char* format, ...)
void errorf(const char* format, ...)
void criticalf(const char* format, ...)
```

### Generic Logging
```cpp
void log(LogLevel level, const char* message)
void logf(LogLevel level, const char* format, ...)
```

### Specialized Logging
```cpp
void logDeterrentActivation(const char* catName, float confidence, const float* allProbs)
void logDeterrentRejection(const char* catName, float confidence, const char* reason)
void logDetection(const char* catName, float confidence, int pictureNumber)
```

### File Management
```cpp
void flush()
void rotateLogs()
```

### Log Retrieval
```cpp
String getRecentLogEntries(int maxLines = 10)  // Returns JSON array string; use -1 for all entries
```

### Configuration
```cpp
void setLogLevel(LogLevel minLevel)
void setMaxFileSize(size_t maxSize)
void setMaxFiles(int maxFiles)
void setFileLoggingEnabled(bool enabled)  // Toggle SD file logging
```

### Status
```cpp
bool isInitialized() const
```

### Boot Counter
The boot counter is automatically managed during `init()` and stored in NVS namespace `"sdlogger"` with key `boot_count`.

## License

Part of the BootBoots CatCam project.
