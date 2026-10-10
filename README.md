# esp32-n2k-touch

Marine touchscreen instrument for the Waveshare **ESP32-S3-Touch-LCD-4**.

## First-time installation

### Recommended for most users: prebuilt firmware + ESPConnect

You do **not** need VS Code, PlatformIO, Git or a compiler just to install the firmware.

1. Open the [latest GitHub Release](https://github.com/cram001/esp32-n2k-touch/releases/latest).
2. Under **Assets**, download **`esp32-n2k-touch-full.bin`**.
3. Connect the Waveshare **ESP32-S3-Touch-LCD-4** to your computer with a USB data cable.
4. Open [ESPConnect](https://thelastoutpostworkshop.github.io/ESPConnect/) in a Chromium-based browser such as Chrome or Edge.
5. Click **Connect** and select the ESP32-S3 serial device.
6. Open **Flash Firmware**, select `esp32-n2k-touch-full.bin`, and set the flash address/offset to **`0x0`**.
7. Start the flash and leave the board connected until ESPConnect reports completion.
8. Disconnect/restart the board with BOOT released.

The **full** image contains the bootloader, partition table, initial OTA metadata and application. It is intended for first installation or clean recovery. Installing the full image can clear saved application settings.

Do **not** substitute `firmware.bin` for the full image on a brand-new board. `firmware.bin` is the application image only.

For screenshots, download-mode help, source-code builds and troubleshooting, see the [step-by-step beginner installation guide](docs/beginner-installation.md).

### Developers / custom builds

Developers who want to modify or compile the project can use VS Code + PlatformIO as described in [Development](docs/development.md).

## 3D printed enclosure

Wall mounted case 3D printed:  https://cults3d.com/en/3d-model/home/flush-mount-wall-case-for-waveshare-esp32-s3-touch-lcd-4-instrument-display


## Functions

- Receive NMEA 2000 over the onboard CAN/TWAI interface.
- Display water depth from NMEA 2000 PGN 128267 (next depth milestone).
- Day/night display themes with separately stored brightness levels.
- Receive Victron SmartShunt **Instant Readout** BLE advertisements, with active scanning for device names.
- Display SmartShunt voltage, current, SOC, consumed Ah and time-to-go.
- Optionally bridge SmartShunt battery data onto NMEA 2000 using a selectable battery instance.
- Connect to Wi-Fi in station or Access Point mode; separate credentials persist in application NVS.
- Use the same Wi-Fi transport for future Cerbo GX control.
- Dual-slot HTTPS pull and local AP-upload OTA with startup validation and rollback support.

## Development stack

- Visual Studio Code
- PlatformIO
- ESP-IDF
- LVGL 9.5
- Waveshare ESP32-S3-Touch-LCD-4 BSP 3.0.0
- NMEA2000 library + ESP32 TWAI transport

## Current UI

The firmware provides six configurable instrument pages with 1, 2, 4, or 6 data fields per page. Fields can mix NMEA 2000 and SmartShunt sources.

The Settings area includes:

- Page enable/layout/data-field configuration.
- Global units for depth, temperature, wind speed, vessel speed, long distance and short distance.
- Day/Night mode and independent brightness.
- Wi-Fi Station/AP selection, masked/showable password, on-demand SSID scan/select, diagnostics and firmware updates.
- Multi-SmartShunt setup with per-device Instant Readout key and optional NMEA 2000 bridging.

Settings are persisted in ESP-IDF NVS.


## Wi-Fi

Choose Station to join a 2.4 GHz router, or Access Point for a direct local firmware upload. Station and AP credentials are stored separately in application NVS and survive reboot. Passwords are masked by default and can be shown explicitly. Open station networks require the Open network checkbox.

Tap Scan Networks for a single scan and select an SSID, or enter a hidden SSID manually. There is no continuous Wi-Fi scanning. The status includes IP address, connection state, signal strength and readable/numeric disconnect reasons; reconnect uses capped backoff.

Existing schema-2 settings migrate without erasing NVS. A protected network requires one password entry on upgrade because the previous version never saved it. The old settings key remains available to the previous firmware.

## OTA updates

The unchanged 16 MB flash layout provides two 6 MB application slots and OTA metadata. Under Wi-Fi > Firmware Update, enter an HTTPS application-image URL while connected in Station mode. In AP mode, connect a phone/laptop to the display and open its displayed HTTP address to upload `firmware.bin` directly. Both methods validate the image and require an explicit reboot.

A candidate image is confirmed after local services initialize and a five-second UI heartbeat check succeeds. A pending image that fails health checks is rejected; a crash/reset before confirmation permits bootloader rollback to a valid previous slot. On-board rollback and radio/touch behavior require bench acceptance.

See [release notes and bench checklist](docs/next-release-wifi-ota.md) for setup, migration, validation and future gesture/Actisense planning.

## SmartShunt Instant Readout

The ESP32 does not create a BLE connection to the SmartShunt. It scans Victron manufacturer advertisements and requests scan-response names, identifies battery-monitor Instant Readout records, decrypts them with the key supplied by VictronConnect, and decodes the published battery-monitor fields. The nearby-device picker refreshes while open and can discover Victron devices before a key is entered. Discovery does not imply that a device supports the decoded battery-monitor record.

Displayed values:

- Battery voltage
- Battery current
- State of charge
- Consumed/depleted Ah
- Time-to-go
- Battery temperature when the SmartShunt auxiliary input is configured for temperature
- BLE RSSI and stale-data status

Configured battery capacity is intentionally not required because it is not present in the Instant Readout battery-monitor record.

### Finding the Instant Readout key

In VictronConnect, open the SmartShunt and locate the Instant Readout encryption data under the product information/settings area. Enter the 32 hexadecimal characters into the SmartShunt settings page. The MAC filter is optional when only one Victron battery monitor is in BLE range, but is recommended on installations with multiple monitors.

## SmartShunt -> NMEA 2000

The bridge is **off by default**. Enable it explicitly in SmartShunt settings to avoid creating a duplicate battery source when another device (for example a Cerbo GX) is already transmitting that battery onto NMEA 2000.

Current mapping:

| SmartShunt field | NMEA 2000 |
| --- | --- |
| Voltage | PGN 127508 Battery Status |
| Current | PGN 127508 Battery Status |
| Temperature, if available | PGN 127508 Battery Status |
| SOC | PGN 127506 DC Detailed Status |
| Time-to-go | PGN 127506 DC Detailed Status |
| Consumed Ah | Local display only |
| Capacity | Not transmitted / NA |

The selected battery instance is used for both PGNs. Fields reported by Victron as unavailable are transmitted as NMEA 2000 NA rather than as zero.

## Hardware baseline

This project targets the Waveshare ESP32-S3-Touch-LCD-4 and follows Waveshare's current first-party BSP examples:

- ESP32-S3
- 16 MB flash
- 8 MB PSRAM
- 480 x 480 touch display
- Onboard TJA1051 CAN transceiver
- CAN/TWAI TX: GPIO6
- CAN/TWAI RX: GPIO0

**Marine installation note:** the onboard CAN transceiver is not treated as a certified isolated NMEA 2000 interface. Bench testing is appropriate, but permanent vessel installation should review galvanic isolation, grounding, backbone power and NMEA 2000 physical-layer requirements.

## Prebuilt firmware releases

Published releases are available from the [GitHub Releases page](https://github.com/cram001/esp32-n2k-touch/releases). New releases automatically attach:

- **`esp32-n2k-touch-full.bin`** — merged first-install/recovery image for ESPConnect; flash at **`0x0`**.
- **`firmware.bin`** — application image for compatible application/OTA update workflows.
- **`SHA256SUMS.txt`** — checksums for verifying downloads.
- **`commit.txt`** — exact source revision used for the build.

The release binaries are built and validated by GitHub Actions from the release tag, so ordinary users do not need a local build environment.

## Build from source

The [release plan](docs/release-plan.md) tracks completed work, heading reference
selection and the remaining wireless transport/gesture proposals.

Install VS Code and the PlatformIO extension, clone the repository, and open the repository folder.

For a walkthrough with every step explained, see [beginner installation](docs/beginner-installation.md).

```bash
pio run
```

Upload:

```bash
pio run --target upload
```

Monitor:

```bash
pio device monitor
```

GitHub Actions also runs `pio run` for feature branches and pull requests.

## Upstream references

- Waveshare product: https://www.waveshare.com/esp32-s3-touch-lcd-4.htm
- Waveshare documentation: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4
- Waveshare examples: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-4
- NMEA2000 library: https://github.com/ttlappalainen/NMEA2000
- ESP32 TWAI transport: https://github.com/jiauka/NMEA2000_esp32xx
