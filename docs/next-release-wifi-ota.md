# Wi-Fi and OTA release notes

Source: `D:\Git\esp32-n2k-touch`, branch `feature/next-release-wifi-ota`.
Base: `f8e648011ed054350f03bb52e5978128b2ff4409` on `feature/smartshunt-ble-n2k`, verified against GitHub on September 30, 2026. The initial working tree was clean. The original branch remains at this commit.

## Implemented

- Display rotation of 180 degrees through the Waveshare BSP under the existing display lock. Installed LVGL 9.5 transforms the associated touch coordinates, so touch is not mirrored separately.
- HTTPS pull OTA restored in the build, with an HTTPS URL field, progress/error status and explicit reboot control under Wi-Fi > Firmware Update. The station starts SNTP after obtaining an IP; a cold-boot HTTPS update waits up to 15 seconds for a usable clock. TLS uses ESP-IDF's CA bundle and certificate verification.
- Application NVS Wi-Fi schema 3 stores enabled state, network mode, station SSID/password/authentication setting, and separate AP SSID/password. Schema 2 is migrated from `app/wifi_v1` into `app/wifi_v3`; the old key remains available to the previous firmware. Protected legacy networks require one password entry because that firmware never saved it. Unknown/malformed new records are not automatically overwritten.
- Password masking defaults on, with an explicit Show password checkbox. Passwords are re-masked when leaving Wi-Fi setup. Open station networks require an explicit Open network checkbox; an empty password never silently changes a protected network into an open one.
- Station authentication threshold now accepts WPA2 rather than requiring WPA2/WPA3 mixed security. Status shows actual IP/connection state, RSSI at association, numeric disconnect reasons and readable error messages, with capped reconnect backoff. ESP32-S3 uses 2.4 GHz Wi-Fi.
- User-triggered scans with SSID, signal strength and Open/Secured labels. Results are limited to 16 AP records and duplicate SSIDs are collapsed. Manual entry remains available for hidden SSIDs. Scans are never automatically repeated. If the driver is busy connecting, the UI reports the scan error and allows an explicit retry.
- Access Point mode uses separate credentials, WPA2, channel 1 and at most two clients. Set a custom AP password of 8–63 bytes. There is no default AP password, and an unconfigured AP will not start. Station and AP modes are selectable alternatives; AP mode does not supply Internet access.
- AP firmware upload through `http://<displayed-IP>/`, typically `http://192.168.4.1/`. Choose the application `firmware.bin`, upload, wait for validation, then click Reboot after validation. The server accepts upload/reboot only in active AP mode and requires a per-boot request token. Its task publishes status snapshots and never accesses LVGL.

Wi-Fi credentials reside in normal application NVS, with the same storage protection as the existing SmartShunt configuration. Firmware changes do not log passwords or include real credentials in tests.

## OTA partitions and rollback

The partition table is unchanged: NVS at `0x9000` (24 KiB), OTA metadata at `0xf000` (8 KiB), and two 6 MiB slots at `0x20000` and `0x620000` within 16 MiB flash. The updater verifies the expected layout before use and writes the inactive slot.

Both update methods validate project identity and secure version. Local upload checks the ESP32-S3 application header before erasing the target slot, bounds content to the slot size, handles short reads, and aborts an incomplete/failed upload. ESP-IDF checks the completed image before selecting it as the next boot image. HTTPS additionally checks that the entire download arrived. An update ready to reboot retains its exclusive claim; a second update cannot overwrite it. Network reconfiguration is serialized with OTA, and scans are refused during an update.

After a new image boots, confirmation requires successful NVS, display/UI, Wi-Fi service, BLE, N2K and OTA-server initialization, followed by a five-second observation period with a UI refresh heartbeat. Association with a router and reception from a SmartShunt/N2K device are not confirmation requirements. A failed health check explicitly rejects a pending image when a previous valid image is available. A crash/reset before confirmation also permits bootloader rollback. This follows the [ESP-IDF rollback lifecycle](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-reference/system/ota.html).

The previous diagnostic firmware omitted OTA confirmation. Use a serial installation for the first bench evaluation of this release if no operational OTA interface is available. An OTA application upload never installs a bootloader or partition table; the board must already have the matching dual-slot table and a rollback-enabled bootloader. Do not erase NVS when preparing the board if existing settings must be retained.

## Automated validation

Build with `pio run`.

The host regression suite compiles the real `app_settings.cpp` and `wifi_config.cpp` against an in-memory NVS test backend. It covers defaults; legacy protected/open migration; failed-commit retry; credential reload; separate station/AP credentials; retained legacy keys; SmartShunt/page/unit preservation; password boundaries; and invalid schemas. The backend tests application behavior and does not simulate ESP-IDF flash power-loss guarantees.

On Linux:

```sh
g++ -std=c++17 -Wall -Wextra -I tests/host -I src src/app_settings.cpp src/wifi_config.cpp tests/host/settings_test.cpp -o /tmp/settings_test
/tmp/settings_test
python tools/validate_release.py --test
```

On Windows, build the same host sources with a local C++ compiler. The local verification used a portable MinGW compiler with `-std=c++14 -static` because that installed test compiler predates the C++17 flag; firmware remains C++17. No compiler/toolchain is added to the repository.

The artifact validator checks generated partition-table integrity and layout, rollback in the generated application and bootloader configurations, ESP32-S3/16 MiB image metadata, project identity, slot size, checksum and SHA-256. Its negative tests reject payload/hash corruption, truncation, wrong chip/project, oversize images and partition-table corruption.

## Bench acceptance still required

### Boot troubleshooting and USB recovery

The startup screen displays the ESP application version (Git commit identifier) and
build date/time for five seconds. Settings retains the same information. The time
is the build machine's timestamp, not the current clock or an assumed UTC time.
Serial output records that identity, the running OTA slot/state and the minimum
remaining main-task stack after settings/UI and service initialization.

Startup now uses an 8192-byte main-task stack in both the defaults and the checked-in
PlatformIO configuration. A compile-time guard and artifact validation reject a
stale smaller configuration. UI initialization takes settings by reference.

**USB upload is installation/recovery, not a rollback-safe update.** The generated
flashing plan writes the application at `0x20000` (`ota_0`) and initializes
`otadata` at `0xf000`. It can overwrite the previous working image and reset the
OTA trial state. Merely having two slots and rollback enabled does not retain a
backup. After a known working USB installation, use HTTPS or local upload OTA to
write the inactive slot and mark it as a trial. Automatic rollback then requires
a bootable previous image and a rollback-enabled bootloader. If a USB-installed
app boot-loops, recover through USB; do not erase NVS just to recover the app.

The September 30 boot log reports a main-task stack overflow before display
initialization. The Wi-Fi switching-flag defect is separate: migrated protected
credentials now leave the service ready to accept an explicit scan, and failed
stop/configure/start operations also release the switching flag.

`tests/host/wifi_service_test.cpp` exercises the actual Wi-Fi service with SDK
boundary doubles: credential-required startup, explicit scan acceptance and
completion, stop/mode/config/start failure recovery, start-event gating, OTA
exclusion and queue-full recovery. It does not simulate radio behavior or task
concurrency. CI runs this alongside the NVS migration tests. Artifact tests also
reject missing or stale startup stack configurations.

1. Start on the bench with the existing N2K/CAN wiring and SmartShunt settings. Verify boot, inverted orientation and touch in all four corners, Wi-Fi keyboard, network picker, scroll areas and password toggle.
2. Upgrade from schema 2 without erasing NVS. Confirm the old SSID remains, the protected-network password prompt appears, and pages/units/brightness/SmartShunt keys and bridge settings survive. Enter the password once, save, power-cycle, and verify automatic reconnection.
3. Test a normal 2.4 GHz WPA2 network, an open network, a hidden SSID, wrong password and an unavailable SSID. Check the displayed numeric reason and error text. Power-cycle the router and confirm reconnect with backoff.
4. Tap Scan once, select a result and save. Verify manual hidden-SSID entry still works. Confirm no repeated scans occur while idle, and compare SmartShunt data freshness before/during/after a scan and throughout OTA.
5. Choose AP mode, enter a unique SSID and password, save and connect a laptop/phone. Verify the IP and update page. Switch back to Station and confirm its credentials remain; repeat in the opposite direction after reboot.
6. Upload a valid application image locally, wait for Ready, reboot and verify the new image confirms only after startup health checks. Repeat with a wrong-project image, non-application image, corrupted/truncated data and a dropped connection; the current image must remain selected on failure.
7. From Station mode, try a valid HTTPS application URL, a broken URL, an untrusted TLS certificate and interrupted download. Check error/progress feedback, the explicit reboot step and clock synchronization after a cold boot. AP-only mode must not offer an Internet-dependent pull update as ready.
8. On a recoverable bench setup with a known valid previous slot, install a deliberately unhealthy test image or reset the candidate before its five-second confirmation window. Confirm rollback to the previous slot, its version and retained NVS settings. Record results before considering this release ready for permanent installation.
9. Run the existing SmartShunt/NMEA bench checks in `docs/development.md`, including stale-data behavior and optional battery-PGN bridging. Those service implementations were not edited.

## Future planning, not implemented

- Selectable wired NMEA 2000 versus a wireless Actisense N2K stream.
- Swipe left/right to change instrument pages.
- Swipe down to edit the current page.
- Swipe up to open Settings.
- Boot-time gesture hints and status when gestures are implemented.

No wireless Actisense transport, gesture handler, gesture boot hints, automatic continuous Wi-Fi scanning, firmware flashing, push or PR merge is part of this change.
