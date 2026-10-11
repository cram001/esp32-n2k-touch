# Victron SmartShunt BLE integration

## Transport

The firmware uses Victron **Instant Readout** BLE manufacturer advertisements. It does not open or hold a GATT connection to a SmartShunt.

In CAN-source mode, the scanner runs continuously so the UI can discover nearby Victron devices and configured monitors can update in the background. Active scanning requests Bluetooth scan-response names; it does not establish a GATT connection. Live battery data remains advertisement-based. Wi-Fi scans remain user-triggered.

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

Use **Settings > Victron BLE > Add Victron BLE**.

Open a SmartShunt slot and tap **SELECT NEARBY**. The ESP32 lists nearby supported Victron Instant Readout advertisements by their advertised Bluetooth name and signal strength. The list refreshes while open; it stays stable during a touch to avoid selecting a different device on release. Tapping a name binds that physical device internally and opens its editor.

If an advertisement does not contain a readable local name, the UI creates a fallback `Victron ####` label. A complete name received later is retained even when subsequent advertisements omit it or provide only an abbreviated name. The stored display name can be changed in the editor.

Discovery does not require an encryption key. It does require enabled Instant
Readout and a supported battery-monitor (0x02) or solar-charger (0x01) record.
Cerbo/GX, setup-only advertisements and other unsupported devices are excluded.
The four slots are shared by SmartShunts and compatible SmartSolar MPPT chargers.
Confirm key entry and live readings on the physical device; host packet tests
cannot verify radio reception or coexistence.

After selecting a device:

1. Confirm or edit its display name.
2. Enter the 32-character Instant Readout encryption key from VictronConnect.
3. Enable or disable reading that SmartShunt.
4. Optionally enable NMEA 2000 transmission.
5. Select its battery instance.

The key is stored in ESP-IDF NVS and is not printed in normal logs.

## Instrument tiles

Open Settings > Pages, choose a field, set its source type to VICTRON BLE and choose the metric. Tap the device-name button to open SELECT VICTRON BLE, then select the configured slot by number and name. Each field stores its own device selection, so different shunts can appear together on one page. Devices remain selectable without live telemetry; missing data displays `--`. BLE is disabled in Wi-Fi-source mode.

Each page shows, when available:

- State of charge
- Battery voltage
- Battery current
- Consumed Ah
- Time-to-go
- BLE RSSI/status
- NMEA 2000 bridge state and battery instance

## Decoded battery-monitor fields

The battery-monitor fields occupy 118 bits (15 bytes). Accept 15-byte records and longer records without requiring AES block padding. The product/readout flags byte is not treated as a fixed `0xA0`; manufacturer product record `0x10` and battery-monitor record `0x02` identify supported telemetry. See [Victron’s published field layout](https://communityarchive.victronenergy.com/storage/attachments/extra-manufacturer-data-2022-12-14.pdf).

The decrypted battery-monitor record contains:

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

## MPPT display support

Select a configured MPPT for each tile and choose Solar V, Solar A or Solar W.
Solar V/A are the charger's battery-side voltage/current; Solar W is reported
PV input power, not V multiplied by A. Panel voltage/current, SOC and time-to-go
are not supplied by the solar-charger record. Existing Battery V/A tiles also
work and acquire solar labels when a solar record is decoded. MPPT reception
uses its own 32-character Instant Readout key and is display-only: Shunt to N2K
applies only to battery-monitor records. Missing/NA values and display data aged
30 seconds show `--`; valid zero is retained.
