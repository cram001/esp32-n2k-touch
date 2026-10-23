#include <cassert>
#include <cstring>
#include "wifi_service.hpp"
int main(){WifiConfig c;c.enabled=true;c.mode=WifiMode::AccessPoint;std::strcpy(c.ap_ssid.data(),"display");std::strcpy(c.ap_password.data(),"password");assert(!wifi_config_valid(c,nullptr));}
