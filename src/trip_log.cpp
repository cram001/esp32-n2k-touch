#include "trip_log.hpp"
#include "nvs.h"

#include <array>
#include <cstring>

namespace {
constexpr char Key[] = "trip_log_v2";
constexpr char LegacyKey[] = "trip_log_v1";
// Fixed layout: settings bits followed by an ESP32 IEEE-754 double in metres.
static_assert(sizeof(double) == 8, "Trip distance storage requires an 8-byte double");
using Record = std::array<uint8_t, 9>;
}

bool TripLog::restore(int64_t now) {
    *this = TripLog{};
    next_checkpoint_us_ = now + CheckpointUs;
    nvs_handle_t handle;
    auto err = nvs_open("app", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return true;
    if (err != ESP_OK) return false;

    Record bytes{};
    size_t size = bytes.size();
    err = nvs_get_blob(handle, Key, bytes.data(), &size);
    const bool legacy = err == ESP_ERR_NVS_NOT_FOUND;
    if (legacy) {
        size = 1;
        err = nvs_get_blob(handle, LegacyKey, bytes.data(), &size);
    }
    nvs_close(handle);
    if (legacy && err == ESP_ERR_NVS_NOT_FOUND) return true;
    if (err != ESP_OK || size != (legacy ? 1 : bytes.size()) || bytes[0] > 3) return false;

    double metres = 0;
    if (!legacy) std::memcpy(&metres, bytes.data() + 1, sizeof(metres));
    if (!std::isfinite(metres) || metres < 0) return false;

    configure({bool(bytes[0] & 1), bytes[0] & 2 ? TripSpeed::Sog : TripSpeed::Stw}, now);
    metres_ = saved_metres_ = metres;
    return true;
}

bool TripLog::save(TripLogConfig config, double metres, int64_t now) {
    if (config.source != TripSpeed::Stw && config.source != TripSpeed::Sog) return false;
    if (!std::isfinite(metres) || metres < 0) return false;
    nvs_handle_t handle;
    auto err = nvs_open("app", NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;
    Record bytes{};
    bytes[0] = (config.enabled ? 1 : 0) | (config.source == TripSpeed::Sog ? 2 : 0);
    std::memcpy(bytes.data() + 1, &metres, sizeof(metres));
    err = nvs_set_blob(handle, Key, bytes.data(), bytes.size());
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return false;

    saved_metres_ = metres;
    next_checkpoint_us_ = now + CheckpointUs;
    checkpoint_failed_ = false;
    return true;
}

bool TripLog::save_settings(TripLogConfig config, int64_t now) {
    if (!save(config, metres_, now)) return false;
    configure(config, now);
    return true;
}

bool TripLog::save_reset(int64_t now) {
    if (!save(config_, 0, now)) return false;
    reset(now);
    return true;
}

bool TripLog::checkpoint(int64_t now, bool force) {
    if (metres_ == saved_metres_ || (!force && now < next_checkpoint_us_)) return true;
    // A failed write remains dirty and is retried at the same limited rate.
    next_checkpoint_us_ = now + CheckpointUs;
    checkpoint_failed_ = !save(config_, metres_, now);
    return !checkpoint_failed_;
}
