#pragma once

#include <cstdint>

enum class DisplayTheme : uint8_t {
    Day = 0,
    Night = 1,
};

struct AppSettings {
    DisplayTheme theme = DisplayTheme::Day;
    uint8_t day_brightness = 80;
    uint8_t night_brightness = 20;
};

bool settings_init();
AppSettings settings_load();
bool settings_save(const AppSettings &settings);
