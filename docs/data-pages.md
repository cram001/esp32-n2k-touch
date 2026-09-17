# Configurable data pages

The display supports six user-configurable instrument pages. Each page can be enabled or disabled independently and uses one of four layouts:

- 1 field
- 2 fields
- 4 fields
- 6 fields

Disabled pages are skipped by the previous/next page controls. At least one page is always kept enabled.

## Field sources

Each tile stores three pieces of configuration:

1. Source type: NMEA 2000 or SmartShunt.
2. Metric: the value to display.
3. Source index: used only for SmartShunt fields to identify a configured SmartShunt by its friendly name.

SmartShunt MAC addresses remain internal stable identifiers and are not part of normal field selection.

## NMEA 2000 metrics

The initial live decoder supports:

- Depth — PGN 128267
- Boat speed / speed through water — PGN 128259
- SOG / COG — PGN 129026
- Heading — PGN 127250
- Apparent wind speed/angle — PGN 130306
- True wind speed/angle — PGN 130306
- Water temperature — PGN 130312
- Outside/air temperature — PGN 130312
- Trip distance — PGN 128275

Distance to waypoint is already present in the field model and UI but its PGN 129284 decoder is still to be added.

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

Values older than five seconds are shown as unavailable rather than leaving stale values frozen on screen.
