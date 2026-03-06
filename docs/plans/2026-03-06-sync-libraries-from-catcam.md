# Sync Libraries from catcam Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Bring the esp32-bootstrap template libraries up to date with the more mature versions in bootboots/catcam, adding log retrieval, improved OTA memory management, and three new optional libraries (CommandDispatcher, MqttService, MqttOTA).

**Architecture:** Four library updates (SDLogger, OTAUpdate, OTABleService, WifiConnect no-op) followed by three new optional libraries copied from catcam and wired into the bootstrap CLI tool. All changes are in `libraries/` and `templates/`. The bootstrap CLI script generates the new projects, so any new optional library needs a new interactive prompt added there too.

**Tech Stack:** C++17, Arduino/ESP-IDF, PlatformIO, Python (CLI tool)

**Source of truth for new code:** `/Users/martinmu_1/repos/nakomis/bootboots/embedded/catcam/lib/`

---

### Task 1: Sync SDLogger — add log retrieval API

The catcam SDLogger has a full log retrieval API (getRecentLogEntries, processRecentLogEntries, listLogFiles, processLogFile) that bootstrap's copy lacks. Also missing: setMaxFiles(), stats counters, proper flush semaphore, and _lastDropWarning throttling.

**Files:**
- Modify: `libraries/core/SDLogger/src/SDLogger.h`
- Modify: `libraries/core/SDLogger/src/SDLogger.cpp`
- Reference: `/Users/martinmu_1/repos/nakomis/bootboots/embedded/catcam/lib/SDLogger/src/SDLogger.h`
- Reference: `/Users/martinmu_1/repos/nakomis/bootboots/embedded/catcam/lib/SDLogger/src/SDLogger.cpp`

**Step 1: Read the catcam SDLogger .cpp to understand the implementation**

```bash
cat /Users/martinmu_1/repos/nakomis/bootboots/embedded/catcam/lib/SDLogger/src/SDLogger.cpp
```

**Step 2: Update SDLogger.h**

Add to the public API (after the existing flush/rotateLogs section):

```cpp
// Log retrieval — used for BLE/MQTT log viewing
String getRecentLogEntries(int maxLines = 50);
void processRecentLogEntries(int maxLines, std::function<void(const String&)> processor);
std::vector<String> listLogFiles();
void processLogFile(const String& filename, int maxLines, std::function<void(const String&)> processor);

// Configuration
void setMaxFiles(int maxFiles) { _maxFiles = maxFiles; }

// Stats
uint32_t getTotalEnqueued() const { return _totalEnqueued; }
uint32_t getTotalWritten() const { return _totalWritten; }
uint32_t getQueueDepth() const;
```

Add to includes at top:
```cpp
#include <vector>
```

Add to private members:
```cpp
int _maxFiles = 10;
SemaphoreHandle_t _flushSemaphore = nullptr;
volatile uint32_t _totalEnqueued = 0;
volatile uint32_t _totalWritten = 0;
uint32_t _lastDropWarning = 0;
static const int BATCH_SIZE = 8;
```

Also bump `QUEUE_SIZE` from 32 to 64 (matches catcam).

**Step 3: Update SDLogger.cpp**

Add the new method implementations copied from catcam. Key methods:

- `getRecentLogEntries(int maxLines)` — opens the current log file, reads the last N lines, returns as a newline-joined String
- `processRecentLogEntries(int maxLines, callback)` — same but streams lines one at a time via callback
- `listLogFiles()` — opens the log directory, returns sorted vector of `.log` filenames
- `processLogFile(filename, maxLines, callback)` — opens a specific log file by name, streams lines

Also update `flush()` to use a semaphore-based wait if `_flushSemaphore` is initialised (init it in `init()`).

Update the writer task to track `_totalEnqueued` / `_totalWritten`, process in batches of `BATCH_SIZE`, and throttle the "queue full" log warning using `_lastDropWarning`.

Also update `cleanupOldLogs()` to respect `_maxFiles` (catcam has this logic).

**Step 4: Verify the header is self-consistent**

Read back both files and check: every method declared in .h has an implementation in .cpp, all new members are initialised.

**Step 5: Commit**

```bash
cd /Users/martinmu_1/repos/nakomis/esp32-bootstrap
git add libraries/core/SDLogger/src/SDLogger.h libraries/core/SDLogger/src/SDLogger.cpp
git commit -m "Update SDLogger: add log retrieval API and stats from catcam"
```

---

### Task 2: Sync OTAUpdate — lazy clients, platform ifdefs, SDLogger, heap diagnostics

**Files:**
- Modify: `libraries/core/OTAUpdate/src/OTAUpdate.h`
- Modify: `libraries/core/OTAUpdate/src/OTAUpdate.cpp`
- Reference: `/Users/martinmu_1/repos/nakomis/bootboots/embedded/catcam/lib/OTAUpdate/src/OTAUpdate.cpp`

**Step 1: Read catcam OTAUpdate.cpp again for reference**

Already read in analysis — but re-check for any details missed.

**Step 2: Update OTAUpdate.h**

Add `_initialized` bool member (for future use). The catcam header also includes SDLogger. The bootstrap header currently includes SDLogger only in the .cpp — ensure consistency.

Remove the `downloadAndFlash()` declaration? No — keep it, it's still useful as a fallback for projects without SD cards. Add a note in a comment.

**Step 3: Update OTAUpdate.cpp — constructor**

Change constructor to NOT create HTTP/SSL clients:
```cpp
OTAUpdate::OTAUpdate() {
    _updating = false;
    _progress = 0;
    _status = "Ready";
    _updateCallback = nullptr;
    _progressCallback = nullptr;
    _httpClient = nullptr;   // Created on demand
    _secureClient = nullptr; // Created on demand
    _totalSize = 0;
    _downloadedSize = 0;
}
```

**Step 4: Update OTAUpdate.cpp — downloadToSD()**

At the start of `downloadToSD()`, add lazy client cleanup:
```cpp
// Clean up any existing clients to avoid fragmentation
if (_httpClient) { delete _httpClient; _httpClient = nullptr; }
if (_secureClient) { delete _secureClient; _secureClient = nullptr; }
```

Replace the SD_MMC remount section with platform-aware code:
```cpp
SD_MMC.end();
delay(100);

#ifdef ESP32S3_CAM
// Re-enable pull-ups (SD_MMC.end() may reset them)
pinMode(39, INPUT_PULLUP);  // CLK
pinMode(38, INPUT_PULLUP);  // CMD
pinMode(40, INPUT_PULLUP);  // D0
delay(10);
SD_MMC.setPins(39, 38, 40);
if (!SD_MMC.begin("/sdcard", true)) {  // 1-bit mode
#else
if (!SD_MMC.begin()) {
#endif
    SDLogger::getInstance().errorf("Failed to remount SD_MMC for OTA download");
    delay(2000);
    ESP.restart();
    return false;
}
```

Add HTTPS memory diagnostics before creating the SSL client:
```cpp
#include <esp_heap_caps.h>
// In the HTTPS branch, before creating _secureClient:
SDLogger::getInstance().infof("Largest INTERNAL free block: %d bytes",
    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
SDLogger::getInstance().infof("Free internal heap: %d bytes",
    heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
SDLogger::getInstance().infof("Free PSRAM: %d bytes",
    heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
```

Replace all `log_i` / `log_e` / `log_w` calls with `SDLogger::getInstance().infof()` / `.errorf()` / `.warnf()`.

**Step 5: Verify downloadAndFlash() still compiles**

`downloadAndFlash()` uses the old approach (eager client). Update it to also create clients lazily:
```cpp
// At start of downloadAndFlash():
if (_httpClient) { delete _httpClient; _httpClient = nullptr; }
if (_secureClient) { delete _secureClient; _secureClient = nullptr; }
```

Also replace its `log_i`/`log_e` with SDLogger.

**Step 6: Commit**

```bash
git add libraries/core/OTAUpdate/src/OTAUpdate.h libraries/core/OTAUpdate/src/OTAUpdate.cpp
git commit -m "Update OTAUpdate: lazy client creation, platform ifdefs, SDLogger, heap diagnostics"
```

---

### Task 3: Sync OTABleService — larger buffer, SDLogger

**Files:**
- Modify: `libraries/core/OTABleService/src/OTABleService.h`
- Modify: `libraries/core/OTABleService/src/OTABleService.cpp`

**Step 1: Update OTABleService.h**

Change `MAX_PENDING_SIZE` from 512 to 4096:
```cpp
static const int MAX_PENDING_SIZE = 4096;
```

**Step 2: Update OTABleService.cpp**

Replace all `log_i` / `log_e` / `log_w` calls with SDLogger equivalents:
- `log_i(...)` → `SDLogger::getInstance().infof(...)`
- `log_e(...)` → `SDLogger::getInstance().errorf(...)`
- `log_w(...)` → `SDLogger::getInstance().warnf(...)`

Add `#include "SDLogger.h"` at the top (OTABleService already includes OTAUpdate.h which will pull in SDLogger, but be explicit).

**Step 3: Verify the buffer change is safe**

The `_pendingBuffer` is a stack member of `OTABleService` (which lives on the heap as a class instance), so 4096 bytes is fine.

**Step 4: Commit**

```bash
git add libraries/core/OTABleService/src/OTABleService.h libraries/core/OTABleService/src/OTABleService.cpp
git commit -m "Update OTABleService: bump pending buffer to 4096 bytes, use SDLogger"
```

---

### Task 4: Add CommandDispatcher as a new optional library

CommandDispatcher is the unified command dispatch pattern from catcam — it decouples transport (BLE/MQTT) from command logic. It's a prerequisite for MqttService and MqttOTA.

**Files:**
- Create: `libraries/optional/CommandDispatcher/src/CommandDispatcher.h`
- Create: `libraries/optional/CommandDispatcher/src/CommandDispatcher.cpp`
- Create: `libraries/optional/CommandDispatcher/library.json`
- Reference: `/Users/martinmu_1/repos/nakomis/bootboots/embedded/catcam/lib/CommandDispatcher/src/`

**Step 1: Read the catcam CommandDispatcher source**

```bash
cat /Users/martinmu_1/repos/nakomis/bootboots/embedded/catcam/lib/CommandDispatcher/src/CommandDispatcher.h
cat /Users/martinmu_1/repos/nakomis/bootboots/embedded/catcam/lib/CommandDispatcher/src/CommandDispatcher.cpp
```

**Step 2: Copy with clean headers**

Copy the files verbatim but:
- Remove any catcam-specific includes (e.g., `SystemState.h` if present)
- Remove any `#include` that references catcam's own relative paths outside CommandDispatcher
- Keep `IResponseSender`, `CommandContext`, and `CommandDispatcher` intact

**Step 3: Create library.json**

```json
{
  "name": "CommandDispatcher",
  "version": "1.0.0",
  "description": "Unified command dispatch for BLE and MQTT transports",
  "frameworks": "arduino",
  "platforms": "espressif32"
}
```

**Step 4: Commit**

```bash
git add libraries/optional/CommandDispatcher/
git commit -m "Add CommandDispatcher optional library from catcam"
```

---

### Task 5: Add MqttService as a new optional library

MqttService provides AWS IoT MQTT over mTLS using device certificates from secrets.h. It depends on PubSubClient and CommandDispatcher.

**Files:**
- Create: `libraries/optional/MqttService/src/MqttService.h`
- Create: `libraries/optional/MqttService/src/MqttService.cpp`
- Create: `libraries/optional/MqttService/library.json`
- Reference: `/Users/martinmu_1/repos/nakomis/bootboots/embedded/catcam/lib/MqttService/src/`

**Step 1: Read catcam MqttService source**

```bash
cat /Users/martinmu_1/repos/nakomis/bootboots/embedded/catcam/lib/MqttService/src/MqttService.h
cat /Users/martinmu_1/repos/nakomis/bootboots/embedded/catcam/lib/MqttService/src/MqttService.cpp
```

**Step 2: Copy with adaptations**

Copy verbatim but:
- Replace any catcam-specific topic prefix (`catcam/`) with a configurable topic prefix. Add a `setTopicPrefix(const char*)` method and store it, defaulting to `"device/"`.
- Remove any hard reference to `SystemState` — replace `setSystemState(SystemState*)` with a generic `setStatusCallback(std::function<String()> callback)` that returns a JSON status string. The caller populates it.
- Keep the `pause()` / `resume()` methods (critical for OTA).

**Step 3: Create library.json**

```json
{
  "name": "MqttService",
  "version": "1.0.0",
  "description": "AWS IoT MQTT client with mTLS, command dispatch, and pause/resume for OTA",
  "frameworks": "arduino",
  "platforms": "espressif32",
  "dependencies": {
    "PubSubClient": "^2.8"
  }
}
```

**Step 4: Commit**

```bash
git add libraries/optional/MqttService/
git commit -m "Add MqttService optional library from catcam (generalised topic prefix, status callback)"
```

---

### Task 6: Add MqttOTA as a new optional library

MqttOTA registers OTA command handlers (ota_update, url_chunk, ota_cancel) with CommandDispatcher so OTA can be triggered via MQTT.

**Files:**
- Create: `libraries/optional/MqttOTA/src/MqttOTA.h`
- Create: `libraries/optional/MqttOTA/src/MqttOTA.cpp`
- Create: `libraries/optional/MqttOTA/library.json`
- Reference: `/Users/martinmu_1/repos/nakomis/bootboots/embedded/catcam/lib/MqttOTA/src/`

**Step 1: Read catcam MqttOTA source**

Already read in analysis. Re-read to check for missed details.

**Step 2: Copy with clean headers**

Copy verbatim. The catcam version references:
- `OTAUpdate` — already in bootstrap core
- `MqttService` — new optional library (Task 5)
- `BluetoothOTA` — the catcam-specific name; change to reference `OTABleService` or forward-declare generically as a pointer with a `stopAdvertising()` call

Actually, the only thing MqttOTA uses BluetoothOTA for is `BLEDevice::stopAdvertising()`. Replace the `BluetoothOTA* _bleOTA` member with a simpler `bool _stopBleOnOTA = false` flag and call `BLEDevice::stopAdvertising()` directly if set.

**Step 3: library.json**

```json
{
  "name": "MqttOTA",
  "version": "1.0.0",
  "description": "MQTT-triggered OTA updates via CommandDispatcher",
  "frameworks": "arduino",
  "platforms": "espressif32"
}
```

**Step 4: Commit**

```bash
git add libraries/optional/MqttOTA/
git commit -m "Add MqttOTA optional library from catcam"
```

---

### Task 7: Wire new libraries into the bootstrap CLI tool

The bootstrap CLI (`esp32-bootstrap`) has an interactive prompt asking which optional libraries to include. Add CommandDispatcher, MqttService, and MqttOTA to the prompt.

**Files:**
- Modify: `esp32-bootstrap` (the Python CLI script)

**Step 1: Read the current CLI optional library prompt section**

```bash
grep -n "optional\|AWSAuth\|Camera\|questionary\|checkbox" /Users/martinmu_1/repos/nakomis/esp32-bootstrap/esp32-bootstrap
```

**Step 2: Add new library choices**

Find the checkbox prompt and add:
```python
{
    "name": "CommandDispatcher (unified BLE+MQTT command routing)",
    "value": "CommandDispatcher",
    "checked": False
},
{
    "name": "MqttService (AWS IoT MQTT with mTLS)",
    "value": "MqttService",
    "checked": False
},
{
    "name": "MqttOTA (MQTT-triggered OTA updates, requires MqttService + CommandDispatcher)",
    "value": "MqttOTA",
    "checked": False
},
```

**Step 3: Add dependency enforcement**

If MqttOTA is selected but MqttService or CommandDispatcher are not, auto-add them:
```python
if "MqttOTA" in selected_optional:
    for dep in ["MqttService", "CommandDispatcher"]:
        if dep not in selected_optional:
            print(f"  → Auto-adding {dep} (required by MqttOTA)")
            selected_optional.append(dep)
```

**Step 4: Add platformio.ini lib_deps entry for PubSubClient**

When MqttService is selected, add `knolleary/PubSubClient@^2.8` to the generated `platformio.ini`.

Find where platformio.ini is generated/templated and add logic:
```python
extra_lib_deps = []
if "MqttService" in selected_optional:
    extra_lib_deps.append("knolleary/PubSubClient@^2.8")
```

Then inject into the platformio.ini template substitution.

**Step 5: Commit**

```bash
git add esp32-bootstrap
git commit -m "Add CommandDispatcher, MqttService, MqttOTA to bootstrap CLI optional library prompts"
```

---

### Task 8: Update CLAUDE.md and README

**Files:**
- Modify: `CLAUDE.md`
- Modify: `README.md` (if exists at project root)

**Step 1: Update CLAUDE.md optional libraries table**

Add rows to the optional libraries table:

| CommandDispatcher | Unified command dispatch for BLE + MQTT | catcam |
| MqttService | AWS IoT MQTT client with mTLS, pause/resume | catcam |
| MqttOTA | MQTT-triggered OTA with URL chunking | catcam |

**Step 2: Update SDLogger documentation section**

Add a note about the log retrieval API (`getRecentLogEntries`, `listLogFiles`, etc.) in the Technical Notes section.

**Step 3: Commit**

```bash
git add CLAUDE.md
git commit -m "Update CLAUDE.md: document new optional libraries and SDLogger retrieval API"
```

---

### Task 9: Create a feature branch and open a PR

**Step 1: Check current branch and create feature branch**

All commits above should have been made on a feature branch. If they were made on main, create the branch now:
```bash
cd /Users/martinmu_1/repos/nakomis/esp32-bootstrap
git checkout -b sync-libraries-from-catcam
```

**Step 2: Push and open PR**

```bash
git push -u origin sync-libraries-from-catcam
gh pr create --title "Sync libraries from catcam: SDLogger retrieval API, OTA improvements, MQTT stack" \
  --body "..."
```
