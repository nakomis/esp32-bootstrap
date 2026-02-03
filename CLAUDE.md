# ESP32 Bootstrap Project

## Project Overview
A CLI tool to bootstrap new ESP32-S3-CAM projects with a standardized structure, ready to build and deploy via OTA using the sandboxsite.

**Command:** `esp32-bootstrap <project-name>`

## Status
**Phase: Complete** - CLI tool is ready for use

### Completed
- [x] Created `nakomis/esp32-bootloader` repo with generic bootloader
- [x] Bootloader supports ESP32-S3 (16MB) and ESP32-CAM (4MB)
- [x] Bootloader build_and_upload.py with `internal: true` metadata
- [x] Standardized NAKOMIS_ESP32 BLE UUIDs across all projects
- [x] OTA flow verified end-to-end (BLE → WiFi → S3 → SD → Bootloader)
- [x] Built esp32-bootstrap CLI tool with interactive prompts
- [x] Created all project templates
- [x] Copied core libraries (SDLogger, WifiConnect, OTAUpdate, OTABleService)
- [x] Created new WifiBleService library for BLE WiFi configuration
- [x] Copied optional libraries (AWSAuth, Camera)
- [x] Comprehensive .gitignore excludes certificates and secrets
- [x] Fixed CDK IoT certificate creation (uses `cdk-iot-core-certificates-v3` package)
- [x] Fixed SDLogger API usage in main.cpp template (singleton pattern, LOG_I/LOG_IF macros)
- [x] Fixed OTABleService to not depend on project-specific version.h
- [x] Fixed WifiBleService to include WiFi.h
- [x] Simplified directory structure (removed nested project name under embedded/)
- [x] Made .py scripts executable in generated projects

### Future Enhancements
- [ ] Update sandboxsite to filter `internal: true` projects from dropdown
- [ ] Add certificate management scripts (from csotp)

## Project Structure to Generate

```
<project-name>/
├── README.md                    # Skeleton with auto-updating TOC
├── CLAUDE.md                    # Pre-filled project instructions
├── .gitignore                   # Comprehensive ignore file
├── .tool-versions               # asdf: python 3.11.14
├── .claude/
│   └── settings.local.json      # Claude Code permissions
│
├── scripts/                     # Project-wide scripts
│   └── install-hooks.sh         # Git hooks installer
│
├── infra/                       # AWS CDK infrastructure
│   ├── bin/infra.ts             # CDK app entry point
│   ├── lib/
│   │   └── iot-device-stack.ts  # IoT Thing + certificates (uses cdk-iot-core-certificates-v3)
│   ├── package.json
│   ├── tsconfig.json
│   └── cdk.json
│
├── githooks/                    # Git hooks
│   └── pre-commit               # README TOC auto-update
│
└── embedded/                    # PlatformIO project
    ├── src/main.cpp             # Minimal working example
    ├── include/
    │   ├── version.h
    │   └── secrets.h            # Generated from SSM
    ├── lib/                     # Libraries
    │   ├── SDLogger/
    │   ├── WifiConnect/
    │   ├── WifiBleService/
    │   ├── OTAUpdate/
    │   └── OTABleService/
    ├── scripts/
    │   ├── factory_setup.sh
    │   ├── build_and_flash.sh
    │   ├── build_and_upload.py
    │   └── generate_secrets.py
    ├── platformio.ini
    ├── partitions.csv
    └── .gitignore
```

## Key Components

### 1. README.md with Auto-TOC
- Skeleton README with sections for the user to fill in
- Git pre-commit hook to auto-update table of contents
- Uses `markdown-toc` npm package (same as nakom.is project)

### 2. .gitignore
Combines patterns for:
- PlatformIO (.pio, .pioenvs, .piolibdeps)
- VSCode (.vscode/*, but keep extensions.json)
- macOS (.DS_Store)
- Compiled artifacts (*.bin, *.elf, *.hex)
- Secrets (secrets.h, credentials.h, *.local.*)
- Python (__pycache__, venv)
- IDE files (.idea, *.sublime-*)

### 3. CLAUDE.md
Pre-filled with:
- Project description placeholder
- Hardware section (ESP32-S3-N16R8)
- Pin mapping template
- Build commands (pio run, scripts)
- Library documentation links
- BLE service UUID conventions
- OTA update flow
- Factory setup instructions
- S3 bucket naming convention
- Reference to sandboxsite

### 4. Infra (CDK)
Uses `cdk-iot-core-certificates-v3` package for IoT certificate creation:
- Creates IoT Thing with auto-generated certificate via Custom Resource Lambda
- Saves cert/key to SSM Parameter Store (captured at creation time)
- Creates IAM role for credentials provider
- Creates IoT policy for MQTT (connect, subscribe, publish)
- Role alias for temporary credentials

**Note:** The Lambda function is necessary because AWS IoT only returns the certificate PEM and private key at creation time. The Custom Resource captures these values and stores them in SSM.

**Naming convention:**
- Thing: `<ProjectName>Thing`
- SSM prefix: `<ProjectName>`
- Role: `<ProjectName>ThingIamRole`
- Policy: `<ProjectName>ExtendedAccessPolicy`
- Role alias: `<ProjectName>RoleAlias`
- MQTT topics: `<project-name>/*`

### 5. Embedded Scripts

#### factory_setup.sh
- Builds bootloader and main app
- Erases otadata partition
- Flashes bootloader to factory (0x10000)
- Flashes app to OTA0 (0x110000)
- Color output, prerequisite checks

#### build_and_flash.sh
- Development workflow
- Auto-detects USB port
- Builds and flashes to OTA0

#### build_and_upload.py
- Version bumping (major/minor/patch)
- PlatformIO build
- S3 upload with metadata
- **S3 bucket:** `nakomis-firmware-updates` (shared)
- **S3 key:** `<ProjectName>/<version>/firmware.bin`

#### generate_secrets.py
- Fetches from SSM Parameter Store:
  - `/<ProjectName>/<ThingName>/certPem`
  - `/<ProjectName>/<ThingName>/privKey`
  - `/Nakomis/Wifi/SSID`
  - `/Nakomis/Wifi/Password`
- Gets IoT endpoints from AWS IoT API
- Reads local AmazonRootCA1.pem
- Generates secrets.h

### 6. Core Libraries (Always Included)

| Library | Purpose | Source |
|---------|---------|--------|
| SDLogger | Async SD card logging with FreeRTOS | kappa-warmer |
| WifiConnect | WiFi with NVS credentials storage | kappa-warmer |
| WifiBleService | BLE WiFi configuration (SSID/password via BLE) | NEW (based on kappa-warmer) |
| OTAUpdate | Direct-to-flash OTA downloads | kappa-warmer |
| OTABleService | BLE OTA service (sandboxsite compatible) | kappa-warmer |

### WiFi BLE Configuration

The `WifiBleService` library provides BLE commands for WiFi configuration:
- `{"command":"wifi_config","ssid":"...","password":"..."}` - Save WiFi credentials to NVS
- `{"command":"wifi_connect"}` - Connect to saved WiFi network
- `{"command":"wifi_status"}` - Get current WiFi connection status

This allows users to configure WiFi via the sandboxsite Bluetooth page without needing serial access.

### 7. Optional Libraries (User Selects)

| Library | Purpose | Source |
|---------|---------|--------|
| AWSAuth | SigV4 signing + credentials provider | bootboots |
| Camera | ESP32-S3-CAM capture with PSRAM | bootboots |

**Implementation:** Interactive prompt during bootstrap asking which optional libraries to include.

### 8. Bootloader (Separate Repo)

**Repository:** `nakomis/esp32-bootloader` (https://github.com/nakomis/esp32-bootloader)

Minimal factory partition app that:
- Checks for `/firmware_update.bin` on SD card
- Flashes pending update to OTA_0
- Sets boot partition for next reboot
- Always boots into main app from OTA_0

The bootloader is:
- Downloaded from S3 by `factory_setup.sh` (not built locally)
- Marked as `internal: true` in S3 metadata (hidden from sandboxsite dropdown)
- Supports ESP32-S3 (16MB) and original ESP32-CAM (4MB)

### 9. Partition Table (16MB Flash)

| Name | Type | Offset | Size |
|------|------|--------|------|
| nvs | data | 0x9000 | 20KB |
| otadata | data | 0xe000 | 8KB |
| factory | app | 0x10000 | 1MB |
| ota_0 | app | 0x110000 | 7MB |
| spiffs | data | 0x810000 | 7.875MB |
| coredump | data | 0xFF0000 | 64KB |

## Decisions Made

### 1. README TOC Generation
**Decision:** Use `markdown-toc` npm package (same as nakom.is project)
- Requires Node.js installed
- Pre-commit hook will run markdown-toc on README.md changes

### 2. CLI Tool Implementation
**Decision:** Python script with interactive checkbox-style prompts
- Use a library like `inquirer` or `questionary` for checkbox UI
- Will prompt for optional libraries and features

### 3. PlatformIO Initialization
**Decision:** Copy pre-configured platformio.ini (already tuned for ESP32-S3-N16R8)

### 4. Project Naming Conventions
- Directory name: `<project-name>` (lowercase-with-dashes)
- CDK identifiers: `<ProjectName>` (PascalCase)
- S3 keys: `<ProjectName>/<version>/firmware.bin`
- MQTT topics: `<project-name>/*`
- BLE device name: `<ProjectName>`

### 5. AWS Profile
Require `AWS_PROFILE=nakom.is-sandbox` for:
- CDK deployment
- generate_secrets.py
- build_and_upload.py

### 6. Sandboxsite Integration
- OTA firmware updates via sandboxsite
- BLE device connection/configuration via sandboxsite
- **Standardized BLE UUIDs** for all projects (bootboots-compatible)
- Stretch goal: Make UUIDs/configs discoverable in web UI

### 7. S3 Bucket
**Decision:** Use shared `nakomis-firmware-updates` bucket
- Stretch goal: Rename bucket to something more generic

### 8. Optional Features (Interactive Selection)
- AWSAuth library (SigV4 signing + credentials provider)
- Camera library (ESP32-S3-CAM capture with PSRAM)

### 9. Bootloader
**Decision:** Separate git repo with S3-hosted firmware
- Bootloader is a standalone project with its own versioning
- Firmware stored in S3: `Bootloader/<version>/firmware.bin`
- factory_setup.sh downloads latest bootloader from S3 (not built locally)
- Bootloader versions NOT shown in sandboxsite dropdown

## Resolved Questions

### 1. Bootloader Repo
**Decision:** Create `nakomis/esp32-bootloader` as a separate repo

### 2. S3 Bootloader Structure
**Decision:** Confirmed structure:
```
s3://nakomis-firmware-updates/
├── Bootloader/
│   ├── latest/firmware.bin      # Symlink/copy of newest version
│   └── <version>/firmware.bin
├── KappaWarmer/...
└── <NewProject>/...
```

### 3. Sandboxsite Version Dropdown & Internal Projects
**How it works:**
- Projects listed by S3 prefix (top-level folders)
- Versions from `<ProjectName>/manifest.json` file
- Sorted by semantic version (newest first)

**Decision:** Add "internal" flag to manifest schema
- Bootloader has `manifest.json` with `"internal": true`
- Sandboxsite filters out internal projects from dropdown
- factory_setup.sh reads Bootloader manifest to find latest version

**Manifest schema update:**
```json
{
  "project": "string",
  "internal": boolean,  // NEW: true = hidden from UI
  "versions": [...]
}
```

### 4. Bootloader Caching
**Decision:** Always fetch fresh from S3

## Implementation Plan (Completed)

### Phase 1: Project Structure ✓
1. Create bootstrap script (Python)
2. Set up template directory structure
3. Implement project name substitution

### Phase 2: Core Files ✓
1. README.md template
2. CLAUDE.md template
3. .gitignore
4. .tool-versions

### Phase 3: Embedded Setup ✓
1. Copy/template platformio.ini files
2. Copy partition tables
3. Set up bootloader project
4. Set up main app skeleton
5. Copy core libraries (SDLogger, WifiConnect, OTAUpdate, OTABleService, WifiBleService)

### Phase 4: Scripts ✓
1. factory_setup.sh (template with project name)
2. build_and_flash.sh (template)
3. build_and_upload.py (template with S3 bucket, project name)
4. generate_secrets.py (template with SSM paths)

### Phase 5: Infrastructure ✓
1. CDK project structure
2. iot-device-stack.ts template
3. package.json with dependencies

### Phase 6: Optional Features ✓
1. Interactive prompt for optional libraries
2. Copy AWSAuth if selected
3. Copy Camera if selected

### Phase 7: Git Hooks ✓
1. Pre-commit hook for TOC update
2. install-hooks.sh script

### Phase 8: Certificates (Deferred)
- Certificate management via .gitignore (excludes *.pem, *.key, *.crt, secrets.h)
- Full PKI scripts can be added later if needed

## Files in esp32-bootstrap

```
esp32-bootstrap/
├── esp32-bootstrap              # Main CLI script (Python, executable)
├── requirements.txt             # Python dependencies (questionary)
├── templates/                   # Template files with placeholders
│   ├── README.md.template
│   ├── CLAUDE.md.template
│   ├── gitignore.template
│   ├── tool-versions.template
│   ├── claude-settings.json.template
│   ├── infra/
│   │   ├── bin/infra.ts.template
│   │   ├── lib/iot-device-stack.ts.template
│   │   ├── package.json.template
│   │   ├── tsconfig.json
│   │   └── cdk.json
│   ├── embedded/
│   │   └── app/
│   │       ├── src/main.cpp.template
│   │       ├── include/
│   │       │   └── version.h.template
│   │       ├── platformio.ini.template
│   │       ├── partitions.csv
│   │       └── gitignore.template
│   ├── scripts/
│   │   ├── factory_setup.sh.template
│   │   ├── build_and_flash.sh.template
│   │   ├── build_and_upload.py.template
│   │   └── generate_secrets.py.template
│   ├── project-scripts/
│   │   └── install-hooks.sh.template
│   └── githooks/
│       └── pre-commit.template
├── libraries/                   # Library source code
│   ├── core/                    # Always included
│   │   ├── SDLogger/
│   │   ├── WifiConnect/
│   │   ├── WifiBleService/
│   │   ├── OTAUpdate/
│   │   └── OTABleService/
│   └── optional/                # User-selected
│       ├── AWSAuth/
│       └── Camera/
└── README.md                    # Documentation for the bootstrap tool
```

## Usage

```bash
# Install dependencies
pip install -r requirements.txt

# Create a new project (interactive)
python esp32-bootstrap my-project

# Create a new project (non-interactive, defaults only)
python esp32-bootstrap my-project --no-interactive
```

## Technical Notes

### SDLogger Usage
The SDLogger library uses a singleton pattern. Correct usage:
```cpp
#include "SDLogger.h"

// Initialize
bool success = SDLogger::getInstance().init("/logs");

// Log using macros (not methods)
LOG_I("Info message");
LOG_IF("Formatted: %s %d", str, num);
LOG_E("Error message");
```

### CDK IoT Certificates
AWS IoT only provides the certificate PEM and private key at creation time - they cannot be retrieved later. The `cdk-iot-core-certificates-v3` package uses a Custom Resource Lambda to:
1. Create the IoT Thing and certificate via AWS SDK
2. Capture the PEM/private key from the creation response
3. Store them in SSM Parameter Store
4. Return the ARNs to CloudFormation

### OTABleService Version
The OTABleService library defaults to version "0.0.0" and must be set via `setVersion()` from the main application (which has access to version.h).

---
*Last updated: 2026-02-03*
