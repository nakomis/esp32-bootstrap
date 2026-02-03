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
│   │   └── iot-device-stack.ts  # IoT Thing + certificates
│   ├── package.json
│   ├── tsconfig.json
│   └── cdk.json
│
├── certs/                       # Local HTTPS certificates
│   ├── generate_certs.sh        # Generate CA-signed certs
│   ├── new_server_cert.sh       # Add new server cert
│   ├── s3sync.sh                # Sync certs to S3
│   └── trust-ca.sh              # macOS keychain trust
│
├── githooks/                    # Git hooks
│   └── pre-commit               # README TOC auto-update
│
└── embedded/
    └── <project-name>/          # Main application (bootloader downloaded from S3)
        ├── src/main.cpp         # Minimal working example
        ├── include/
        │   ├── version.h
        │   ├── secrets.h.template
        │   └── README
        ├── lib/                 # Core libraries (always included)
        │   ├── SDLogger/
        │   ├── WifiConnect/
        │   ├── OTAUpdate/
        │   └── OTABleService/
        ├── scripts/
        │   ├── factory_setup.sh
        │   ├── build_and_flash.sh
        │   ├── build_and_upload.py
        │   └── generate_secrets.py
        ├── platformio.ini
        ├── partitions.csv
        ├── AmazonRootCA1.pem
        └── .gitignore
```

## Key Components

### 1. README.md with Auto-TOC
- Skeleton README with sections for the user to fill in
- Git pre-commit hook to auto-update table of contents
- **Decision needed:** Use `markdown-toc` npm package or custom script?

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
Based on bootboots/infra/lib/iot-device.ts:
- Creates IoT Thing with auto-generated certificate
- Saves cert/key to SSM Parameter Store
- Creates IAM role for credentials provider
- Creates IoT policy for MQTT (connect, subscribe, publish)
- Role alias for temporary credentials

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

### 8. Certificate Management

Based on csotp/certs:
- EC (prime256v1) certificates for efficiency on embedded
- 10-year validity
- CA certificate for local signing
- generate_certs.sh creates full PKI
- new_server_cert.sh adds device certs
- s3sync.sh syncs to S3 bucket
- trust-ca.sh adds CA to macOS keychain
- Generates server_certs.h for embedding

### 9. Bootloader (Separate Repo)

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

### 10. Partition Table (16MB Flash)

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
- AWSAuth library
- Camera library
- Certificate management scripts (from csotp)

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

## Files to Create in esp32-bootstrap

```
esp32-bootstrap/
├── esp32-bootstrap              # Main CLI script (Python, executable)
├── templates/                   # Template files with placeholders
│   ├── README.md.template
│   ├── CLAUDE.md.template
│   ├── gitignore.template
│   ├── tool-versions.template
│   ├── infra/
│   │   ├── bin/infra.ts.template
│   │   ├── lib/iot-device-stack.ts.template
│   │   ├── package.json.template
│   │   ├── tsconfig.json
│   │   └── cdk.json
│   ├── embedded/
│   │   └── app/                 # Main application template
│   │       ├── src/main.cpp.template
│   │       ├── include/
│   │       │   ├── version.h.template
│   │       │   └── secrets.h.template
│   │       ├── platformio.ini.template
│   │       ├── partitions.csv
│   │       └── gitignore.template
│   ├── scripts/
│   │   ├── factory_setup.sh.template   # Downloads bootloader from S3
│   │   ├── build_and_flash.sh.template
│   │   ├── build_and_upload.py.template
│   │   └── generate_secrets.py.template
│   └── githooks/
│       └── pre-commit.template
├── libraries/                   # Library source code
│   ├── core/                    # Always included
│   │   ├── SDLogger/
│   │   ├── WifiConnect/
│   │   ├── WifiBleService/      # NEW: BLE WiFi configuration
│   │   ├── OTAUpdate/
│   │   └── OTABleService/
│   └── optional/                # User-selected
│       ├── AWSAuth/
│       └── Camera/
└── README.md                    # Documentation for the bootstrap tool itself
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

---
*Last updated: 2026-02-03*
