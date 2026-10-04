#include <cassert>
#include <cstdio>
#include <cstring>
bool fake_queue_full=false;
unsigned fake_commands=0, fake_starts=0, fake_scans=0;
int fake_stop_error=0, fake_mode_error=0, fake_config_error=0, fake_start_error=0;
bool fake_ota_busy=false, fake_network_claim=false;
unsigned server_reports=0;
void ota_log_local_server_status(){++server_reports;}
bool ota_update_in_progress() { return fake_ota_busy; }
bool ota_begin_network_change() { if (fake_ota_busy) return false; fake_network_claim=true; return true; }
void ota_end_network_change() { fake_network_claim=false; }
// Include the actual service to exercise worker commands without starting threads.
#include "../../src/wifi_service.cpp"

int main() {
    assert(server_reports==0);
    WifiConfig migrated{};
    migrated.enabled=true;
    std::strcpy(migrated.ssid.data(), "BoatWiFi");
    // Schema-2 migration preserves a protected SSID with no stored password.
    assert(wifi_service_start(migrated));
    assert(wifi_service_get_status().state == WifiState::CredentialsRequired);
    assert(fake_starts == 0 && !fake_network_claim);
    assert(wifi_service_request_scan());
    assert(fake_commands == 1);
    assert(!wifi_service_request_scan()); // No overlapping scans.
    scan(); // Run the queued worker operation.
    assert(fake_starts == 1 && fake_scans == 1);
    complete_scan();
    assert(wifi_service_get_scan().state == WifiScanState::Complete);
    assert(wifi_service_get_status().state == WifiState::CredentialsRequired);
    assert(wifi_service_request_scan());
    complete_scan();

    WifiConfig valid=migrated;
    std::strcpy(valid.password.data(), "password123");
    fake_stop_error=ESP_FAIL;
    assert(!configure(valid));
    assert(!fake_network_claim && wifi_service_request_scan());
    complete_scan();
    fake_stop_error=ESP_OK;
    for (int *failure : {&fake_mode_error, &fake_config_error, &fake_start_error}) {
        *failure=ESP_FAIL;
        assert(!configure(valid));
        assert(!fake_network_claim && wifi_service_request_scan());
        complete_scan();
        *failure=ESP_OK;
    }
    assert(configure(valid));
    assert(!wifi_service_request_scan()); // Wait for actual station start event.
    event(nullptr, WIFI_EVENT, WIFI_EVENT_STA_START, nullptr);
    fake_ota_busy=true;
    assert(!wifi_service_request_scan());
    fake_ota_busy=false;
    fake_queue_full=true;
    assert(!wifi_service_request_scan());
    assert(!wifi_service_scan_active());
    fake_queue_full=false;
    assert(wifi_service_request_scan());
    complete_scan();
    WifiConfig ap=valid;ap.mode=WifiMode::AccessPoint;
    assert(!configure(ap));
    assert(!wifi_service_apply_config(ap));
    std::puts("PASS: migrated credentials scan; station-only Wi-Fi; stop/mode/config/start recovery; OTA and queue gating");
}
