# esp32-n2k-touch

Marine touchscreen instrument for the Waveshare **ESP32-S3-Touch-LCD-4**.

## Functions

- Receive NMEA 2000 over the onboard CAN/TWAI interface.
- Display water depth from NMEA 2000 PGN 128267 (next depth milestone).
- Day/night display themes with separately stored brightness levels.
- Passively receive Victron SmartShunt **Instant Readout** BLE advertisements.
- Display SmartShunt voltage, current, SOC, consumed Ah and time-to-go.
- Optionally bridge SmartShunt battery data onto NMEA 2000 using a selectable battery instance.
- Connect to Wi-Fi in station mode using settings stored in NVS.
- Use the same Wi-Fi transport for future Cerbo GX control.
- Dual-slot HTTPS OTA update engine with boot validation and rollback support.

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
- Wi-Fi station configuration with SSID, masked password, connection state, IP address and RSSI.
- Multi-SmartShunt setup with per-device Instant Readout key and optional NMEA 2000 bridging.

Settings are persisted in ESP-IDF NVS.


## Wi-Fi

Wi-Fi uses the ESP-IDF station interface. The touchscreen stores:

- Enable/disable state
- SSID
- Password

The status screen reports connection state, IP address and RSSI. The Wi-Fi connection is shared by network features such as OTA and the planned Cerbo GX integration.

## OTA updates

The board has 16 MB flash and the project partition table already provides two 6 MB OTA application slots plus OTA metadata. Firmware updates use ESP-IDF HTTPS OTA APIs and the system is configured for bootloader rollback.

A newly installed image is only marked valid after the display and core application services finish initialization. If the new image crashes or resets before validation, the bootloader can return to the previous application slot.

The OTA download engine is implemented. The user-facing update-source/release selection workflow is still to be added before OTA is considered complete for field use.

## SmartShunt Instant Readout

The ESP32 does not create a BLE connection to the SmartShunt. It passively scans Victron manufacturer advertisements, identifies battery-monitor Instant Readout records, decrypts them with the key supplied by VictronConnect, and decodes the published battery-monitor fields.

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

## Build

Install VS Code and the PlatformIO extension, clone the repository, and open the repository folder.

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
