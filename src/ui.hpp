#pragma once

#include "esp_err.h"
#include "app_settings.hpp"

// LVGL ownership contract: caller must hold bsp_display_lock() during ui_start().
// After startup, only LVGL callbacks/timers mutate UI objects. Wi-Fi, BLE and
// NMEA worker tasks publish into synchronized service/data snapshots and must
// never call LVGL directly.
void ui_start(const AppSettings &initial_settings);

bool ui_is_healthy();

constexpr uint32_t UI_NOTICE_ENVIRONMENT=1,UI_NOTICE_NVS_RESET=2;
// Thread-safe publication; the LVGL timer owns all resulting objects.
void ui_post_startup_notice(uint32_t flags);
