# Development

## Toolchain

Use Visual Studio Code with the PlatformIO extension. The firmware framework is ESP-IDF rather than Arduino.

## First hardware test

1. Connect the Waveshare board by USB.
2. Open the repository in VS Code.
3. Allow PlatformIO to install the Espressif platform and managed ESP-IDF components.
4. Build with `pio run`.
5. Upload with `pio run --target upload`.
6. Open the serial monitor at 115200 baud.

Expected behavior:

- LCD initializes.
- GT911 touch is available through the Waveshare BSP.
- DEPTH placeholder screen appears.
- SETTINGS opens the settings screen.
- DAY and NIGHT change the palette.
- The brightness slider changes the actual LCD backlight.
- Each theme retains its own brightness value after reboot.

## Waveshare baseline

The initial board bring-up is intentionally based on Waveshare's current LVGL v9 ESP-IDF example. It retains their I2C bus recovery sequence before `bsp_display_start()` because quick ESP32 resets can leave another board device holding the shared I2C bus.
