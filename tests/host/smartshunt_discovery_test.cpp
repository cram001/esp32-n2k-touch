#include "ble/sdk.hpp"
#include "../../src/smartshunt_ble.cpp"
#include <cassert>
#include <cstdio>
int64_t fake_ble_time=1000000;
esp_ble_scan_params_t fake_scan_params{};
using Scan=esp_ble_gap_cb_param_t::ble_scan_result_evt_param;
int main(){
    AppSettings settings{};assert(smartshunt_ble_start(settings));
    assert(fake_scan_params.scan_type==BLE_SCAN_TYPE_ACTIVE);
    Scan scan{};scan.bda[5]=1;
    const uint8_t adv[]={5,0xff,0xe1,0x02,0x10,0x01,0,0};
    const uint8_t name[]={10,0x09,'H','o','u','s','e',' ','B','a','t'};
    scan.adv_data_len=sizeof(adv);scan.scan_rsp_len=sizeof(name);
    std::memcpy(scan.ble_adv,adv,sizeof(adv));std::memcpy(scan.ble_adv+sizeof(adv),name,sizeof(name));
    handle_victron_advertisement(scan);
    std::array<DiscoveredSmartShunt,MAX_DISCOVERED_SMARTSHUNTS> devices{};
    assert(smartshunt_ble_get_discovered(devices)==1);
    assert(!std::strcmp(devices[0].name.data(),"House Bat"));
    assert(!smartshunt_ble_get_data(0).valid); // Appears without config/key/telemetry.
    scan.scan_rsp_len=0;handle_victron_advertisement(scan);
    assert(smartshunt_ble_get_discovered(devices)==1 && !std::strcmp(devices[0].name.data(),"House Bat"));
    const uint8_t short_name[]={3,0x08,'H','B'};
    scan.scan_rsp_len=sizeof(short_name);std::memcpy(scan.ble_adv+sizeof(adv),short_name,sizeof(short_name));
    handle_victron_advertisement(scan);
    assert(smartshunt_ble_get_discovered(devices)==1 && !std::strcmp(devices[0].name.data(),"House Bat"));
    // A name-only response enriches a known MAC, never a new unrelated device.
    scan.adv_data_len=0;scan.scan_rsp_len=sizeof(name);std::memcpy(scan.ble_adv,name,sizeof(name));
    handle_victron_advertisement(scan);scan.bda[5]=2;handle_victron_advertisement(scan);
    assert(smartshunt_ble_get_discovered(devices)==1);
    scan.adv_data_len=63;scan.scan_rsp_len=0;handle_victron_advertisement(scan);
    assert(smartshunt_ble_get_discovered(devices)==1);
    fake_ble_time+=15001000;
    assert(smartshunt_ble_get_discovered(devices)==0); // Stale devices disappear.
    scan.adv_data_len=sizeof(adv);scan.scan_rsp_len=0;std::memcpy(scan.ble_adv,adv,sizeof(adv));
    handle_victron_advertisement(scan);
    assert(smartshunt_ble_get_discovered(devices)==1 && std::strstr(devices[0].name.data(),"Victron"));
    std::puts("PASS: real BLE service active scan, key-independent discovery, cached names, known-MAC responses, bounds and expiry");
}
