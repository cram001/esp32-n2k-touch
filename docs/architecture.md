# Architecture

## Platform

- Waveshare ESP32-S3-Touch-LCD-4
- ESP-IDF under PlatformIO
- LVGL 9.5 through the Waveshare BSP
- ESP32 NVS for persistent settings
- ESP32 BLE GAP active scanner for Victron Instant Readout
- NMEA2000 library with a vendored ESP-IDF/TWAI transport derived from jiauka/NMEA2000_esp32xx

## Runtime services

### UI / display

The Waveshare BSP owns display, touch and backlight initialization. LVGL screens are created after `bsp_display_start()` while the BSP display lock is held.

The current UI contains six configurable instrument pages, units/settings screens, Wi-Fi setup/status, and multi-SmartShunt setup.

Day and Night modes have independent brightness values. Brightness changes are sent through `bsp_display_brightness_set()` because the board backlight is controlled through the Waveshare helper/IO-expander path rather than a normal ESP32 PWM GPIO.

### SmartShunt BLE service

`smartshunt_ble` uses active scanning for names and advertisement-based telemetry, without a GATT connection. It starts only in CAN-source mode. Wi-Fi-source mode starts Station/W2K-1 with CAN and BLE disabled.

The service:

1. Filters Victron manufacturer data (`0x02E1`).
2. Selects Product Advertisement / Battery Monitor or Solar Charger Instant Readout records.
3. Matches the BLE identity bound to each configured SmartShunt slot.
4. Validates the first encryption-key byte exposed in the record header.
5. Decrypts the required 15 bytes of the battery-monitor payload using AES-CTR and the user-supplied Instant Readout key.
6. Converts Victron scale factors and NA values into an internal `SmartShuntData` snapshot.
7. Marks data stale after 5 seconds without a valid supported advertisement.

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

### Cerbo GX / inverter

A network service will join the vessel Wi-Fi and use the Cerbo GX local API/MQTT path to read and command the VE.Bus inverter mode. UI state changes will be based on confirmed Cerbo feedback rather than optimistic button state.

### OTA

The flash layout provides two OTA application slots. HTTPS OTA transport, project/secure-version validation, startup-health confirmation and bootloader rollback are implemented. Wireless OTA and AP mode are currently unavailable in the selected-source architecture. The OTA implementation remains in the source; `next-release-wifi-ota.md` describes an earlier architecture.

## Concurrency rules

- LVGL is changed only while the display/LVGL context is safe.
- BLE callbacks decode into a protected, generation-checked data snapshot rather than directly changing UI objects.
- NMEA input values are published into a synchronized cache; the UI reads snapshots.
- NMEA output reads copies of settings and SmartShunt state.
- Settings changes are persisted, then applied to the relevant runtime service.
- Wi-Fi configuration and scans are queued to a dedicated worker; scan records are processed outside the event-loop stack. Wi-Fi events publish protected status snapshots.
- HTTP/HTTPS OTA tasks publish status snapshots and share an update claim with network reconfiguration; they never access LVGL.

## Installation constraints

The current Waveshare board uses an onboard TJA1051 transceiver on GPIO6/GPIO0. It must not automatically be treated as a certified isolated NMEA 2000 interface. Permanent vessel installation requires a separate review of galvanic isolation, grounding, backbone power and physical-layer compliance.

### Solar-charger BLE records

The existing BLE scanner also accepts Instant Readout record 0x01. It decrypts
at least 10 bytes through the PV-power field, using signed 16-bit battery-side
voltage/current (0.01 V / 0.1 A) and unsigned PV power (1 W), with individual NA
checks. Runtime record type identifies MPPT versus battery monitor; persisted
slot/configuration structures are unchanged. Solar metrics are appended to the
metric enum to preserve saved IDs. Nearby discovery admits only supported
0x01/0x02 records; known-device name responses still refresh names. NMEA2000
bridging remains restricted to record 0x02; MPPT support is display-only.
