# Architecture

## Goals

The firmware is being built as a marine instrument rather than a single-screen demo. Hardware access, UI, NMEA 2000 processing, networking, and Victron control will remain separated so each can be tested independently.

## Planned modules

```text
Application
├── UI / LVGL
│   ├── Depth screen
│   └── Settings screen
├── Settings / NVS
├── NMEA 2000
│   ├── TWAI transport
│   ├── NMEA 2000 stack
│   └── PGN 128267 depth service
├── Network
│   ├── Wi-Fi
│   └── MQTT
└── Victron
    ├── Cerbo GX discovery
    ├── VE.Bus state monitor
    └── inverter command service
```

## Display behavior

Milestone 1 stores these settings in NVS:

- Theme: Day or Night.
- Day brightness: 1-100%.
- Night brightness: 1-100%.

Brightness is applied using the Waveshare BSP instead of a hard-coded ESP32 GPIO because the board backlight is controlled through the onboard CH32 helper controller.

## NMEA 2000 plan

Milestone 2 will:

1. Bring up the board TWAI interface at the NMEA 2000 bitrate of 250 kbit/s.
2. Verify receive-only CAN traffic first.
3. Integrate the NMEA 2000 protocol stack.
4. Parse PGN 128267 Water Depth.
5. Implement stale-data handling before presenting live depth as valid.

The physical connection to the vessel backbone will be reviewed separately for NMEA 2000 isolation and termination requirements.

## Victron plan

The ESP32 will join the vessel Wi-Fi network and communicate with the Cerbo GX locally. Inverter commands must be acknowledged from the Cerbo before the UI changes to the requested state.

Initial control scope:

- OFF
- INVERTER ONLY

No Victron identifiers will be hard-coded where discovery can be used instead.
