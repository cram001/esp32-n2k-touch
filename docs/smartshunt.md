# Victron SmartShunt BLE integration

## Transport

The firmware uses Victron **Instant Readout** BLE manufacturer advertisements. It does not open or hold a GATT connection to a SmartShunt.

The scanner runs continuously so the UI can discover nearby Victron battery monitors and configured devices can update in the background. Live battery data remains advertisement-based and does not require a connection.

## Multiple SmartShunts

Up to four SmartShunts can be configured independently.

Each configured device stores:

- Friendly/device name shown in the UI
- BLE identity used internally to match future advertisements
- Instant Readout encryption key
- Read enabled/disabled state
- NMEA 2000 bridge enabled/disabled state
- NMEA 2000 battery instance

The BLE hardware address is an internal stable identifier. Normal setup and operation do **not** require the user to type or select a MAC address.

## Name-based discovery

Use **Settings > SmartShunts > Add SmartShunt**.

The ESP32 lists nearby Victron battery-monitor advertisements by their advertised Bluetooth name. Signal strength is shown as secondary information. Tapping a name binds that physical device internally and opens its editor.

If an advertisement does not contain a readable local name, the UI creates a fallback `SmartShunt ####` label for selection. The stored display name can then be changed in the editor.

After selecting a device:

1. Confirm or edit its display name.
2. Enter the 32-character Instant Readout encryption key from VictronConnect.
3. Enable or disable reading that SmartShunt.
4. Optionally enable NMEA 2000 transmission.
5. Select its battery instance.

The key is stored in ESP-IDF NVS and is not printed in normal logs.

## Battery pages

Each configured SmartShunt has its own logical battery page. The battery screen displays the selected device name at the top and provides previous/next controls to move between configured devices.

Each page shows, when available:

- State of charge
- Battery voltage
- Battery current
- Consumed Ah
- Time-to-go
- BLE RSSI/status
- NMEA 2000 bridge state and battery instance

## Decoded battery-monitor fields

The 16-byte decrypted battery-monitor record contains:

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

Each SmartShunt has its own receive timestamp. Data older than 5 seconds is marked stale and that device stops transmitting onto NMEA 2000 until fresh data arrives.

Discovery entries age out of the nearby-device list after 15 seconds.

## NMEA 2000 mapping

Every SmartShunt has an independent bridge toggle and battery instance.

### PGN 127508 — Battery Status

- Instance: configured battery instance
- Voltage: SmartShunt battery voltage
- Current: SmartShunt battery current
- Temperature: SmartShunt auxiliary temperature when available
- SID: rolling per-device sequence identifier

### PGN 127506 — DC Detailed Status

- Instance: configured battery instance
- DC type: Battery
- State of charge: SmartShunt SOC
- State of health: NA
- Time remaining: SmartShunt time-to-go
- Ripple voltage: NA
- Capacity: NA

Consumed Ah is displayed locally but is not mapped to a non-standard proprietary PGN.

## Duplicate-instance protection

If two configured SmartShunts are both enabled for NMEA 2000 with the same battery instance, the firmware suppresses transmission for the conflicting instance and reports the conflict on the battery page. This prevents two different batteries from being announced as the same NMEA battery instance.

This protection does not detect an external device such as a Cerbo GX transmitting the same SmartShunt, so the bridge still defaults off for newly added devices.

## CAN hardware

Current Waveshare hardware routes the onboard TJA1051 CAN transceiver to:

- TX: GPIO6
- RX: GPIO0

The onboard transceiver should not be assumed to provide certified galvanic isolation for permanent NMEA 2000 installation. Review isolation, common grounding, backbone power, connector wiring and NMEA 2000 physical-layer requirements before vessel installation.
