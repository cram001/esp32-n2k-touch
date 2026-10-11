#define SMARTSHUNT_TEST_REAL_AES
#include "ble/sdk.hpp"
#include "../../src/smartshunt_ble.cpp"
#include <cassert>
#include <cmath>
int64_t fake_ble_time=1000000;
esp_ble_scan_params_t fake_scan_params{};
using Scan=esp_ble_gap_cb_param_t::ble_scan_result_evt_param;
void bits(uint8_t *out,size_t start,unsigned count,uint32_t value){
    for(unsigned i=0;i<count;++i){const size_t bit=start+i;out[bit/8]=(out[bit/8]&~(1U<<(bit%8)))|(((value>>i)&1U)<<(bit%8));}
}
Scan packet(unsigned mac,float voltage,int current,size_t length=15,uint8_t flags=0xa3){
    Scan scan{};scan.bda[5]=mac;
    uint8_t plain[16]{};
    bits(plain,0,16,120);bits(plain,16,16,static_cast<unsigned>(std::lround(voltage*100)));
    bits(plain,48,16,29815);bits(plain,64,2,2);bits(plain,66,22,current&0x3fffff);
    bits(plain,88,20,123);bits(plain,108,10,875);
    scan.adv_data_len=12+length;
    uint8_t *ad=scan.ble_adv;ad[0]=scan.adv_data_len-1;ad[1]=0xff;ad[2]=0xe1;ad[3]=2;
    ad[4]=0x10;ad[5]=2;ad[6]=0xa3;ad[7]=flags;ad[8]=2;ad[9]=0x34;ad[10]=0x12;ad[11]=0;
    std::array<uint8_t,16> key{};assert(parse_bindkey("000102030405060708090a0b0c0d0e0f",key));
    assert(decrypt_payload(key,plain,length,0x34,0x12,ad+12));
    return scan;
}
int main(){
    AppSettings settings{};
    for(unsigned i=0;i<2;++i){auto &c=settings.smartshunts[i];c.configured=c.enabled=true;
        std::snprintf(c.mac.data(),c.mac.size(),"00:00:00:00:00:%02X",i+1);
        std::strcpy(c.bindkey.data(),"000102030405060708090a0b0c0d0e0f");}
    assert(smartshunt_ble_start(settings));
    auto first=packet(1,13.25f,-12345);
    // Independent Python cryptography AES-CTR vector: key 000102...0f,
    // IV 3412 followed by 14 zero bytes, packed 118-bit battery fields.
    const uint8_t ciphertext[]={0x51,0x60,0x6b,0x0e,0xc6,0x01,0x7f,0xbe,0xb0,0xdf,0x64,0x5a,0xc9,0xd1,0x7b};
    std::memcpy(first.ble_adv+12,ciphertext,sizeof(ciphertext));
    handle_victron_advertisement(first);
    auto second=packet(2,12.6f,4567,16,0xa0);handle_victron_advertisement(second);
    auto a=smartshunt_ble_get_data(0),b=smartshunt_ble_get_data(1);
    assert(a.valid && a.key_valid && a.voltage_valid && a.current_valid);
    assert(std::fabs(a.voltage_v-13.25f)<0.001 && std::fabs(a.current_a+12.345f)<0.001);
    assert(std::fabs(a.soc_pct-87.5f)<0.001 && std::fabs(a.consumed_ah+12.3f)<0.001);
    assert(a.time_to_go_min==120 && std::fabs(a.temperature_c-25)<0.01);
    assert(b.valid && std::fabs(b.voltage_v-12.6f)<0.001 && std::fabs(b.current_a-4.567f)<0.001);
    auto short_packet=packet(1,15,10,14);handle_victron_advertisement(short_packet);
    assert(smartshunt_ble_get_data(0).voltage_v==a.voltage_v);
    first.ble_adv[8]=1;handle_victron_advertisement(first); // Other product record.
    assert(smartshunt_ble_get_data(0).voltage_v==a.voltage_v);
    first=packet(3,15,10);handle_victron_advertisement(first); // Unconfigured identity.
    assert(smartshunt_ble_get_data(0).voltage_v==a.voltage_v);
    first=packet(1,15,10);first.ble_adv[11]=1;handle_victron_advertisement(first);
    assert(!smartshunt_ble_get_data(0).valid && !smartshunt_ble_get_data(0).key_valid);
    assert(smartshunt_ble_get_data(1).valid); // Failure stays local to one slot.
    fake_ble_time+=5001000;assert(smartshunt_ble_get_data(1).stale);
    settings.smartshunts[0].enabled=false;smartshunt_ble_apply_settings(settings);
    first=packet(1,13.25f,-12345);handle_victron_advertisement(first);
    assert(!smartshunt_ble_get_data(0).valid);
    std::puts("PASS: encrypted 15/16-byte records, readout flags, per-MAC slots, signed fields, key mismatch, disabled and truncated records, freshness");
}
