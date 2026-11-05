#include "trip_log.hpp"
#include "nvs.h"

namespace {
constexpr char Key[] = "trip_log_v1";
// Bit 0: enabled; bit 1: GPS SOG (otherwise STW).
}

bool trip_log_load(TripLogConfig &config) {
    config = {};
    nvs_handle_t handle;
    auto err = nvs_open("app", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return true;
    if (err != ESP_OK) return false;
    uint8_t bits = 0;
    size_t size = sizeof(bits);
    err = nvs_get_blob(handle, Key, &bits, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return true;
    if (err != ESP_OK || size != sizeof(bits) || bits > 3) return false;
    config = {bool(bits & 1), bits & 2 ? TripSpeed::Sog : TripSpeed::Stw};
    return true;
}

bool trip_log_save(TripLogConfig config) {
    if (config.source != TripSpeed::Stw && config.source != TripSpeed::Sog) return false;
    nvs_handle_t handle;
    auto err = nvs_open("app", NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;
    const uint8_t bits = (config.enabled ? 1 : 0) | (config.source == TripSpeed::Sog ? 2 : 0);
    err = nvs_set_blob(handle, Key, &bits, sizeof(bits));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}
