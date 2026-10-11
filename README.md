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

**ESPConnect rule:** always use `esp32-n2k-touch-full.bin` at address `0x0`.

Do **not** use `firmware.bin` with ESPConnect. It is the application image only and flashing it at `0x0` will overwrite the bootloader and leave the display unable to boot.

For screenshots, download-mode help, source-code builds and troubleshooting, see the [step-by-step beginner installation guide](docs/beginner-installation.md).

### Updating to a newer release with ESPConnect

If the display is already running an older release, you can update it without VS Code or compiling anything:

1. Open the [latest GitHub Release](https://github.com/cram001/esp32-n2k-touch/releases/latest).
2. Under **Assets**, download the new **`esp32-n2k-touch-full.bin`**.
3. Connect the display to the computer with a USB data cable.
4. Open [ESPConnect](https://thelastoutpostworkshop.github.io/ESPConnect/) in Chrome, Edge, Brave, or another Chromium-based browser.
5. Click **Connect** and select the ESP32-S3 serial device.
6. Open **Flash Firmware**.
7. Select the downloaded **`esp32-n2k-touch-full.bin`**.
8. Set the flash address/offset to **`0x0`**.
9. Start the flash and wait for it to complete.
10. Restart the board with BOOT released, then confirm the new firmware version on the startup screen.

This is the recommended simple USB update method for non-developers. It writes a complete known-good flash layout, so it avoids ambiguity about which OTA application slot is active.

**Important:** the full image is a clean-install/recovery image and can clear saved application settings. Be prepared to re-enter Wi-Fi credentials, SmartShunt keys, page layouts and other saved configuration after the update.

**Do not use `firmware.bin` in ESPConnect.** It is application-only and is reserved for compatible OTA/application-update workflows. Flashing `firmware.bin` at `0x0` overwrites the bootloader and can produce a black/non-booting display.

For ESPConnect, always select **`esp32-n2k-touch-full.bin`** and use **`0x0`**.

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
- Select CAN + SmartShunt BLE, or Wi-Fi Station + W2K-1 TCP; only the selected transports start.
- Use the same Wi-Fi transport for future Cerbo GX control.
- Preserve dual OTA slots and boot validation; wireless OTA is currently disabled.

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
- Wi-Fi Station configuration, masked/showable password, on-demand SSID scan/select and diagnostics.
- Multi-SmartShunt setup with per-device Instant Readout key and optional NMEA 2000 bridging.

Settings are persisted in ESP-IDF NVS.


## Wi-Fi

Select Wi-Fi as the NMEA source to join a 2.4 GHz router and receive W2K-1 TCP data. This profile disables CAN and SmartShunt BLE. Select CAN for wired NMEA 2000 and SmartShunt BLE; Wi-Fi does not start in that profile. Source selection persists and changing transport requires reboot. Access Point mode is unavailable in the current architecture.

Station credentials persist in application NVS. Passwords are masked by default and can be shown explicitly. Open station networks require the Open network checkbox.

Tap Scan Networks for a single scan and select an SSID, or enter a hidden SSID manually. There is no continuous Wi-Fi scanning. The status includes IP address, connection state, signal strength and readable/numeric disconnect reasons; reconnect uses capped backoff.

Existing schema-2 settings migrate without erasing NVS. A protected network requires one password entry on upgrade because the previous version never saved it. The old settings key remains available to the previous firmware.

## OTA updates

Wireless OTA is currently disabled/unreachable in the selected-source architecture. The unchanged 16 MB flash layout retains two 6 MB application slots, OTA metadata and boot validation. The HTTPS/AP upload implementation remains in the source but is not an available update path in this firmware.

Use the USB installation/update instructions above. ESPConnect requires `esp32-n2k-touch-full.bin` at `0x0`; this can clear settings. Do not flash `firmware.bin` at `0x0`. Earlier [Wi-Fi/OTA notes](docs/next-release-wifi-ota.md) describe a historical architecture.

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

In VictronConnect, open the SmartShunt and locate the Instant Readout encryption data under the product information/settings area. Enter the 32 hexadecimal characters into the SmartShunt settings page. Use SELECT NEARBY to bind each configured slot to its device; the stored BLE identity is required for telemetry matching.

### Choose a SmartShunt for each tile

In **Settings > Pages**, open a field, set its source to **SMARTSHUNT**, and choose voltage, current, SOC or another battery metric. Tap the **device-name button** to open **SELECT SMARTSHUNT**, then select the configured slot by number and name. Each tile saves its own selection, so you can show multiple batteries on the same page. Slots remain selectable while offline; missing or expired readings show `--`.

The decoder accepts battery-monitor records containing the required 15 bytes, including longer records, and does not require the product flags byte to equal `0xA0`. Discovery alone does not prove that telemetry is being received. Use **CAN source** for SmartShunt BLE; Wi-Fi source currently disables BLE.

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
