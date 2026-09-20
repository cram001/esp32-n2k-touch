#pragma once

#include <array>
#include <cstdint>

#include "app_settings.hpp"

enum class WifiState : uint8_t {
    Disabled = 0,
    Connecting,
    Connected,
    Disconnected,
    Error,
};

struct WifiStatus {
    WifiState state = WifiState::Disabled;
    int8_t rssi = 0;
    std::array<char, 16> ip{};
    std::array<char, 33> ssid{};
};

bool wifi_service_start(const WifiConfig &config);
void wifi_service_apply_config(const WifiConfig &config);
WifiStatus wifi_service_get_status();
bool wifi_service_is_connected();
