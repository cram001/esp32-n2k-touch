# Release plan

## Working release

Display/touch rotation, restored HTTPS and AP-upload OTA, Wi-Fi credential
persistence/migration, password visibility, station diagnostics, manual scan/select,
AP mode and boot identity are implemented. The user confirmed the boot fix is
functional on September 30, 2026. Existing N2K and SmartShunt behavior remains the
baseline for further changes. Hardware rollback and the broader network acceptance
checklist in `next-release-wifi-ota.md` still need explicit bench results.

## Current increment: heading reference

Units offers Heading: TRUE / MAGNETIC, defaults to True for existing installations,
and saves the selection in application NVS under `heading_ref`. Existing display,
units, pages and SmartShunt blobs retain their byte layout. Legacy padding is
ignored; missing/invalid heading keys default to True.

Heading PGN 127250 retains its reference. Variation from that message or PGN 127258
allows conversion: true = magnetic + east-positive variation. Heading tiles label
`deg T` / `deg M`, wrap at north and show `---` if conversion needs missing or
expired variation. Heading data expires after five seconds; variation after sixty.
Changing heading reference does not change COG or wind angles.

Bench checks: select a heading tile, toggle True/Magnetic, compare against known
N2K headings/variation, verify north wraparound, save and reboot, and confirm all
existing page/unit/SmartShunt settings remain. Confirm the seven Units rows fit and
respond correctly with rotated touch.

## Queued for later increments

- Selectable wired N2K versus wireless Actisense stream.
- Swipe left/right to change instrument pages.
- Swipe down to edit the current page.
- Swipe up to open Settings.
- Boot-time gesture hints and status when gestures are introduced.

Wireless transport and gestures remain future work. Wi-Fi scans remain explicit,
user-triggered operations to limit BLE coexistence impact. Every increment uses a
feature branch and PR; merging requires explicit approval.
