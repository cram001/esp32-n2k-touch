#pragma once

#include <array>
#include <cstdint>

enum class DisplayTheme : uint8_t {
    Day = 0,
    Night = 1,
};

struct AppSettings {
    DisplayTheme theme = DisplayTheme::Day;
    uint8_t day_brightness = 80;
    uint8_t night_brightness = 20;

    bool smartshunt_enabled = false;
    bool smartshunt_n2k_enabled = false;
    uint8_t battery_instance = 0;
    std::array<char, 18> smartshunt_mac{};       // "AA:BB:CC:DD:EE:FF"
    std::array<char, 33> smartshunt_bindkey{};   // 128-bit key as 32 hex chars
};

bool settings_init();
AppSettings settings_load();
bool settings_save(const AppSettings &settings);
