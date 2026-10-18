# Static review hardening verification

Base: `b12e203` on `main`. One commit per numbered review item. Existing NVS layouts and size assertions are retained.

## 1 — Station local firmware upload

Fixed the UI's unreachable AP-only gate; connected Station can open the local upload window. Removed BLE pause/resume from the worker: `main.cpp` starts BLE only in wired mode, and Wi-Fi only in W2K mode. Extracted the changed OTA callbacks into `ui_ota.inc` inside the existing private namespace (LVGL task ownership unchanged). Updated release notes.

Test: `ota_ui_test.cpp`, added to CI. Fails against original callbacks; passes with the fix. All 19 CI host executables pass with `LSAN_OPTIONS=detect_leaks=0` (container cannot run LeakSanitizer; ASan/UBSan remain enabled). Library tests use the pinned NMEA2000 commit, checked out locally.

`pio run` started; platform installation completed after a delay and toolchain installation is still in progress. Firmware build is not yet verified locally. No hardware test performed.
