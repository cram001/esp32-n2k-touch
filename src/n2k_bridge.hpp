#pragma once

#include "app_settings.hpp"

bool n2k_bridge_start(const AppSettings &settings);
void n2k_bridge_apply_settings(const AppSettings &settings);
// Queues identity discovery on the existing N2K task; never sends from LVGL.
void n2k_bridge_request_sources();
