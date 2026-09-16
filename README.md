# esp32-n2k-touch

Marine touchscreen instrument for the Waveshare **ESP32-S3-Touch-LCD-4**.

Planned functions:

- Receive NMEA 2000 data over the onboard CAN/TWAI interface.
- Display water depth from NMEA 2000 PGN 128267.
- Day/night display themes with separately stored brightness levels.
- Connect by Wi-Fi to a Victron Cerbo GX.
- Read and control a Victron VE.Bus inverter state through the Cerbo GX.
- Support OTA firmware updates.

## Development stack

- Visual Studio Code
- PlatformIO
- ESP-IDF
- LVGL 9
- Waveshare ESP32-S3-Touch-LCD-4 BSP

## Milestone 1

The initial firmware brings up the Waveshare display/touch BSP and implements:

- DEPTH placeholder screen.
- Settings screen.
- Day and Night themes.
- Independent day/night brightness values.
- Backlight control through the Waveshare BSP.
- Persistent settings using ESP-IDF NVS.
- OTA-capable partition layout reserved from the start.

NMEA 2000 and Victron communications are intentionally not enabled in Milestone 1.

## Hardware baseline

This project currently targets the Waveshare ESP32-S3-Touch-LCD-4 and follows Waveshare's current first-party BSP examples:

- BSP: `waveshare/esp32_s3_touch_lcd_4` 3.0.0
- LVGL: 9.5.0
- ESP32-S3, 16 MB flash, octal PSRAM

Waveshare hardware revisions have changed. Verify the PCB revision before relying on revision-specific CAN or power wiring details.

## Build

Install VS Code and the PlatformIO extension, clone the repository, then open the repository folder in VS Code.

From a PlatformIO terminal:

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

The first build downloads the ESP-IDF platform and the managed Waveshare/LVGL components.

## Upstream hardware references

- Product: https://www.waveshare.com/esp32-s3-touch-lcd-4.htm
- Documentation: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4
- First-party examples: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-4
