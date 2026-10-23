# Local OTA security

Local upload/reboot requires both the per-window page token and a random six-digit PIN shown only on the display. The PIN changes for every server session, is kept only in RAM and is never stored in NVS or embedded in the page. Both comparisons process fixed-size buffers without data-dependent early exit. The default window is 120 seconds; active uploads retain it until completion/reboot.

Firmware signature enforcement is **not enabled**. The current release workflow produces unsigned images and supplies no signing key/trust provisioning. Turning on CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT alone would break that flow. Before enabling it, provision a protected signing key, sign every release, validate the signed artifacts and test an upgrade/rollback from an existing installation on the bench. No private signing key belongs in the repository or plaintext NVS.

The PIN controls access, not image authorship. Project identity, chip, size, checksum and SHA verification detect incompatible/corrupt images but do not prove the publisher. Local HTTP exposes the token/PIN and image to an on-path LAN observer. Use a trusted isolated LAN for local updates; HTTPS pull authenticates the server using the CA bundle, but firmware itself remains unsigned.

NVS encryption is not enabled. ESP-IDF supports encrypted NVS with a keys partition and protected key provisioning; enabling it safely for existing devices requires a partition/provisioning/migration/recovery plan. Existing Wi-Fi and BLE credentials retain their current storage compatibility. No new persistent secrets were introduced by the PIN.
