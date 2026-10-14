# Sources, instruments and depth setup

Edit a data page, select a field, tap its metric to open the scrollable list, then
tap its device button to choose the source. Sources appear after their messages
arrive. Refresh updates the list and requests address claims/product information
on wired CAN. The receive-only W2K-1 path depends on the gateway delivering those
messages; it never sends gateway packets onto CAN.

## Device identity

Labels include manufacturer, model when available, measurement/engine/tank
instance, device instance and current network address. Two Garmin GPS units both
using instance 0 are still different devices: selection stores their unique
64-bit NMEA2000 NAME, with measurement instance/type where applicable. A selected
NAME follows address changes and survives reboot. Manufacturer and model are
readable descriptions, not unique identifiers.

If a gateway has not supplied address claims, a source is marked **this boot**.
Its address selection must be selected again after reboot or transport changes.
If an address claim arrives, select the identified device again. This prevents
silently assigning old readings or installation distances to a different device.

Each field can select a different device/instance. GPS selection is shared across
latitude, longitude, GNSS altitude, SOG and COG: those fields use one GPS device.
Automatic mode sticks to the first received device for a metric (one first GPS
for all GPS fields). It does not alternate values or fall back when a source
stops. Select a specific identified device for a choice that survives reboot.
Automatic source order can differ at the next boot.

Missing/invalid readings and readings aged **30 seconds or more** display `--`.
The same display timeout applies to SmartShunt readings. Existing SmartShunt CAN
transmission retains its separate five-second safety cutoff. Source lists retain
stale devices for selection; no background Wi-Fi scans have been added.

## Supported NMEA2000 fields

| Group | PGNs | Fields |
| --- | --- | --- |
| Heading | 127250, 127258 | Heading True/Magnetic and magnetic variation |
| Engines 0 and 1 | 127488, 127489 | RPM, boost/oil/coolant/fuel pressure, oil/coolant temperature, alternator voltage, fuel rate, hours, load, torque |
| Tanks | 127505 | Level and capacity; fluid type plus tank instance separates water, sewage, fuel and other tanks |
| Speed/depth/log | 128259, 128267, 128275 | Water speed, transducer depth/offset, trip distance |
| Position | 129025, 129026, 129029 | Latitude, longitude, GNSS altitude, SOG, COG |
| Navigation | 129283, 129284 | Cross-track error, distance/bearing/VMG to waypoint |
| Waypoint metadata | 129285, 130074 | Target name matched to the current navigation destination ID |
| Wind | 130306 | Apparent/true speed and angle, separated by reference type |
| Environment | 130310, 130311, 130312, 130316 | Water/outside/cabin/engine-room/other temperatures, exhaust gas temperature when its source type is reported |
| Humidity/pressure | 130311, 130313, 130314 | Humidity and atmospheric pressure |
| Identity | 60928, 126996 | Unique NAME, manufacturer/device instance, product model |

Unavailable fields remain `--`. Exhaust temperature requires a temperature PGN
identifying ExhaustGas; engine dynamic data alone does not provide it. Engine
instances above 1 are ignored. Tank instance is separate from device instance.
Depth PGN 128267 has no transducer-instance field: sources are distinguished by
device, not SID. GPS fields select a device rather than treating SID as a sensor.
Altitude is the reported GNSS altitude; this release does not convert vertical
datums or label it as water-surface elevation. COG and waypoint bearings retain
their received reference; Units' heading setting applies to heading only.
Waypoint names require route/list metadata from the selected navigation device;
a target change without a matching name displays `--`. Unicode metadata is
validated and stored as bounded UTF-8; display glyph availability depends on the font.
PGN 129285 has a separate 8-bit reserved field after the route-name string, before
the first waypoint ID, in addition to the reserved bits in byte 8. The decoder
retains this byte. Its layout is checked with an independent hand-authored fixture
as well as the library encoder; see [CANboat's PGN definitions](https://canboat.github.io/).

Pressure uses bar (engine) or hPa (atmosphere), tank capacity litres, fuel rate
litres/hour, engine time hours and level/load/torque/humidity percent. Existing
temperature/speed/distance settings apply. Position uses decimal degrees with
hemisphere; altitude and cross-track distance use the metres/feet setting.

## Depth references

Settings > Depth Setup first selects the physical depth source. Installation
distances are saved **per source**, following its NAME across address changes.
For an unidentified source, installation settings last only this boot.

| Reference | Calculation |
| --- | --- |
| Below transducer | Raw PGN depth |
| Sensor-provided offset | Raw depth + signed offset reported in PGN 128267 |
| Below keel | Raw depth - transducer-to-keel distance |
| Below water surface | Raw depth + transducer-to-surface distance |

Enter installation distances in metres, 0–100. Leave an unknown distance blank;
zero explicitly means zero. Local keel/surface modes use raw depth and do not
also apply the sensor's offset. A sensor already programmed below keel can use
Sensor-provided offset; verify that its gateway exposes the raw depth and offset
as defined by PGN 128267 before choosing further corrections.

The default for unconfigured sensors can be Transducer or Sensor-provided offset;
it cannot hold installation distances for unrelated sensors. The ordinary Depth
field uses its sensor's configured reference. Dedicated Depth: transducer,
Depth: keel, Depth: surface and Depth: sensor offset fields allow simultaneous
views of the same sensor. Required missing geometry/offset displays `--`.
Negative keel clearance remains visible. Raw depth above 1000 m and adjusted
depth above 1000 m display `--`, checked before conversion to feet.

## Persistence and limits

Existing page/units/Wi-Fi/SmartShunt blob layouts and metric IDs are preserved.
New metric IDs are appended. New independent versioned NVS blobs are
`sources_v1` (460 bytes, 36 field choices + shared GPS/default depth reference)
and `depth_src_v1` (356 bytes, 16 per-sensor installation profiles).
Writes update runtime preferences only after NVS commit succeeds.

The live cache is bounded to 192 measurement sources and 64 device descriptions.
Menus show up to 16 sources for a field. Waypoint metadata holds 32 entries,
decoding up to 16 entries from one complete list message. Messages above the
existing 223-byte gateway/library payload limit are rejected; larger route lists
must be supplied as smaller complete messages. No unbounded network allocations
or continuous discovery loops are introduced.

## Hardware acceptance

- With two same-model GPS devices and several additional GPS devices, choose one,
  verify all GPS fields, reboot, then change its CAN address. Disable it and verify
  `--` at 30 seconds without switching to another device.
- Select engine 0/1, water/sewage tanks with overlapping instances, and cabin,
  engine-room, outside and exhaust sensors. Compare against known instruments.
- Test two transducers mounted at different heights. Verify each installation
  profile, negative keel clearance, positive/negative sensor offsets, absent
  offsets/geometry, 1000 m and greater-than-1000 m readings in metres and feet.
- Change navigation target and route names, then remove metadata and verify no
  previous target name survives. Verify malformed and truncated messages cannot
  overwrite valid metadata.
- Confirm source/metric menus, depth keyboard and gestures on the rotated panel.
  Compare SmartShunt BLE and CAN operation during gateway reception and explicit
  Wi-Fi scans. Validate cold boot, OTA trial boot and rollback on the board.

Host tests and firmware builds validate logic and image structure. These hardware
acceptance steps require the physical display and network and are not yet claimed
as completed.
