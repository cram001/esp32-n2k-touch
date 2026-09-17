# Victron SmartShunt BLE integration

## Transport

The firmware uses Victron **Instant Readout** BLE manufacturer advertisements. It does not open or hold a GATT connection to the SmartShunt.

This is intentional:

- VictronConnect can still connect normally.
- The ESP32 only listens for advertisements.
- No polling interval or reconnect loop is required.
- BLE traffic is limited to passive scanning and decoding relevant packets.

## Configuration

SmartShunt support is disabled by default.

Settings provide:

1. **Read BLE Instant Readout** — enables use of decoded SmartShunt advertisements.
2. **Send battery data to NMEA 2000** — enables the battery bridge. This is forced off when SmartShunt reading is disabled.
3. **Battery instance** — 0 through 252.
4. **MAC address** — optional device filter. Recommended when multiple Victron battery monitors are in range.
5. **Instant Readout key** — 128-bit AES key represented as 32 hexadecimal characters.

The key is stored in ESP-IDF NVS. It is not printed to normal application logs.

## Decoded battery-monitor fields

The 16-byte decrypted battery monitor record contains:

- Time-to-go
- Battery voltage
- Alarm reason
- Auxiliary input value/type
- Battery current
- Consumed Ah
- State of charge

Victron-specific NA sentinels are retained as unavailable values. The UI displays `--` for unavailable fields and the NMEA bridge sends the corresponding NMEA NA representation where applicable.

The auxiliary field is exposed as battery temperature only when the SmartShunt reports the auxiliary input type as temperature.

## Stale data

A successful battery-monitor advertisement records its receive timestamp. Data older than 5 seconds is marked stale and is no longer transmitted onto NMEA 2000.

This prevents an old battery state from continuing to be announced if the SmartShunt goes out of range, powers down, Bluetooth is disabled, or the key/configuration changes.

## NMEA 2000 mapping

### PGN 127508 — Battery Status

- Instance: configured battery instance
- Voltage: SmartShunt battery voltage
- Current: SmartShunt battery current
- Temperature: SmartShunt auxiliary temperature when available
- SID: rolling sequence identifier

### PGN 127506 — DC Detailed Status

- Instance: configured battery instance
- DC type: Battery
- State of charge: SmartShunt SOC
- State of health: NA
- Time remaining: SmartShunt time-to-go
- Ripple voltage: NA
- Capacity: NA

Consumed Ah is displayed locally but is not mapped to a non-standard proprietary PGN.

## Duplicate-source protection

The NMEA bridge defaults to **off**. Do not enable it if the same SmartShunt is already being bridged to NMEA 2000 by another device unless the installations use deliberately different battery instances.

## CAN hardware

Current Waveshare hardware routes the onboard TJA1051 CAN transceiver to:

- TX: GPIO6
- RX: GPIO0

The firmware configures NMEA 2000 at the transport layer through the ESP32 TWAI driver used by `NMEA2000_esp32xx`.

The onboard transceiver should not be assumed to provide certified galvanic isolation for permanent NMEA 2000 installation. Review isolation, common grounding, backbone power, connector wiring and NMEA 2000 physical-layer requirements before vessel installation.
