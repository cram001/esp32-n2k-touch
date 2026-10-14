#include "app_settings.hpp"
#include "settings_upgrade_nvs.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
static void load_fixture(const std::filesystem::path &path) {
 reset();std::ifstream input(path);assert(input.good());std::string line;
 while(std::getline(input,line)) {
  if(line.empty() || line[0]=='#')continue;
  std::istringstream row(line);std::string key,hex;assert(bool(row>>key>>hex));
  assert(hex.size()%2==0);Bytes bytes;
  for(size_t i=0;i<hex.size();i+=2)bytes.push_back(static_cast<unsigned char>(std::stoul(hex.substr(i,2),nullptr,16)));
  durable[key]=bytes;
 }
 namespace_exists=true;
}
static void check(const AppSettings &s,bool wifi) {
 assert(s.theme==DisplayTheme::Night && s.day_brightness==73 && s.night_brightness==19);
 assert(s.units.depth==DepthUnit::Feet && s.units.temperature==TemperatureUnit::Fahrenheit);
 assert(s.units.heading_reference==HeadingReference::Magnetic);
 assert(s.units.short_distance_threshold_nm==0.35f);
 assert(s.wifi.enabled && s.wifi.mode==WifiMode::Station);
 assert(std::strcmp(s.wifi.ssid.data(),"Upgrade test boat")==0);
 assert(std::strcmp(s.wifi.password.data(),"fixture-password-only")==0);
 assert(s.n2k_input.mode==(wifi?N2kInputMode::W2kTcp:N2kInputMode::Wired));
 assert(std::strcmp(s.n2k_input.ip.data(),"192.168.10.20")==0 && s.n2k_input.port==60003);
 for(size_t i=0;i<s.pages.size();++i) {
  const auto &p=s.pages[i];char name[20];std::snprintf(name,sizeof(name),"OLD PAGE %u",unsigned(i+1));
  assert(p.enabled && p.layout==PageLayout::Six && std::strcmp(p.name.data(),name)==0);
  assert(p.fields[0].metric==DataMetric::DepthBelowKeel && p.fields[1].metric==DataMetric::SpeedOverGround);
  for(size_t j=2;j<p.fields.size();++j)assert(p.fields[j].source==DataSourceType::SmartShunt && p.fields[j].metric==DataMetric::BatteryVoltage && p.fields[j].source_index==j-2);
 }
 for(size_t i=0;i<s.smartshunts.size();++i) {
  const auto &d=s.smartshunts[i];char name[32],mac[32];
  std::snprintf(name,sizeof(name),"OLD SHUNT %u",unsigned(i+1));
  std::snprintf(mac,sizeof(mac),"AA:BB:CC:DD:EE:%02X",unsigned(i));
  assert(d.configured && d.enabled && d.n2k_enabled && d.battery_instance==10+i);
  assert(std::strcmp(d.name.data(),name)==0 && std::strcmp(d.mac.data(),mac)==0);
  assert(std::strcmp(d.bindkey.data(),"0123456789ABCDEF0123456789ABCDEF")==0);
 }
}
int main(int argc,char **argv) {
 assert(argc==2);unsigned fixtures=0;
 for(const auto &entry:std::filesystem::directory_iterator(argv[1])) {
  if(entry.path().extension()!=".nvs")continue;
  const bool wifi=entry.path().stem().string().find("-wifi")!=std::string::npos;
  load_fixture(entry.path());const auto original=durable;
  assert(settings_init());auto s=settings_load();check(s,wifi);
  assert(durable==original && commits==0 && erase_calls==0); // Upgrade is read-only for supported current schemas.
  check(settings_load(),wifi); // Subsequent boot without installing intermediate releases.
  assert(settings_save(s));check(settings_load(),wifi);
  const auto saved=durable;fail_commit=true;
  s.day_brightness=95;assert(!settings_save(s));assert(durable==saved);
  fail_commit=false;check(settings_load(),wifi);
  // A release before separate preference keys must ignore old padding bytes.
  load_fixture(entry.path());durable.erase("rotation");durable.erase("heading_ref");durable.erase("latlon_fmt");
  durable.at("display_v1")[10]=255;durable.at("display_v1")[11]=255;
  const auto absent=durable;s=settings_load();
  assert(s.rotation==DisplayRotation::Rotated180 && s.units.heading_reference==HeadingReference::True && s.units.lat_lon_format==LatLonFormat::DegreesMinutes);
  assert(s.units.depth==DepthUnit::Feet && s.pages[5].enabled && s.smartshunts[3].configured);
  assert(durable==absent);
  // Unrecognized/truncated groups default individually without rewriting originals.
  for(const char *key:{"shunts_v2","display_v1","wifi_v3","n2k_in_v1"}) {
   load_fixture(entry.path());durable.at(key).resize(1);const auto broken=durable;
   s=settings_load();assert(durable==broken && erase_calls==0);
   if(std::strcmp(key,"shunts_v2")==0)assert(!s.smartshunts[0].configured && s.pages[0].enabled);
   if(std::strcmp(key,"display_v1")==0)assert(s.pages[0].layout==PageLayout::One && s.smartshunts[3].configured);
   if(std::strcmp(key,"wifi_v3")==0)assert(!s.wifi.enabled && s.smartshunts[3].configured);
   if(std::strcmp(key,"n2k_in_v1")==0)assert(s.n2k_input.mode==N2kInputMode::Wired && s.smartshunts[3].configured);
   load_fixture(entry.path());durable.at(key)[0]=255;const auto future=durable;
   (void)settings_load();assert(durable==future && erase_calls==0);
  }
  // Failed initialization must preserve even newer/full NVS for recovery.
  for(int error:{ESP_ERR_NVS_NO_FREE_PAGES,ESP_ERR_NVS_NEW_VERSION_FOUND,ESP_FAIL}) {
   load_fixture(entry.path());const auto before=durable;init_error=error;
   assert(!settings_init());assert(durable==before && erase_calls==0);
  }
  ++fixtures;
 }
 assert(fixtures==18);
 puts("PASS: 18 historical CAN/Wi-Fi settings snapshots loaded directly, preserved, saved and rebooted; missing preferences, malformed/future groups failed commits and non-destructive NVS init failures");
}
