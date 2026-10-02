# HTTP listener memory investigation and bounded startup

The board reports ESP_FAIL / socket errno 105 (ENOBUFS), with a working AP,
DHCP and valid OTA layout. This identifies a socket-resource allocation failure,
but does **not** prove internal RAM exhaustion. The installed PlatformIO
espressif32 6.13.0 SDK is ESP-IDF **5.5.3** (package `version.txt`).

## Heap evidence

Every attempt logs heap free bytes and largest free block for INTERNAL and
SPIRAM, plus INTERNAL minimum-ever free bytes, immediately before startup and
again after failure. INTERNAL|8BIT free/largest are also logged: raw INTERNAL
may include memory unsuitable for byte-accessible connection allocations.
The last snapshot is replayed on AP client connection, so missing early USB
output does not hide it. The minimum is a historical low-water mark; compare
current free and largest blocks to diagnose the current attempt.

Low internal byte-accessible free/largest values support pressure/fragmentation.
Healthy internal numbers **refute a simple internal-heap exhaustion claim**;
also inspect PSRAM availability and fragmentation. Free bytes alone cannot
prove that every particular capability-specific allocation could succeed.

In this SDK `lwip_socket()` sets ENOBUFS when `netconn_new_with_callback()`
fails. This can include the netconn allocation, receive mailbox/FreeRTOS queue,
or protocol control-block creation. A full descriptor table instead returns
ENFILE. `MEMP_MEM_MALLOC=1` means ordinary lwIP memp allocations use heap
allocation rather than fixed compile-time pools; raising MAX_SOCKETS alone is
not an evidence-based remedy here. If both heaps look healthy, inspect the
exact failed allocation/capability and netconn/mailbox/PCB path, SDK/config
mismatch, leaks and heap corruption. TCP/IP initialization must have completed
before socket creation; the normal path calls `esp_netif_init()` before
scheduling the listener. A working DHCP lease makes a permanently unstarted
TCP/IP thread less likely, but does not rule out a startup timing defect.

## Retry and rollback behavior

One priority-1 worker with a 4096-byte internal stack owns the HTTP handle.
Startup scheduling returns immediately. Each batch attempts at most ten times,
with two seconds between failed attempts (about 18 seconds plus execution time
for all attempts). AP_START in enabled AP mode queues a coalesced request;
events cannot bypass active backoff or create another listener. A new AP start
after exhaustion starts another bounded batch. A successful server is retained.
An exhausted batch does not automatically spin forever. The worker stays
alive to service subsequent AP-start events and sleeps between checks.

Status records the stage, ESP error, saved socket errno, attempt count and
whether another retry is pending. Failed route registration still stops the
candidate before retry. UI changes are made exclusively by the existing UI
refresh path; event/worker tasks never access LVGL objects.

A separate priority-1 health task has a 4096-byte internal stack and frees it
when finished. It keeps all existing settings/source/Wi-Fi/BLE/N2K/OTA-layout
prerequisites. It confirms no earlier than five seconds after services have
initialized, only with an actually Listening server and a healthy UI. A late
listener can satisfy those requirements within 25 seconds. Otherwise a pending
OTA trial is rejected using the existing rollback API. Merely scheduling retries
does not confirm an image. app_main does not wait through the retry/health window.
USB-installed images still have the existing USB/rollback limitations.

## Memory profile and tradeoffs

These are valid Kconfig symbols in the installed ESP-IDF 5.5.3. They are a
conservative pressure mitigation to evaluate on the board, not proof of cause.

| Setting | New value | Reason and tradeoff |
| --- | --- | --- |
| SPIRAM_USE_MALLOC | y (already the SDK default) | Keep PSRAM available to normal allocations. DMA and ordinary task stacks still require internal RAM. |
| SPIRAM_MALLOC_ALWAYSINTERNAL | 1024, previously 16384 | Prefer PSRAM for more medium-size allocations. Preserves internal RAM, with extra PSRAM access latency. Smaller allocations can still consume internal RAM. |
| SPIRAM_TRY_ALLOCATE_WIFI_LWIP | y | Supported network allocations prefer PSRAM with internal fallback. This does not move DMA buffers or all FreeRTOS queues to PSRAM. |
| ESP_WIFI_STATIC_RX_BUFFER_NUM | 6, previously 10 | Fewer permanent approximately 1.6 KB DMA buffers; matches the explicitly pinned RX BA window. Less receive burst headroom. |
| ESP_WIFI_RX_BA_WIN | 6, unchanged from the old profile | PSRAM-first would otherwise raise this to 16. Pin six to match static RX buffers. This deliberately sacrifices the SDK's recommended larger PSRAM throughput/compatibility profile for memory headroom; validate with the actual AP/station peers. |
| ESP_WIFI_DYNAMIC_RX_BUFFER_NUM | 16, previously 32 | Bounds dynamic receive bursts. May reduce throughput during congestion. |
| ESP_WIFI_STATIC_TX_BUFFER / NUM | y / 6 | PSRAM-first disables the dynamic TX choice in this SDK. Six buffers cost approximately 9.6 KB permanently and limit TX burst headroom; pinning avoids the new default of sixteen. |
| ESP_WIFI_CACHE_TX_BUFFER_NUM | 8, new PSRAM profile default 32 | Bound cached TX packets, at the cost of less congestion buffering. |
| LWIP_TCP_SND_BUF_DEFAULT / WND_DEFAULT | 2880 / 2880, previously 5760 / 5760 | Two 1440-byte MSS per direction reduce per-connection buffering; bulk OTA throughput may drop. Validate W2K freshness and station operation. |
| LWIP_TCP_OOSEQ_MAX_PBUFS | 2, previously 4 | Bound out-of-order buffering; PSRAM-first would otherwise change the default to unlimited (0). Lossy links may require more retransmission. |
| LWIP_MAX_SOCKETS | 10, unchanged | Four HTTP sessions plus three HTTP infrastructure sockets, W2K, HTTPS and headroom. Lowering it can prevent startup or disrupt other transports. |

The static TX switch can increase initialization-time DMA use compared with
dynamic TX despite the smaller RX count. Measure both pre/post heaps; this
profile reduces peak/burst pressure and moves supported allocations to PSRAM,
but is not a guaranteed reduction in every startup allocation.

HTTP stack remains **10240 bytes**, internal by SDK default. The upload handler
has a 4096-byte local buffer; page generation has a 2600-byte local buffer in
a separate handler. HTTP task stack allocation occurs **after** socket setup
in this SDK, so shrinking it does not repair this first socket-creation failure.
Reducing it without a measured upload-time stack watermark risks overflow.
Four HTTP sessions remain appropriate for two AP clients: browsers may use more
than one TCP connection per client, and LRU purging evicts idle sessions.

## Build and apply

The exported patch is against commit `868fa36041ee6169b45b36e5124db6e305b70477`
on `feature/display-ble-polish`. Apply to that source with:

```
git apply --check http-listener-memory.patch
git apply http-listener-memory.patch
```

Existing generated sdkconfig values override defaults. Preserve custom settings:
rename `sdkconfig.waveshare-touch-4` to a backup, then run `pio run -t clean`
and `pio run` to regenerate from defaults. Alternatively use
`pio run -t menuconfig` and set every profile value above manually.
Do not erase NVS. Reapply unrelated custom configuration from the backup if any.
The compile-time check rejects a stale memory profile. Verify the SDK version
in the build banner/package version.txt and inspect
`.pio/build/waveshare-touch-4/config/sdkconfig.h` for these exact symbols.
`python tools/validate_release.py --test` additionally verifies the generated
profile, firmware checksum/hash, partitions and rollback support.

## Expected board logs

Numbers below are placeholders, not measured heap results:

```
ota: HTTP startup attempt 1/10
ota: HTTP heap before httpd_start: internal free=... largest=... minimum=...; PSRAM free=... largest=...
ota: HTTP heap before httpd_start: internal 8-bit free=... largest=...
ota: HTTP server: Listening on port 80; error ESP_OK (0x0); socket errno 0 (none captured); attempt 1/10; retry idle; OTA layout: valid
app: Startup health passed: HTTP listener ready and UI healthy
ota: Local HTTP GET /
```

A transient failure additionally prints `HTTP heap after failed startup`,
the retained error and `HTTP startup retry in 2000 ms`. Later success must show
Listening and clear the old errors. Persistent failure ends with
`HTTP startup retries exhausted`; after the readiness deadline the health task
rejects an unconfirmed OTA trial. A regular USB installation does not become
an OTA trial just because this check fails.

Connect to the AP, visit `http://192.168.4.1/`, upload the application `.bin`,
wait for validation, then use the existing explicit reboot control. Also test
AP/Station transitions, CAN/N2K, SmartShunt freshness, W2K and OTA rejection cases.
The page and raw-upload format, partition/image validation and flash-write
safety checks are unchanged. Host tests cover helper errors/cleanup, bounded
retry timing/coalescing/restart, enabled-AP event gating and the health policy;
they do not prove FreeRTOS scheduling or radio/memory behavior on the board.

Primary references:
[Espressif HTTP server](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/protocols/esp_http_server.html),
[SDK PSRAM Kconfig](https://github.com/espressif/esp-idf/blob/v5.5.3/components/esp_psram/Kconfig.spiram.common),
[SDK Wi-Fi Kconfig](https://github.com/espressif/esp-idf/blob/v5.5.3/components/esp_wifi/Kconfig),
[SDK HTTP startup](https://github.com/espressif/esp-idf/blob/v5.5.3/components/esp_http_server/src/httpd_main.c).
