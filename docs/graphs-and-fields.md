# Graphs, field groups and tile labels

## Configure a graph page

In Settings > Pages, select a page and cycle its layout button through NUMBERS
1/2/4/6, GRAPHS 1 and GRAPHS 2. Graphs use the first one or two configured fields;
each panel has an independent vertical scale and unit. Select 1 min, 5 min or
30 min using the time-window button. Each graph shows its current reading, range,
window and source. Swipe between pages as usual. Graphs accept both NMEA2000 and
configured Victron BLE numeric fields (including solar power).

History continues on enabled graph pages while another page/settings screen is
shown. Samples use the numeric display's units and precision. Stale/unavailable
readings leave gaps, including missed sampling intervals. Source, metric, unit or
window changes reset the affected history rather than joining unrelated values.
History is volatile and is lost on reboot; there is no SD logging. Coordinates,
combined position, waypoint names, UTC time, time-to-go and gear are not graph
fields. Select a numeric field if the panel asks for one. Angles retain their
normal wrap at 360 degrees; graphs do not unwrap bearings.

## Grouped field selection

Tap Data in a field editor to open groups, then a field. BACK returns from the
field list to groups; BACK again returns to the editor. None is at the group root.
Groups are Navigation & steering; Depth, speed & log; Wind; Engine & transmission;
Electrical & solar; Tanks; Environment. The previous/next arrows also follow group
order. Victron BLE filtering still shows only fields suitable for the selected
battery monitor/MPPT record type.

Additional received values:

| PGN | Fields added |
| --- | --- |
| 126992 | UTC time |
| 127245 | Rudder angle, per rudder instance |
| 127251 | Rate of turn, shown in degrees/minute |
| 127257 | Yaw, pitch and roll |
| 127488 | Engine trim |
| 127493 | Transmission gear, oil pressure and temperature |
| 127497 | Engine trip fuel used; average, economy and instantaneous fuel rates |
| 127506 | NMEA battery SOC/time-to-go; DC health, ripple voltage and capacity |
| 127508 | NMEA battery voltage, current and temperature |
| 127751 | DC voltage/current, per connection number |
| 128000 | Leeway angle |
| 130576 | Port and starboard trim tabs |

The 127497 economy fields are **L/h**, as defined by the pinned NMEA2000 library,
not calculated L/NM. DC capacity arrives in coulombs and displays as Ah; remaining
time arrives in seconds and uses the existing time-to-go display. A signed 24-bit
127751 decoder avoids the pinned library's negative-current sign-extension issue.
Field availability still depends on transmitted data; these are displays, not
new calculations. Source locking, engine/tank isolation and 30-second stale-data
rules remain in force on CAN and the existing W2K-1 decoder path.

## Custom tile labels

In a field editor, enter an optional custom label and press DONE. Labels are per
page/field, so two tiles can display the same metric with different titles. Clear
the label and press DONE to restore the automatic metric/depth-reference title.
Labels support up to 20 ASCII characters (letters, numbers, spaces and common
punctuation). They change titles only: units, selected sensor and calibration do
not change. A failed save keeps the old saved label and asks you to retry DONE.

## Persistence and memory

Existing metric IDs, display_v1 blob layouts and partition offsets are unchanged.
New metric IDs are appended. Graph preferences use a separate optional graphs_v1
key (16 bytes), defaulting to numeric pages on older installations. Custom titles
use tile_labels_v1 (760 bytes) and leave other NVS keys intact. Older firmware
ignores these keys, although saving new field selections using older firmware
may reset unsupported metrics to None. No erase or sequential upgrade is needed.

Graph sample and chart-point arrays are allocated explicitly in PSRAM: at most
12 histories (two per page), with 300 samples each; histories are retained for the
boot once allocated. Only two chart widgets are created, reused for the visible
page; numeric-only installations allocate no graph histories/widgets. Tile labels
also live in PSRAM and are not copied onto CAN/Wi-Fi task stacks. Both graph
sampling and LVGL updates run in the existing UI owner; there are no new tasks,
stack reductions, transport initialization changes or memory-profile changes.
Sample arrays remain external to LVGL, avoiding internal-RAM history buffers.

Hardware acceptance: update firmware.bin at the confirmed running slot with
whole-chip erase OFF; verify existing pages, source selections and Victron keys.
Test one/two graphs on CAN and separately on Wi-Fi/W2K; signed/zero values;
unit/source/window changes; source loss and reconnect; swiping away/back; labels
saved, cleared and retained on reboot. Check periodic internal heap/largest block
and stack watermarks during extended traffic. Host tests and firmware builds do
not establish hardware performance or runtime memory margins.
