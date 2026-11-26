#include "trip_log.hpp"
#include "nvs_fake.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

int main() {
    fake_nvs::clear();
    TripLog log;
    assert(log.restore(0) && !log.config().enabled && log.config().source == TripSpeed::Stw);
    fake_nvs::durable["trip_log_v1"] = {3};
    assert(log.restore(0) && log.config().enabled && log.config().source == TripSpeed::Sog);
    assert(log.metres() == 0 && !log.receiving());
    fake_nvs::durable["trip_log_v1"] = {0xff};
    assert(!log.restore(0) && !log.config().enabled);
    fake_nvs::durable["trip_log_v1"] = {3, 0};
    assert(!log.restore(0) && !log.config().enabled);
    fake_nvs::durable["trip_log_v1"] = {1};

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
    assert(log.checkpoint(TripLog::CheckpointUs - 1));
    assert(!fake_nvs::durable.count("trip_log_v2"));
    assert(log.checkpoint(TripLog::CheckpointUs));

    TripLog rebooted;
    assert(rebooted.restore(0));
    assert(rebooted.metres() == 1 && rebooted.config().enabled && rebooted.speed_metric() == DataMetric::SpeedOverGround);
    assert(!rebooted.receiving()); // No pre-reboot sample can add distance after boot.
    InstrumentValue first{};
    first.valid = true;
    first.stale = false;
    first.value = 3;
    first.sample_us = 1000000;
    rebooted.sample(first.sample_us, first);
    assert(rebooted.metres() == 1);
    first.sample_us += 1000000;
    rebooted.sample(first.sample_us, first);
    assert(rebooted.metres() == 4);
    assert(rebooted.checkpoint(first.sample_us, true));
    TripLog after_restart;
    assert(after_restart.restore(0) && after_restart.metres() == 4);
    assert(log.save_settings(log.config(), TripLog::CheckpointUs)); // Restore the original session for failure tests.
    assert(rebooted.restore(0) && rebooted.metres() == 1);
    auto original = fake_nvs::durable.at("trip_log_v2");
    fake_nvs::fail_commit = true;
    assert(rebooted.checkpoint(TripLog::CheckpointUs)); // No write while unchanged.
    assert(!log.save_reset(61000000) && log.metres() == 1);
    assert(!log.save_settings({false, TripSpeed::Stw}, 61000000));
    assert(log.config().enabled && log.config().source == TripSpeed::Sog);
    assert(fake_nvs::durable.at("trip_log_v2") == original);

    at(26, 2);
    assert(!log.checkpoint(2 * TripLog::CheckpointUs) && log.checkpoint_failed());
    fake_nvs::fail_commit = false;
    assert(log.checkpoint(2 * TripLog::CheckpointUs + 1));
    assert(log.checkpoint_failed() && fake_nvs::durable.at("trip_log_v2") == original);
    assert(log.checkpoint(3 * TripLog::CheckpointUs) && !log.checkpoint_failed());
    assert(rebooted.restore(0) && rebooted.metres() == 3);

    at(27, 2);
    assert(log.save_settings({false, TripSpeed::Stw}, 181000000));
    assert(rebooted.restore(0) && rebooted.metres() == 5 && !rebooted.config().enabled);
    assert(rebooted.config().source == TripSpeed::Stw);
    assert(log.save_reset(182000000));
    assert(rebooted.restore(0) && rebooted.metres() == 0 && !rebooted.config().enabled);

    auto &record = fake_nvs::durable.at("trip_log_v2");
    record[0] = 0xff;
    assert(!rebooted.restore(0) && !rebooted.config().enabled && rebooted.metres() == 0);
    record[0] = 1;
    for (const double bad : {-1.0, std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
        std::memcpy(record.data() + 1, &bad, sizeof(bad));
        assert(!rebooted.restore(0) && !rebooted.config().enabled && rebooted.metres() == 0);
    }
    record.resize(8);
    assert(!rebooted.restore(0) && !rebooted.config().enabled);
    std::puts("PASS: trip integration, checkpoint rate and retry, reboot restore, reset, legacy settings and corrupt NVS");
}
