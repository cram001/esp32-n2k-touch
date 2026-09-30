#pragma once
#include <array>
#include <cstdint>
#include "app_settings.hpp"

enum class WifiState : uint8_t {
    Disabled, Connecting, Connected, CredentialsRequired, Disconnected, Error, AccessPoint
};
struct WifiStatus {
    WifiState state = WifiState::Disabled;
    int8_t rssi = 0;
    std::array<char,16> ip{};
    std::array<char,33> ssid{};
    int disconnect_reason = 0;
    int last_error = 0;
    std::array<char,96> message{};
};
constexpr size_t MAX_WIFI_NETWORKS = 16;
enum class WifiScanState : uint8_t { Idle, Scanning, Complete, Failed };
struct WifiNetwork {
    std::array<char,33> ssid{};
    int8_t rssi = 0;
    bool open = false;
};
struct WifiScanResults {
    WifiScanState state = WifiScanState::Idle;
    uint32_t generation = 0;
    size_t count = 0;
    int last_error = 0;
    std::array<WifiNetwork,MAX_WIFI_NETWORKS> networks{};
};
bool wifi_service_prepare();
bool wifi_service_scan_active();
bool wifi_service_start(const WifiConfig &config);
// Commands go to the Wi-Fi worker; LVGL callbacks never wait for a scan/stop.
bool wifi_service_apply_config(const WifiConfig &config);
bool wifi_service_request_scan();
WifiScanResults wifi_service_get_scan();
WifiStatus wifi_service_get_status();
bool wifi_service_is_connected();
bool wifi_config_valid(const WifiConfig &config, const char **reason);
