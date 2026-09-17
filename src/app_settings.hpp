#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

constexpr size_t MAX_SMARTSHUNTS = 4;

enum class DisplayTheme : uint8_t {
    Day = 0,
    Night = 1,
};

struct SmartShuntConfig {
    bool configured = false;
    bool enabled = true;
    bool n2k_enabled = false;
    uint8_t battery_instance = 0;
    std::array<char, 25> name{};
    std::array<char, 18> mac{};       // Internal stable identity; normally hidden from UI.
    std::array<char, 33> bindkey{};   // 128-bit key as 32 hex chars.
};

struct AppSettings {
    DisplayTheme theme = DisplayTheme::Day;
    uint8_t day_brightness = 80;
    uint8_t night_brightness = 20;
    std::array<SmartShuntConfig, MAX_SMARTSHUNTS> smartshunts{};
};

bool settings_init();
AppSettings settings_load();
bool settings_save(const AppSettings &settings);
