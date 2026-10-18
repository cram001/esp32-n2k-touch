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

## 4 — W2K throughput

Fixed: socket drain is bounded by 4096 bytes or 5 ms per iteration, ending at EAGAIN. Shared `n2k_receive_pgns.hpp` retains RECEIVE_PGNS and accepts 60928/126996 before queueing. Increased packet queue and bridge drain from 16 to 64; queue drop count already appears in the Sources status and is preserved. No new per-packet allocation.

Files: `n2k_input.cpp`, `n2k_bridge.cpp`, shared PGN header, existing CI gateway test. The 1200-line burst fails before and passes after; unsupported PGN is filtered, backpressure remains counted. All 19 host tests pass. Firmware attempt timed out waiting for package installation; initial installation continues in a separate build attempt and has had mirror checksum failures followed by retries. Hardware throughput unverified.

## 5 — Bridge snapshot timeout and CAN guard

Fixed: `settings_snapshot()` retains a task-owned last-good cache, initialized before task creation. `g_wireless_input` is atomic; `ParseMessages()` requires successful CAN open. The extracted snapshot implementation is shared verbatim with the host test, avoiding an unrelated bridge refactor.

Files: `n2k_bridge.cpp`, `n2k_settings_snapshot.inc`, `bridge_snapshot_test.cpp`, CI. Snapshot regression fails before/pass after; checks brightness and wireless mode survive timeout, then a successful snapshot accepts changes. All 20 host tests pass. CAN-open guard inspected; not independently simulated by this test. Firmware attempted, installation lock timeout; no firmware pass claimed.

## 6 — Web window stop/re-enable race

Fixed: replaced worker-handle signalling with atomic Stopped/Starting/Listening/Stopping state (`local_server_window.hpp`). OTA mutex serializes transitions, deadlines and task creation. Expiry claims Stopping before httpd_stop; re-enable rejects during Stopping, and a later restart cannot have its deadline cleared by the previous worker. No worker handle is published across tasks.

Files: `ota.cpp`, `ota.hpp`, window policy/API include, `ota_window_test.cpp`, CI. New test exercises the public enable/disable code and stop interleaving; old API fails enable-during-stop assertion, replacement passes. All 21 host tests pass. Firmware attempted; install lock timeout. Real task scheduling still needs bench coverage.

## 7 — Lossless OTA state writes

Fixed: `set_active` and `set_status` use portMAX_DELAY, so transient contention cannot drop clears or progress/error updates. Failed claims never set active; successful ReadyToReboot deliberately retains the claim until reboot.

Files: `ota.cpp`, shared `ota_state.inc`, `ota_state_test.cpp`, CI. Contention fake rejects timed takes and accepts blocking takes: old implementation fails clearing active, replacement passes active/status/clamping/null-message checks. All 22 host tests pass. Firmware attempted, package installation still incomplete; no firmware pass claimed.

## 8 — Scan timeout

Fixed in `wifi_service.cpp`: worker stops/clears a scan after 10 s without SCAN_DONE, records ESP_ERR_TIMEOUT, clears scanning and permits reconnect/new scan. Late completion is ignored while idle.

Extended Wi-Fi SDK fake with time/worker stepping/scan-stop count; existing CI `wifi_service_test.cpp` runs the actual worker without completion. Regression fails before and passes after. All 22 host tests pass. Firmware attempted, dependency installation incomplete. Radio timing unverified on hardware.

## 9 — TWAI receive backpressure

Fixed: RX queue depth 128; failed ISR queue sends increment a lock-free atomic uint32 counter, exposed as RxDropped(). N2K task logs it every minute. No ISR allocation/blocking; polling cadence retained.

Files: transport `.cpp/.h`, bridge, TWAI fake and existing CI transport test. Original fails capacity regression; new test feeds 135 ISR receives, confirms 128 queued and 7 drops, and excludes driver receive errors. All 22 host tests pass. Firmware attempted, dependencies incomplete; live-bus load unverified.

## 10 — Task-only data locks

Fixed: source/device tables and the legacy instrument cache use statically allocated FreeRTOS mutexes instead of portMUX interrupt masking. Static allocation is enabled in the checked-in sdkconfig. No ISR accesses either cache; formatting/copies/loops no longer disable interrupts. No new heap allocation.

Files: `n2k_sources.cpp`, `instrument_data.cpp`, instrument FreeRTOS fake, `source_lock_test.cpp`, CI. Regression instruments critical-section entry; fails before/pass after for source formatting/get/reset and instrument reset. All 23 host tests pass. Firmware attempted, dependencies incomplete. Mutex contention/latency needs device observation.

## 11 — Non-allocating source lookups

Fixed: added `find_device()` and use it for label/invalidate. Only name/model/publish create slots. Files: `n2k_sources.cpp`, `source_lookup_test.cpp`, CI. Original fails after invalidating 200 unseen addresses; replacement consumes no slots, label lookup consumes none, and later NAME/manufacturer discovery succeeds. All 24 host tests pass. Firmware attempted; package installation incomplete.

## 12 — Local upload PIN; signing deferred

Fixed access control: every upload/reboot requires the page token plus a random six-digit per-session PIN, shown only on the display. Fixed-buffer comparisons run without data-dependent early exit. PIN is RAM-only, not returned by page HTML and not persisted. Signed-image enforcement is **not enabled**: no signing/trust provisioning exists in the current release flow. `docs/firmware-security.md` documents unsigned-image and HTTP interception risks plus signing/NVS-encryption prerequisites.

Files: OTA source/API/UI include, shared HTTP auth/page implementation, HTTP fake, `ota_auth_test.cpp`, OTA UI test, CI and security documentation. Original accepts token alone and fails regression; replacement rejects missing/wrong PIN and wrong token, accepts both, and serves no PIN value. All 25 host tests pass. Firmware attempted, dependency installation incomplete. PIN display and local upload still need device verification.
