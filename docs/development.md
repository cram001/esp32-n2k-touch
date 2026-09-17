# Development

## Toolchain

- VS Code
- PlatformIO
- ESP-IDF
- C++17

## Build

```bash
pio run
```

Upload:

```bash
pio run --target upload
```

Serial monitor:

```bash
pio device monitor
```

GitHub Actions runs the same `pio run` command on feature branches and pull requests.

## Bench-test sequence

### Display / touch

1. Verify clean boot and LVGL rendering.
2. Verify touch targets.
3. Change Day/Night mode.
4. Change each brightness value independently.
5. Reboot and confirm settings persist.

### SmartShunt BLE

1. In VictronConnect, enable Instant Readout and obtain the encryption key.
2. Enter the 32-character key under Settings > SmartShunt.
3. Optionally enter the SmartShunt MAC address.
4. Enable `Read BLE Instant Readout`.
5. Open the Battery screen.
6. Verify SOC, voltage, current, consumed Ah and time-to-go against VictronConnect.
7. Remove power from or move the SmartShunt out of range and verify the display marks data stale after about 5 seconds.
8. Restore reception and verify the values recover automatically.

### NMEA 2000 battery bridge

Do this first on a bench NMEA/CAN network.

1. Leave `Send battery data to NMEA 2000` disabled and verify no battery PGNs originate from this node.
2. Select the intended battery instance.
3. Ensure no other device is already bridging the same SmartShunt as that instance.
4. Enable the NMEA bridge.
5. Verify PGN 127508 contains SmartShunt voltage/current and temperature when available.
6. Verify PGN 127506 contains SOC and time-to-go.
7. Disable SmartShunt BLE reading and verify NMEA battery transmission stops.
8. Interrupt BLE reception and verify transmission stops when data becomes stale.

## Before permanent vessel installation

- Confirm the physical Waveshare board revision.
- Verify CAN TX/RX and transceiver behavior on the actual board.
- Review galvanic isolation and grounding. The onboard TJA1051 must not simply be assumed to be an isolated NMEA 2000 interface.
- Confirm backbone termination and power arrangements.
- Replace the development-only NMEA manufacturer code before any certification/commercial deployment.
