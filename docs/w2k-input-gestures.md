# W2K-1 input and touch gestures

Settings > NMEA Input selects **wired CAN** or **W2K-1 TCP**. Existing installations
default to wired CAN. The gateway IPv4 address and TCP port are editable and saved
in application NVS independently of existing Wi-Fi, page, unit and SmartShunt blobs.

## Gateway setup

1. Join the W2K-1 access point, or connect both devices to the same Wi-Fi network.
2. In the W2K-1 web interface, open Settings > Data-Server Settings. Configure a
   server for **TCP**, **N2K ASCII**, **Transmit Only**. Note its TCP port.
3. On the display, open Settings > NMEA Input. Select W2K-1 TCP, enter the gateway
   IPv4 address and matching port, and press Save. Port 60001 is an editable default;
   the selected gateway server determines the actual port.

This release receives complete PGNs in Actisense's documented N2K ASCII format.
It does not decode NMEA 0183, RAW CAN, NGT binary or BST binary formats. Configure
the gateway format explicitly; a TCP connection alone does not prove valid data.
The setup page reports connection state, message/rejection/drop counts and the
socket error number. Counters reset when input settings change. Packets exceeding
the NMEA library's 223-byte payload capacity are discarded. Supported instrument
PGNs fit within that limit; truncated instrument PGNs are rejected.

The TCP client uses a dedicated worker, bounded queues and nonblocking sockets.
It retries failed connections after five seconds, and reports silence after five
seconds without bytes. Instrument values expire at thirty seconds and show `--`.
Only the selected input updates N2K instruments. Changing mode or endpoint clears
cached N2K values and heading variation. Queued frames from previous settings are
discarded. Gateway data is never retransmitted onto CAN. The existing SmartShunt
BLE display and configured battery transmissions onto wired CAN remain active.
Wi-Fi scanning remains on demand.

## Gestures

On an instrument page, swipe left for the next enabled page and right for the
previous enabled page. Swipe down to edit the page currently displayed. Swipe up
to open Settings. Existing buttons remain available. Forms and keyboards do not
use these navigation gestures. A recognized gesture consumes the touch release
before changing screens. Gesture callbacks and boot-screen updates run on LVGL's
own task, under its existing ownership model.

The boot screen shows firmware version and build date/time, gesture hints, Wi-Fi
and N2K startup state for eight seconds. SmartShunt acquisition starts in the
background; the boot text does not claim a BLE device is connected.

## Multiple instrument devices

Within the chosen wired or wireless input, values now retain their selected source.
Use the field device menu to select an identified device/instance; all GPS fields
share one GPS selection. Automatic mode sticks to the first source and does not
fall back or alternate. See [sources and depth setup](sources-instruments-depth.md).

## Bench acceptance

- Confirm version, timestamp and readable gesture/status hints at cold boot.
- Check all four swipe directions on tile backgrounds and labels with rotated
  touch. Check page wraparound, disabled pages, and one enabled page. Confirm a
  swipe cannot also press a button on the screen it opens.
- Confirm gestures do not navigate while editing a form or using a keyboard.
- Configure a real W2K-1 TCP/N2K ASCII server. Enter its IP/port, save, reboot and
  compare depth, wind and heading with known instruments.
- Disconnect gateway/Wi-Fi and restore them; confirm stale tiles and recovery.
  Test a wrong IP/port and a wrong data format, then correct the settings.
- Change between wired and wireless while both send different values; confirm
  only the selected input is displayed and old heading variation is cleared.
- Verify SmartShunt BLE updates and battery CAN transmissions continue while
  receiving gateway data, running an explicit Wi-Fi scan and navigating pages.
- Exercise HTTPS and local upload OTA from the working firmware; retain a known
  good OTA slot and validate trial boot/rollback on hardware separately from USB.

Primary protocol reference:
[Actisense NMEA 2000 ASCII output format](https://actisense.com/knowledge-base/nmea-2000/w2k-1-nmea-2000-to-wifi-gateway/nmea-2000-ascii-output-format/).
