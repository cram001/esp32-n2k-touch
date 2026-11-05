#include "trip_log.hpp"
#include "nvs_fake.hpp"
#include <cassert>
#include <cstdio>
#include <limits>

int main() {
    fake_nvs::clear();
    TripLogConfig config{true, TripSpeed::Sog};
    assert(trip_log_load(config) && !config.enabled && config.source == TripSpeed::Stw);
    assert(trip_log_save({true, TripSpeed::Sog}));
    assert(trip_log_load(config) && config.enabled && config.source == TripSpeed::Sog);
    fake_nvs::fail_commit = true;
    assert(!trip_log_save({false, TripSpeed::Stw}));
    assert(trip_log_load(config) && config.enabled && config.source == TripSpeed::Sog);
    fake_nvs::durable["trip_log_v1"] = {0xff};
    assert(!trip_log_load(config) && !config.enabled && config.source == TripSpeed::Stw);
    fake_nvs::durable["trip_log_v1"] = {3, 0};
    assert(!trip_log_load(config) && !config.enabled);

    TripLog log;
    const auto at = [&](int64_t time, double mps, uint64_t source = 1, uint32_t context = 0) {
        InstrumentValue speed{};
        speed.valid = true;
        speed.stale = false;
        speed.value = mps;
        speed.sample_us = time * 1000000;
        speed.source_identity = source;
        speed.display_context = context;
        log.sample(speed.sample_us, speed);
    };
    at(1, 2); at(2, 4);
    assert(!log.receiving() && log.metres() == 0);
    log.configure({true, TripSpeed::Stw}, 3000000);
    assert(log.speed_metric() == DataMetric::BoatSpeed);
    at(2, 4); // Cached data from before enable.
    assert(!log.receiving());
    at(3, 2); at(4, 4);
    assert(log.receiving() && log.metres() == 3);
    at(4, 4); at(3, 2);
    assert(log.metres() == 3);
    at(5, 4);
    assert(log.metres() == 7);
    at(8, 4); // Exactly three seconds: skip the gap.
    assert(log.metres() == 7);
    at(9, 4, 2); // New transmitting device.
    at(10, 4, 2, 1); // Input reset.
    assert(log.metres() == 7);
    log.sample(11000000, InstrumentValue{});
    assert(!log.receiving());
    at(12, 4, 2, 1);
    at(13, std::numeric_limits<double>::quiet_NaN(), 2, 1);
    assert(!log.receiving() && log.metres() == 7);
    at(14, 4, 2, 1);
    InstrumentValue expired{};
    expired.valid = true;
    expired.stale = false;
    expired.sample_us = 14000000;
    expired.value = 4;
    log.sample(17000000, expired);
    assert(!log.receiving());
    log.configure({false, TripSpeed::Stw}, 17000000);
    at(18, 8);
    assert(!log.receiving() && log.metres() == 7);
    log.configure({true, TripSpeed::Stw}, 19000000);
    at(19, 2); at(20, 2);
    assert(log.metres() == 9);
    log.configure({true, TripSpeed::Sog}, 20500000);
    assert(log.speed_metric() == DataMetric::SpeedOverGround);
    at(20, 8); at(21, 8);
    assert(log.metres() == 9);
    at(22, 8);
    assert(log.metres() == 17);
    log.reset(22500000);
    at(22, 8);
    assert(log.metres() == 0 && !log.receiving() && log.config().enabled);
    at(23, 0); at(24, 0);
    assert(log.metres() == 0 && log.receiving());
    at(25, 2);
    assert(log.metres() == 1);
    std::puts("PASS: optional trip log, STW/SOG, integration, gaps, source changes, pause, reset and preferences");
}
