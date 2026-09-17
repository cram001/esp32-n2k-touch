# Architecture

## Platform

- Waveshare ESP32-S3-Touch-LCD-4
- ESP-IDF under PlatformIO
- LVGL 9.5 through the Waveshare BSP
- ESP32 NVS for persistent settings
- ESP32 BLE GAP passive scanner for Victron Instant Readout
- NMEA2000 library with ESP32 TWAI transport

## Runtime services

### UI / display

The Waveshare BSP owns display, touch and backlight initialization. LVGL screens are created after `bsp_display_start()` while the BSP display lock is held.

The current UI contains:

- Depth screen
- SmartShunt battery screen
- General display settings
- SmartShunt/BLE/NMEA bridge settings

Day and Night modes have independent brightness values. Brightness changes are sent through `bsp_display_brightness_set()` because the board backlight is controlled through the Waveshare helper/IO-expander path rather than a normal ESP32 PWM GPIO.

### SmartShunt BLE service

`smartshunt_ble` passively scans BLE advertisements. It does not create a GATT connection.

The service:

1. Filters Victron manufacturer data (`0x02E1`).
2. Selects Product Advertisement / Battery Monitor Instant Readout records.
3. Optionally filters on the configured SmartShunt MAC address.
4. Validates the first encryption-key byte exposed in the record header.
5. Decrypts the 16-byte battery-monitor payload using AES-CTR and the user-supplied Instant Readout key.
6. Converts Victron scale factors and NA values into an internal `SmartShuntData` snapshot.
7. Marks data stale after 5 seconds without a valid battery-monitor advertisement.

The decoded snapshot is shared with the UI and the NMEA bridge; neither layer parses BLE packets directly.

### NMEA 2000 service

`n2k_bridge` owns the NMEA2000/TWAI object and continuously calls `ParseMessages()`. This gives the project one CAN/NMEA owner that can later also dispatch incoming PGN 128267 depth messages.

Current SmartShunt output:

- PGN 127508: voltage, current, optional temperature
- PGN 127506: SOC and time remaining

Capacity, state of health and ripple are sent as unavailable when not known. Consumed Ah remains a local-display value because there is no direct standard field for it in these battery PGNs.

SmartShunt-to-NMEA output defaults off to avoid duplicate battery sources.

## Persistent settings

`AppSettings` is stored in NVS and currently contains:

- Display theme
- Day brightness
- Night brightness
- SmartShunt read enable
- SmartShunt-to-NMEA enable
- NMEA battery instance
- Optional SmartShunt MAC filter
- SmartShunt Instant Readout encryption key

## Planned services

### Depth

Incoming NMEA 2000 PGN 128267 will update a dedicated depth-state object. The UI will enforce stale-data timeout behavior instead of leaving an old depth value displayed indefinitely.

### Cerbo GX / inverter

A network service will join the vessel Wi-Fi and use the Cerbo GX local API/MQTT path to read and command the VE.Bus inverter mode. UI state changes will be based on confirmed Cerbo feedback rather than optimistic button state.

### OTA

The flash layout reserves two OTA application slots. OTA transport and update UI remain to be implemented.

## Concurrency rules

- LVGL is changed only while the display/LVGL context is safe.
- BLE callbacks decode into a protected data snapshot rather than directly changing UI objects.
- NMEA output reads copies of settings and SmartShunt state.
- Settings changes are persisted, then applied to the relevant runtime service.

## Installation constraints

The current Waveshare board uses an onboard TJA1051 transceiver on GPIO6/GPIO0. It must not automatically be treated as a certified isolated NMEA 2000 interface. Permanent vessel installation requires a separate review of galvanic isolation, grounding, backbone power and physical-layer compliance.
