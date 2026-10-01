# Configurable data pages

The display supports six user-configurable instrument pages. Each page can be enabled or disabled independently and uses one of four layouts:

- 1 field
- 2 fields
- 4 fields
- 6 fields

Disabled pages are skipped by left/right swipes. At least one page is always kept enabled.
Navigation uses swipes: left/right changes enabled pages, down edits the current
page and up opens Settings. The instrument screen has no bottom Setup or arrow
buttons. Tiles use the released space; the page indicators remain at the bottom.
Only Victron SmartShunt tiles display their source name. NMEA2000 source selection
and identity remain available in the field editor.

## Brightness recovery

Every boot displays the startup screen at 80% brightness, even when saved day or
night brightness is too low to read. The screen instructs:
**TOUCH SCREEN FOR 3 SECONDS TO RESTORE BRIGHTNESS**.
Touch and hold anywhere continuously for three seconds to restore Day 80% and
Night 20%. A confirmation is shown; release to continue. A hold started near the
end of the eight-second boot screen is allowed to finish. Short taps do not reset
brightness. Normal startup applies the saved brightness after the boot screen.

Only the two brightness NVS keys are changed; Wi-Fi, pages, sources, calibration,
theme and SmartShunt settings are retained. If the save fails, visibility is
restored for the current boot and a message offers another hold to retry.
The retry window remains open for eight seconds after releasing the failed hold.
Starting another hold suspends the window until the hold completes. The boot
screen remains readable on the next restart regardless of the saved value.

## Field sources

Each tile stores three pieces of configuration:

1. Source type: NMEA 2000 or SmartShunt.
2. Metric: the value to display.
3. Source index: used only for SmartShunt fields to identify a configured SmartShunt by its friendly name.

SmartShunt MAC addresses remain internal stable identifiers and are not part of normal field selection.

NMEA2000 device selection is stored separately per field using unique NAME plus
measurement instance/type. GPS fields share one device selection. The device
button opens the source menu; automatic mode sticks to the first source without
alternating or falling back.

## NMEA 2000 metrics

The live decoder supports the original fields below, plus engines, tanks, position,
navigation and environmental sensors documented in
[sources and depth setup](sources-instruments-depth.md):

- Depth — PGN 128267
- Boat speed / speed through water — PGN 128259
- SOG / COG — PGN 129026
- Heading — PGN 127250; Units selects True or Magnetic. Conversion uses received
  magnetic variation (127250 or 127258); labels show `deg T` / `deg M`. Missing
  variation shows `--` when conversion is required. COG remains unchanged.
- Apparent wind speed/angle — PGN 130306
- True wind speed/angle — PGN 130306
- Water temperature — PGN 130312 and PGN 130316
- Outside/air temperature — PGN 130312 and PGN 130316
- Trip distance — PGN 128275
- Distance to waypoint — PGN 129284

## SmartShunt metrics

Any configured SmartShunt can be used as a tile source for:

- Voltage
- Current
- State of charge
- Consumed Ah
- Time to go
- Battery temperature when available

## Canonical values and units

Transport layers publish canonical values into the shared instrument cache:

- Distance/depth: metres
- Speed: metres/second
- Angles: radians
- Temperature: kelvin

The UI applies the selected display units when rendering. This avoids modifying NMEA values or coupling unit preferences to the network parser.

Global unit settings currently include:

- Depth: metres / feet
- Temperature: Celsius / Fahrenheit
- Wind speed: knots / km/h / m/s
- Vessel speed: knots / km/h / m/s
- Long distance: nautical miles / kilometres
- Short distance: metres / feet / yards
- Short-distance threshold: default 0.20 NM, adjustable in 0.05 NM increments

Values aged 30 seconds or more show `--`. This also applies to SmartShunt display values; its existing CAN safety cutoff remains five seconds.
