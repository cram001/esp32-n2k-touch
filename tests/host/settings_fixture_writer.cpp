// Compiled against the historical release's actual header and settings writer.
#include "app_settings.hpp"
#include "settings_upgrade_nvs.hpp"
#include <cstdio>
int main(int argc,char **argv) {
 assert(argc==2);const bool wireless=std::strcmp(argv[1],"wifi")==0;
 AppSettings s;
 s.theme=DisplayTheme::Night;s.day_brightness=73;s.night_brightness=19;
 s.units.depth=DepthUnit::Feet;s.units.temperature=TemperatureUnit::Fahrenheit;
 s.units.heading_reference=HeadingReference::Magnetic;
 s.units.short_distance_threshold_nm=0.35f;
 s.wifi.enabled=true;s.wifi.mode=WifiMode::Station;
 std::strcpy(s.wifi.ssid.data(),"Upgrade test boat");
 std::strcpy(s.wifi.password.data(),"fixture-password-only");
 s.n2k_input.mode=wireless?N2kInputMode::W2kTcp:N2kInputMode::Wired;
 std::strcpy(s.n2k_input.ip.data(),"192.168.10.20");s.n2k_input.port=60003;
 for(size_t i=0;i<s.pages.size();++i) {
  auto &p=s.pages[i];p.enabled=true;p.layout=PageLayout::Six;
  std::snprintf(p.name.data(),p.name.size(),"OLD PAGE %u",unsigned(i+1));
  p.fields[0]={DataSourceType::Nmea2000,DataMetric::DepthBelowKeel,0};
  p.fields[1]={DataSourceType::Nmea2000,DataMetric::SpeedOverGround,0};
  for(size_t j=2;j<p.fields.size();++j)
   p.fields[j]={DataSourceType::SmartShunt,DataMetric::BatteryVoltage,uint8_t(j-2)};
 }
 for(size_t i=0;i<s.smartshunts.size();++i) {
  auto &d=s.smartshunts[i];d.configured=true;d.enabled=true;d.n2k_enabled=true;
  d.battery_instance=uint8_t(10+i);
  std::snprintf(d.name.data(),d.name.size(),"OLD SHUNT %u",unsigned(i+1));
  std::snprintf(d.mac.data(),d.mac.size(),"AA:BB:CC:DD:EE:%02X",unsigned(i));
  std::strcpy(d.bindkey.data(),"0123456789ABCDEF0123456789ABCDEF");
 }
 assert(settings_init());assert(settings_save(s));
 for(const auto &[key,bytes]:durable) {
  std::printf("%s ",key.c_str());for(auto b:bytes)std::printf("%02x",b);std::puts("");
 }
}
