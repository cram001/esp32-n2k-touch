#pragma once

#include "esp_err.h"
#include "app_settings.hpp"

// LVGL ownership contract: caller must hold bsp_display_lock() during ui_start().
// After startup, only LVGL callbacks/timers mutate UI objects. Wi-Fi, BLE and
// NMEA worker tasks publish into synchronized service/data snapshots and must
// never call LVGL directly.
void ui_start(AppSettings initial_settings);
