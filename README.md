# esp32-bootstrap

A CLI tool to bootstrap new ESP32-S3-CAM projects with a standardized structure, ready to build and deploy via OTA using the sandboxsite.

## Installation

```bash
pip install -r requirements.txt
```

## Usage

```bash
# Interactive mode (prompts for optional libraries)
python esp32-bootstrap my-project

# Non-interactive mode (uses defaults)
python esp32-bootstrap my-project --no-interactive
```

The project name should be in kebab-case (lowercase with hyphens). It will be converted to PascalCase for AWS/CDK identifiers.

## Generated Project Structure

```
<project-name>/
├── README.md                    # Project documentation
├── CLAUDE.md                    # AI assistant context
├── .gitignore                   # Comprehensive ignore file
├── .tool-versions               # asdf: python 3.11.14
├── .claude/
│   └── settings.local.json      # Claude Code permissions
│
├── scripts/
│   └── install-hooks.sh         # Git hooks installer
│
├── infra/                       # AWS CDK infrastructure
│   ├── bin/infra.ts
│   ├── lib/iot-device-stack.ts  # IoT Thing + certificates
│   ├── package.json
│   ├── tsconfig.json
│   └── cdk.json
│
├── githooks/
│   └── pre-commit               # README TOC auto-update
│
└── embedded/
    └── <project-name>/
        ├── src/main.cpp
        ├── include/
        │   ├── version.h
        │   └── secrets.h        # Generated from SSM
        ├── lib/                  # Libraries
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
        └── partitions.csv
```

## Core Libraries

| Library | Purpose |
|---------|---------|
| SDLogger | Async SD card logging with FreeRTOS |
| WifiConnect | WiFi with NVS credentials storage |
| WifiBleService | BLE WiFi configuration |
| OTAUpdate | Direct-to-flash OTA downloads |
| OTABleService | BLE OTA service (sandboxsite compatible) |

## Optional Libraries

| Library | Purpose |
|---------|---------|
| AWSAuth | SigV4 signing + credentials provider |
| Camera | ESP32-S3-CAM capture with PSRAM |

## Getting Started

After creating a project:

```bash
cd my-project

# Deploy AWS infrastructure
cd infra && npm install && cdk deploy

# Generate secrets from SSM
cd ../embedded/my-project/scripts
python generate_secrets.py

# Flash device
./factory_setup.sh
```

## Requirements

- Python 3.11+
- AWS CLI configured with `nakom.is-sandbox` profile
- PlatformIO
- Node.js (for CDK)

## License

CC0
