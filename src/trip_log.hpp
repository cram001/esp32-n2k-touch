#pragma once

#include "instrument_data.hpp"

#include <cmath>

enum class TripSpeed : uint8_t { Stw, Sog };

struct TripLogConfig {
    bool enabled = false;
    TripSpeed source = TripSpeed::Stw;
};

bool trip_log_load(TripLogConfig &config);
bool trip_log_save(TripLogConfig config);

// Owned by the LVGL task. Integrate between distinct NMEA receive timestamps,
// never across a pause, source change, reset, or missing data.
class TripLog {
public:
    static constexpr int64_t MaxGapUs = 3000000;

    TripLogConfig config() const { return config_; }
    double metres() const { return metres_; }
    bool receiving() const { return sample_us_ != 0; }
    DataMetric speed_metric() const {
        return config_.source == TripSpeed::Sog ? DataMetric::SpeedOverGround : DataMetric::BoatSpeed;
    }

    void configure(TripLogConfig config, int64_t now) {
        if (config.enabled != config_.enabled || config.source != config_.source) {
            sample_us_ = 0;
            start_us_ = now;
        }
        config_ = config;
    }

    void reset(int64_t now) {
        metres_ = 0;
        sample_us_ = 0;
        start_us_ = now;
    }

    void sample(int64_t now, const InstrumentValue &speed) {
        if (!config_.enabled || !speed.valid || speed.stale ||
            !std::isfinite(speed.value) || speed.value < 0 ||
            speed.sample_us <= 0 || speed.sample_us < start_us_ ||
            speed.sample_us > now || now - speed.sample_us >= MaxGapUs) {
            sample_us_ = 0;
            return;
        }
        const bool same_source = source_ == speed.source_identity && context_ == speed.display_context;
        if (sample_us_ && same_source) {
            const auto gap = speed.sample_us - sample_us_;
            if (gap <= 0) return;
            if (gap < MaxGapUs) {
                metres_ += (previous_speed_ + speed.value) * 0.5 * (gap / 1000000.0);
            }
        }
        sample_us_ = speed.sample_us;
        source_ = speed.source_identity;
        context_ = speed.display_context;
        previous_speed_ = speed.value;
    }

private:
    TripLogConfig config_{};
    double metres_ = 0;
    double previous_speed_ = 0;
    int64_t start_us_ = 0;
    int64_t sample_us_ = 0;
    uint64_t source_ = 0;
    uint32_t context_ = 0;
};
