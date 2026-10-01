#include "app_settings.hpp"
#include "wifi_service.hpp"
#include "nvs.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using Bytes=std::vector<unsigned char>;
std::map<std::string,Bytes> durable,pending;
bool namespace_exists=false,fail_commit=false;
unsigned commits=0;
int nvs_flash_init(){return ESP_OK;}
int nvs_flash_erase(){durable.clear();namespace_exists=false;return ESP_OK;}
int nvs_open(const char *name,int mode,nvs_handle_t *handle){
    assert(std::strcmp(name,"app")==0);
    if(!namespace_exists && mode==NVS_READONLY)return ESP_ERR_NVS_NOT_FOUND;
    namespace_exists=true;*handle=1;return ESP_OK;
}
int nvs_get_blob(nvs_handle_t,const char *key,void *out,size_t *size){
    const auto it=durable.find(key);if(it==durable.end())return ESP_ERR_NVS_NOT_FOUND;
    if(*size<it->second.size()){*size=it->second.size();return ESP_ERR_NVS_INVALID_LENGTH;}
    *size=it->second.size();std::memcpy(out,it->second.data(),*size);return ESP_OK;
}
int nvs_set_blob(nvs_handle_t,const char *key,const void *data,size_t size){
    const auto *bytes=static_cast<const unsigned char *>(data);pending[key]=Bytes(bytes,bytes+size);return ESP_OK;
}
int nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *out){size_t size=1;return nvs_get_blob(h,key,out,&size);}
int nvs_set_u8(nvs_handle_t h,const char *key,uint8_t value){return nvs_set_blob(h,key,&value,1);}
int nvs_commit(nvs_handle_t){
    if(fail_commit)return ESP_FAIL;
    for(const auto &item:pending)durable[item.first]=item.second;
    pending.clear();++commits;return ESP_OK;
}
void nvs_close(nvs_handle_t){pending.clear();}
void reset(){durable.clear();pending.clear();namespace_exists=false;commits=0;fail_commit=false;}
template<class T> void fixture(const char *key,const T &value){
    const auto *bytes=reinterpret_cast<const unsigned char *>(&value);durable[key]=Bytes(bytes,bytes+sizeof(T));namespace_exists=true;
}
struct LegacyWifi {uint32_t schema=2;bool enabled=true;bool open=false;std::array<char,33> ssid{"BoatWiFi"};};
static_assert(sizeof(LegacyWifi)==40,"Migration fixture ABI");
int main(){
    reset();assert(settings_init());auto defaults=settings_load();
    assert(!defaults.wifi.enabled && defaults.pages[0].enabled && commits==0);
    assert(defaults.units.heading_reference==HeadingReference::True);

    reset();LegacyWifi legacy;fixture("wifi_v1",legacy);const Bytes old=durable["wifi_v1"];
    auto migrated=settings_load();
    assert(migrated.wifi.enabled && !migrated.wifi.open_network);
    assert(std::strcmp(migrated.wifi.ssid.data(),"BoatWiFi")==0 && migrated.wifi.password[0]==0);
    assert(migrated.wifi.mode==WifiMode::Station && commits==1 && durable["wifi_v3"].size()==204);
    assert(durable["wifi_v1"]==old);
    const char *reason=nullptr;assert(!wifi_config_valid(migrated.wifi,&reason) && reason);
    auto reboot=settings_load();assert(reboot.wifi.enabled && reboot.wifi.password[0]==0 && commits==1);

    reset();legacy.open=true;fixture("wifi_v1",legacy);migrated=settings_load();
    assert(migrated.wifi.open_network && wifi_config_valid(migrated.wifi,&reason));

    reset();legacy.open=false;fixture("wifi_v1",legacy);fail_commit=true;
    migrated=settings_load();assert(commits==0 && durable.count("wifi_v3")==0);
    fail_commit=false;migrated=settings_load();assert(commits==1); // Retry migration.

    reset();auto expected=defaults;expected.wifi.enabled=true;
    expected.wifi.ssid.fill('S');expected.wifi.ssid.back()=0;
    expected.wifi.password.fill('a');expected.wifi.password.back()=0; // 64 hex digits.
    expected.wifi.mode=WifiMode::AccessPoint;
    std::strcpy(expected.wifi.ap_ssid.data(),"Display OTA");std::strcpy(expected.wifi.ap_password.data(),"test-ap-secret");
    expected.smartshunts[0].configured=true;expected.smartshunts[0].n2k_enabled=true;
    expected.smartshunts[0].battery_instance=17;std::strcpy(expected.smartshunts[0].bindkey.data(),"0123456789abcdef0123456789abcdef");
    expected.units.depth=DepthUnit::Feet;expected.pages[1].enabled=true;expected.pages[1].layout=PageLayout::Six;
    expected.n2k_input.mode=N2kInputMode::W2kTcp;
    std::strcpy(expected.n2k_input.ip.data(),"192.168.4.1");expected.n2k_input.port=60003;
    expected.units.heading_reference=HeadingReference::Magnetic;
    assert(settings_save(expected));reboot=settings_load();
    assert(reboot.wifi.enabled && reboot.wifi.ssid==expected.wifi.ssid && reboot.wifi.password==expected.wifi.password);
    assert(reboot.wifi.ap_ssid==expected.wifi.ap_ssid && reboot.wifi.ap_password==expected.wifi.ap_password && reboot.wifi.mode==WifiMode::AccessPoint);
    assert(reboot.smartshunts[0].bindkey==expected.smartshunts[0].bindkey && reboot.smartshunts[0].battery_instance==17 && reboot.smartshunts[0].n2k_enabled);
    assert(reboot.units.depth==DepthUnit::Feet && reboot.pages[1].layout==PageLayout::Six && reboot.pages[1].enabled);
    assert(reboot.units.heading_reference==HeadingReference::Magnetic);
    assert(reboot.n2k_input.mode==N2kInputMode::W2kTcp && reboot.n2k_input.ip==expected.n2k_input.ip && reboot.n2k_input.port==60003);
    assert(durable["n2k_in_v1"].size()==24);
    auto saved_input=durable["n2k_in_v1"];durable["n2k_in_v1"][0]=2;
    assert(settings_load().n2k_input.mode==N2kInputMode::Wired);
    durable["n2k_in_v1"]=saved_input;durable["n2k_in_v1"][4]=255;
    assert(settings_load().n2k_input.mode==N2kInputMode::Wired);
    durable["n2k_in_v1"]=saved_input;
    // Original display_v1 padding must not be treated as a heading preference.
    durable.erase("heading_ref");
    durable["display_v1"][4+6]=255;
    reboot=settings_load();assert(reboot.units.heading_reference==HeadingReference::True);
    assert(reboot.units.depth==DepthUnit::Feet && reboot.pages[1].enabled);
    durable["heading_ref"]=Bytes(1,255);
    reboot=settings_load();assert(reboot.units.heading_reference==HeadingReference::True);
    expected.units.heading_reference=HeadingReference::True;
    assert(settings_save(expected));assert(settings_load().units.heading_reference==HeadingReference::True);

    auto config=expected.wifi;assert(wifi_config_valid(config,&reason));
    config.ap_password.fill(0);assert(!wifi_config_valid(config,&reason));
    std::strcpy(config.ap_password.data(),"1234567");assert(!wifi_config_valid(config,&reason));
    std::strcpy(config.ap_password.data(),"12345678");assert(wifi_config_valid(config,&reason));
    config.ap_password.fill('x');config.ap_password[63]=0;assert(wifi_config_valid(config,&reason));
    config.ap_password[63]='x';config.ap_password[64]=0;assert(!wifi_config_valid(config,&reason));
    config.mode=WifiMode::Station;assert(wifi_config_valid(config,&reason));
    config.password[5]='z';assert(!wifi_config_valid(config,&reason));
    config.open_network=true;assert(wifi_config_valid(config,&reason));
    config.ssid.fill(0);assert(!wifi_config_valid(config,&reason));
    config.enabled=false;assert(wifi_config_valid(config,&reason));

    // A malformed/future current blob must never be read as valid credentials.
    reset();namespace_exists=true;durable["wifi_v3"]=Bytes(204,0xff);
    const auto unknown=durable["wifi_v3"];reboot=settings_load();
    assert(!reboot.wifi.enabled && reboot.wifi.password[0]==0 && durable["wifi_v3"]==unknown);
    std::puts("PASS: defaults, legacy protected/open migration, retry, reboot credentials, AP/station separation, SmartShunt/page preservation, password bounds, invalid schema");
}
