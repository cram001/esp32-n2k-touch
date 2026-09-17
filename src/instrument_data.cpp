#include "instrument_data.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "esp_timer.h"
#include "smartshunt_ble.hpp"

namespace {
constexpr uint32_t NMEA_STALE_MS = 5000;
constexpr double MPS_TO_KNOTS = 1.94384449244;
constexpr double MPS_TO_KPH = 3.6;
constexpr double M_TO_FT = 3.280839895;
constexpr double M_TO_YD = 1.093613298;
constexpr double M_TO_NM = 1.0 / 1852.0;
constexpr double M_TO_KM = 0.001;
constexpr double RAD_TO_DEG = 57.29577951308232;
constexpr size_t METRIC_COUNT = static_cast<size_t>(DataMetric::BatteryTemperature) + 1U;

struct CachedValue {
    bool valid = false;
    double value = 0.0;
    int64_t updated_us = 0;
};

std::array<CachedValue, METRIC_COUNT> g_nmea{};

size_t metric_index(DataMetric metric) { return static_cast<size_t>(metric); }

bool is_angle(DataMetric metric)
{
    return metric == DataMetric::CourseOverGround || metric == DataMetric::Heading ||
           metric == DataMetric::ApparentWindAngle || metric == DataMetric::TrueWindAngle;
}

bool is_wind_speed(DataMetric metric)
{
    return metric == DataMetric::ApparentWindSpeed || metric == DataMetric::TrueWindSpeed;
}

bool is_vessel_speed(DataMetric metric)
{
    return metric == DataMetric::BoatSpeed || metric == DataMetric::SpeedOverGround;
}

bool is_temperature(DataMetric metric)
{
    return metric == DataMetric::WaterTemperature || metric == DataMetric::AirTemperature ||
           metric == DataMetric::BatteryTemperature;
}

bool is_distance(DataMetric metric)
{
    return metric == DataMetric::DistanceToWaypoint || metric == DataMetric::TripDistance;
}

void format_speed(double mps, SpeedUnit unit, char *value, size_t vs, char *units, size_t us)
{
    switch (unit) {
    case SpeedUnit::KilometresPerHour:
        std::snprintf(value, vs, "%.1f", mps * MPS_TO_KPH);
        std::snprintf(units, us, "km/h");
        break;
    case SpeedUnit::MetresPerSecond:
        std::snprintf(value, vs, "%.1f", mps);
        std::snprintf(units, us, "m/s");
        break;
    case SpeedUnit::Knots:
    default:
        std::snprintf(value, vs, "%.1f", mps * MPS_TO_KNOTS);
        std::snprintf(units, us, "kn");
        break;
    }
}

InstrumentValue smartshunt_value(const DataFieldSelection &selection)
{
    InstrumentValue out{};
    if (selection.source_index >= MAX_SMARTSHUNTS) return out;
    const SmartShuntData d = smartshunt_ble_get_data(selection.source_index);
    out.stale = d.stale;
    out.age_ms = d.age_ms;
    switch (selection.metric) {
    case DataMetric::BatteryVoltage: out.valid = d.voltage_valid; out.value = d.voltage_v; break;
    case DataMetric::BatteryCurrent: out.valid = d.current_valid; out.value = d.current_a; break;
    case DataMetric::BatterySoc: out.valid = d.soc_valid; out.value = d.soc_pct; break;
    case DataMetric::BatteryConsumedAh: out.valid = d.consumed_ah_valid; out.value = d.consumed_ah; break;
    case DataMetric::BatteryTimeToGo: out.valid = d.time_to_go_valid; out.value = d.time_to_go_min; break;
    case DataMetric::BatteryTemperature: out.valid = d.temperature_valid; out.value = d.temperature_c + 273.15; break;
    default: break;
    }
    return out;
}
}

void instrument_data_update_nmea(DataMetric metric, double value)
{
    const size_t idx = metric_index(metric);
    if (idx >= g_nmea.size() || metric == DataMetric::None) return;
    g_nmea[idx].valid = true;
    g_nmea[idx].value = value;
    g_nmea[idx].updated_us = esp_timer_get_time();
}

InstrumentValue instrument_data_get(const DataFieldSelection &selection)
{
    if (selection.source == DataSourceType::SmartShunt) return smartshunt_value(selection);
    InstrumentValue out{};
    const size_t idx = metric_index(selection.metric);
    if (idx >= g_nmea.size() || !g_nmea[idx].valid) return out;
    out.valid = true;
    out.value = g_nmea[idx].value;
    out.age_ms = static_cast<uint32_t>((esp_timer_get_time() - g_nmea[idx].updated_us) / 1000);
    out.stale = out.age_ms > NMEA_STALE_MS;
    return out;
}

const char *instrument_metric_name(DataMetric metric)
{
    switch (metric) {
    case DataMetric::Depth: return "Depth";
    case DataMetric::BoatSpeed: return "Boat speed";
    case DataMetric::SpeedOverGround: return "SOG";
    case DataMetric::CourseOverGround: return "COG";
    case DataMetric::Heading: return "Heading";
    case DataMetric::ApparentWindSpeed: return "App wind";
    case DataMetric::ApparentWindAngle: return "AWA";
    case DataMetric::TrueWindSpeed: return "True wind";
    case DataMetric::TrueWindAngle: return "TWA";
    case DataMetric::WaterTemperature: return "Water temp";
    case DataMetric::AirTemperature: return "Air temp";
    case DataMetric::DistanceToWaypoint: return "To waypoint";
    case DataMetric::TripDistance: return "Trip";
    case DataMetric::BatteryVoltage: return "Battery V";
    case DataMetric::BatteryCurrent: return "Battery A";
    case DataMetric::BatterySoc: return "Battery SOC";
    case DataMetric::BatteryConsumedAh: return "Consumed Ah";
    case DataMetric::BatteryTimeToGo: return "Time to go";
    case DataMetric::BatteryTemperature: return "Battery temp";
    default: return "None";
    }
}

const char *instrument_source_name(const DataFieldSelection &selection, const AppSettings &settings)
{
    if (selection.source == DataSourceType::Nmea2000) return "NMEA 2000";
    if (selection.source_index >= settings.smartshunts.size()) return "SmartShunt";
    const auto &cfg = settings.smartshunts[selection.source_index];
    return cfg.name[0] ? cfg.name.data() : "SmartShunt";
}

bool instrument_metric_supported(DataSourceType source, DataMetric metric)
{
    if (source == DataSourceType::SmartShunt) {
        return metric >= DataMetric::BatteryVoltage && metric <= DataMetric::BatteryTemperature;
    }
    return metric > DataMetric::None && metric < DataMetric::BatteryVoltage;
}

void instrument_format_value(const DataFieldSelection &selection,
                             const UnitsSettings &units,
                             const InstrumentValue &v,
                             char *value_out,
                             size_t value_out_size,
                             char *unit_out,
                             size_t unit_out_size)
{
    if (!value_out || !unit_out || value_out_size == 0 || unit_out_size == 0) return;
    value_out[0] = '\0'; unit_out[0] = '\0';
    if (!v.valid || v.stale) { std::snprintf(value_out, value_out_size, "---"); return; }

    const DataMetric metric = selection.metric;
    if (metric == DataMetric::Depth) {
        if (units.depth == DepthUnit::Feet) { std::snprintf(value_out, value_out_size, "%.1f", v.value * M_TO_FT); std::snprintf(unit_out, unit_out_size, "ft"); }
        else { std::snprintf(value_out, value_out_size, "%.1f", v.value); std::snprintf(unit_out, unit_out_size, "m"); }
    } else if (is_wind_speed(metric)) {
        format_speed(v.value, units.wind_speed, value_out, value_out_size, unit_out, unit_out_size);
    } else if (is_vessel_speed(metric)) {
        format_speed(v.value, units.vessel_speed, value_out, value_out_size, unit_out, unit_out_size);
    } else if (is_angle(metric)) {
        std::snprintf(value_out, value_out_size, "%.0f", v.value * RAD_TO_DEG);
        std::snprintf(unit_out, unit_out_size, "deg");
    } else if (is_temperature(metric)) {
        const double c = v.value - 273.15;
        if (units.temperature == TemperatureUnit::Fahrenheit) { std::snprintf(value_out, value_out_size, "%.1f", c * 9.0 / 5.0 + 32.0); std::snprintf(unit_out, unit_out_size, "F"); }
        else { std::snprintf(value_out, value_out_size, "%.1f", c); std::snprintf(unit_out, unit_out_size, "C"); }
    } else if (is_distance(metric)) {
        const double nm = v.value * M_TO_NM;
        if (nm < units.short_distance_threshold_nm) {
            switch (units.short_distance) {
            case ShortDistanceUnit::Feet: std::snprintf(value_out, value_out_size, "%.0f", v.value * M_TO_FT); std::snprintf(unit_out, unit_out_size, "ft"); break;
            case ShortDistanceUnit::Yards: std::snprintf(value_out, value_out_size, "%.0f", v.value * M_TO_YD); std::snprintf(unit_out, unit_out_size, "yd"); break;
            case ShortDistanceUnit::Metres:
            default: std::snprintf(value_out, value_out_size, "%.0f", v.value); std::snprintf(unit_out, unit_out_size, "m"); break;
            }
        } else if (units.distance == DistanceUnit::Kilometres) {
            std::snprintf(value_out, value_out_size, "%.2f", v.value * M_TO_KM); std::snprintf(unit_out, unit_out_size, "km");
        } else {
            std::snprintf(value_out, value_out_size, "%.2f", nm); std::snprintf(unit_out, unit_out_size, "NM");
        }
    } else if (metric == DataMetric::BatteryVoltage) { std::snprintf(value_out, value_out_size, "%.2f", v.value); std::snprintf(unit_out, unit_out_size, "V"); }
    else if (metric == DataMetric::BatteryCurrent) { std::snprintf(value_out, value_out_size, "%+.1f", v.value); std::snprintf(unit_out, unit_out_size, "A"); }
    else if (metric == DataMetric::BatterySoc) { std::snprintf(value_out, value_out_size, "%.1f", v.value); std::snprintf(unit_out, unit_out_size, "%%"); }
    else if (metric == DataMetric::BatteryConsumedAh) { std::snprintf(value_out, value_out_size, "%.1f", v.value); std::snprintf(unit_out, unit_out_size, "Ah"); }
    else if (metric == DataMetric::BatteryTimeToGo) {
        const unsigned mins = static_cast<unsigned>(v.value < 0 ? 0 : v.value);
        std::snprintf(value_out, value_out_size, "%uh %02um", mins / 60U, mins % 60U);
    } else {
        std::snprintf(value_out, value_out_size, "%.1f", v.value);
    }
}
