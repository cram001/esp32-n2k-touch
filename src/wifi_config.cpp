#include "wifi_service.hpp"
#include <algorithm>
#include <cstring>

bool wifi_config_valid(const WifiConfig &config, const char **reason) {
    if (!config.enabled) return true;
    if (config.mode != WifiMode::Station) {
        if (reason) *reason="Invalid network mode";
        return false;
    }
    const auto &ssid = config.ssid;
    const auto &password = config.password;
    const size_t ssid_len = strnlen(ssid.data(),ssid.size());
    const size_t len = strnlen(password.data(),password.size());
    if (!ssid_len || ssid_len > 32) { if(reason) *reason="Enter an SSID (1-32 bytes)"; return false; }
    if (config.open_network) return true;
    if (len >= 8 && len <= 63) return true;
    if (len == 64 && std::all_of(password.begin(),password.begin()+64,[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');})) return true;
    if(reason) *reason = "Enter password (8-63 bytes or 64 hex digits)";
    return false;
}
