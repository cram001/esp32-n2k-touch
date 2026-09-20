#pragma once

#include "app_settings.hpp"

// LVGL ownership contract: callers must hold bsp_display_lock() while entering
// ui_start(). After startup, only LVGL callbacks/timers may mutate UI objects.
// Worker tasks must publish data to synchronized model/service snapshots; they
// must never call LVGL directly.
void ui_start(AppSettings initial_settings);
