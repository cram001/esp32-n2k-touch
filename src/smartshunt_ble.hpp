#pragma once

#include <cstdint>

#include "app_settings.hpp"

struct SmartShuntData {
    bool valid = false;
    bool key_valid = false;
    bool stale = true;
    int rssi = 0;
    uint32_t age_ms = 0;
    uint16_t alarm_reason = 0;

    bool time_to_go_valid = false;
    uint16_t time_to_go_min = 0;
    bool voltage_valid = false;
    float voltage_v = 0.0f;
    bool current_valid = false;
    float current_a = 0.0f;
    bool consumed_ah_valid = false;
    float consumed_ah = 0.0f;
    bool soc_valid = false;
    float soc_pct = 0.0f;
    bool temperature_valid = false;
    float temperature_c = 0.0f;

    char source_mac[18]{};
};

bool smartshunt_ble_start(const AppSettings &settings);
void smartshunt_ble_apply_settings(const AppSettings &settings);
SmartShuntData smartshunt_ble_get_data();
