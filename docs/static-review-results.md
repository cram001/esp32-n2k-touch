# Static review hardening verification

Base: `b12e203` on `main`. One commit per numbered review item. Existing NVS layouts and size assertions are retained.

## 1 — Station local firmware upload

Fixed the UI's unreachable AP-only gate; connected Station can open the local upload window. Removed BLE pause/resume from the worker: `main.cpp` starts BLE only in wired mode, and Wi-Fi only in W2K mode. Extracted the changed OTA callbacks into `ui_ota.inc` inside the existing private namespace (LVGL task ownership unchanged). Updated release notes.

Test: `ota_ui_test.cpp`, added to CI. Fails against original callbacks; passes with the fix. All 18 CI host executables pass with `LSAN_OPTIONS=detect_leaks=0` (container cannot run LeakSanitizer; ASan/UBSan remain enabled). Library tests use the pinned NMEA2000 commit, checked out locally.

`pio run` started; platform installation completed after a delay and toolchain installation is still in progress. Firmware build is not yet verified locally. No hardware test performed.

## 2 — Automatic source failover

Fixed: stale (30 s) or invalid automatic metric/GPS selections move to a fresher valid record. Manual choices remain locked. Publish reclaims records idle for 10 minutes and clears automatic indices before reuse; no persisted bytes changed.

Files: `n2k_sources.cpp`, `source_failover_test.cpp`, CI workflow. New test fails before the fix and covers silent first source, invalidation, GPS pairing, manual lock and full-table expiry. All 19 host tests pass (same LeakSanitizer limitation). `pio run` attempted after this item; bounded attempt timed out during package installation, so firmware remains unverified.

## 3 — Silent W2K connections

Fixed in `n2k_input.cpp`: 10-second receive silence closes the socket, resets framing and schedules the existing 5-second retry. TCP keepalive uses 10 s idle, 3 s interval, 3 probes; setup failures close/retry. Extended gateway socket fake and existing CI-registered `n2k_input_test.cpp`; new silent-peer assertion fails before and passes after, including the retry interval. All 19 host tests pass. `pio run` attempted, package-install lock did not resolve within the bounded local attempt; no firmware pass claimed.
