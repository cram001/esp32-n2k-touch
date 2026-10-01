#include "instrument_data.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "smartshunt_ble.hpp"
#include "n2k_sources.hpp"

namespace {
constexpr uint32_t NMEA_STALE_MS = 30000;
constexpr int64_t VARIATION_STALE_US = 30000000;
constexpr double MPS_TO_KNOTS = 1.94384449244;
constexpr double MPS_TO_KPH = 3.6;
constexpr double M_TO_FT = 3.280839895;
constexpr double M_TO_YD = 1.093613298;
constexpr double M_TO_NM = 1.0 / 1852.0;
constexpr double M_TO_KM = 0.001;
constexpr double RAD_TO_DEG = 57.29577951308232;
constexpr size_t METRIC_COUNT = static_cast<size_t>(DataMetric::Count);

struct CachedValue {
    bool valid = false;
    double value = 0.0;
    int64_t updated_us = 0;
    HeadingReference heading_reference = HeadingReference::Unknown;
};

std::array<CachedValue, METRIC_COUNT> g_nmea{};
CachedValue g_variation{};
portMUX_TYPE g_nmea_mux = portMUX_INITIALIZER_UNLOCKED;

size_t metric_index(DataMetric metric) { return static_cast<size_t>(metric); }

bool is_angle(DataMetric metric)
{
    return metric == DataMetric::CourseOverGround || metric == DataMetric::Heading ||
           metric == DataMetric::ApparentWindAngle || metric == DataMetric::TrueWindAngle || metric == DataMetric::WaypointBearing || metric == DataMetric::MagneticVariation;
}

bool is_wind_speed(DataMetric metric)
{
    return metric == DataMetric::ApparentWindSpeed || metric == DataMetric::TrueWindSpeed;
}

bool is_vessel_speed(DataMetric metric)
{
    return metric == DataMetric::BoatSpeed || metric == DataMetric::SpeedOverGround || metric == DataMetric::WaypointVmg;
}

bool is_temperature(DataMetric metric)
{
    return metric == DataMetric::WaterTemperature || metric == DataMetric::AirTemperature ||
           metric == DataMetric::BatteryTemperature || metric == DataMetric::EngineCoolantTemperature || metric == DataMetric::EngineOilTemperature || metric == DataMetric::EngineExhaustTemperature || metric == DataMetric::CabinTemperature || metric == DataMetric::EngineRoomTemperature || metric == DataMetric::EnvironmentalTemperature;
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
    // CAN bridge retains its five-second safety cutoff; display keeps values for 30 seconds.
    out.stale = d.age_ms >= 30000;
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

void instrument_data_reset_nmea() {
    n2k_sources_reset();
    portENTER_CRITICAL(&g_nmea_mux);
    g_nmea = {}; g_variation = {};
    portEXIT_CRITICAL(&g_nmea_mux);
}

void instrument_data_update_nmea(DataMetric metric, double value)
{
    const size_t idx = metric_index(metric);
    if (idx >= g_nmea.size() || metric == DataMetric::None) return;
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&g_nmea_mux);
    g_nmea[idx].valid = true;
    g_nmea[idx].value = value;
    g_nmea[idx].updated_us = now;
    // Generic heading updates have no trustworthy reference.
    g_nmea[idx].heading_reference = HeadingReference::Unknown;
    portEXIT_CRITICAL(&g_nmea_mux);
}

void instrument_data_update_heading(double radians, HeadingReference reference)
{
    if (!std::isfinite(radians) || (reference != HeadingReference::True && reference != HeadingReference::Magnetic)) return;
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&g_nmea_mux);
    auto &heading = g_nmea[metric_index(DataMetric::Heading)];
    heading.valid = true;
    heading.value = radians;
    heading.updated_us = now;
    heading.heading_reference = reference;
    portEXIT_CRITICAL(&g_nmea_mux);
}

void instrument_data_update_variation(double radians)
{
    if (!std::isfinite(radians) || std::abs(radians) > 3.141592653589793) return;
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&g_nmea_mux);
    g_variation.valid = true;
    g_variation.value = radians;
    g_variation.updated_us = now;
    portEXIT_CRITICAL(&g_nmea_mux);
}

InstrumentValue instrument_data_get(const DataFieldSelection &selection, uint8_t field_id)
{
    if (selection.source == DataSourceType::SmartShunt) return smartshunt_value(selection);
    if (selection.metric == DataMetric::Position) {
        InstrumentValue lat{}, lon{};
        n2k_sources_get(DataMetric::Latitude, field_id, lat);
        n2k_sources_get(DataMetric::Longitude, field_id, lon);
        InstrumentValue out{};
        out.valid = lat.valid && lon.valid;
        out.stale = lat.stale || lon.stale;
        out.age_ms = lat.age_ms > lon.age_ms ? lat.age_ms : lon.age_ms;
        out.value = lat.value;
        out.secondary_valid = lon.valid;
        out.secondary_value = lon.value;
        out.source_kind = lat.source_kind;
        out.source_instance = lat.source_instance;
        return out;
    }
    InstrumentValue out{};
    if (n2k_sources_get(selection.metric,field_id,out)) return out;
    const size_t idx = metric_index(selection.metric);
    if (idx >= g_nmea.size()) return out;

    CachedValue cached{};
    CachedValue variation{};
    portENTER_CRITICAL(&g_nmea_mux);
    cached = g_nmea[idx];
    variation = g_variation;
    portEXIT_CRITICAL(&g_nmea_mux);

    if (!cached.valid) return out;
    out.valid = true;
    out.value = cached.value;
    out.age_ms = static_cast<uint32_t>((esp_timer_get_time() - cached.updated_us) / 1000);
    out.stale = out.age_ms >= NMEA_STALE_MS;
    out.heading_reference = cached.heading_reference;
    out.variation_valid = variation.valid && esp_timer_get_time() - variation.updated_us < VARIATION_STALE_US;
    out.variation_radians = variation.value;
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
    case DataMetric::DepthTransducer: return "Depth: transducer";
    case DataMetric::DepthBelowKeel: return "Depth: keel";
    case DataMetric::DepthWaterline: return "Depth: surface";
    case DataMetric::DepthSensorOffset: return "Depth: sensor offset";
    case DataMetric::EngineRpm: return "Engine RPM";
    case DataMetric::EngineCoolantTemperature: return "Coolant temp";
    case DataMetric::EngineOilPressure: return "Oil pressure";
    case DataMetric::EngineOilTemperature: return "Oil temp";
    case DataMetric::EngineBoostPressure: return "Boost pressure";
    case DataMetric::EngineAlternatorVoltage: return "Alternator V";
    case DataMetric::EngineFuelRate: return "Fuel rate";
    case DataMetric::EngineHours: return "Engine hours";
    case DataMetric::EngineCoolantPressure: return "Coolant pressure";
    case DataMetric::EngineFuelPressure: return "Fuel pressure";
    case DataMetric::EngineLoad: return "Engine load";
    case DataMetric::EngineTorque: return "Engine torque";
    case DataMetric::EngineExhaustTemperature: return "Exhaust temp";
    case DataMetric::TankLevel: return "Tank level";
    case DataMetric::TankCapacity: return "Tank capacity";
    case DataMetric::Latitude: return "Latitude";
    case DataMetric::Longitude: return "Longitude";
    case DataMetric::Position: return "Position";
    case DataMetric::Altitude: return "GNSS altitude";
    case DataMetric::WaypointBearing: return "Waypoint bearing";
    case DataMetric::WaypointVmg: return "Waypoint VMG";
    case DataMetric::CrossTrackError: return "Cross track";
    case DataMetric::WaypointName: return "Waypoint name";
    case DataMetric::CabinTemperature: return "Cabin temp";
    case DataMetric::EngineRoomTemperature: return "Engine room temp";
    case DataMetric::EnvironmentalTemperature: return "Sensor temp";
    case DataMetric::AtmosphericPressure: return "Air pressure";
    case DataMetric::Humidity: return "Humidity";
    case DataMetric::MagneticVariation: return "Mag variation";
    default: return "None";
    }
}

const char *instrument_source_name(const DataFieldSelection &selection, const AppSettings &settings, uint8_t field_id)
{
    if (selection.source == DataSourceType::Nmea2000) {
        // Called only by the LVGL task; live source data is copied under its own lock.
        static char label[80];n2k_sources_label(selection.metric,field_id,label,sizeof(label));return label;
    }
    if (selection.source_index >= settings.smartshunts.size()) return "SmartShunt";
    const auto &cfg = settings.smartshunts[selection.source_index];
    return cfg.name[0] ? cfg.name.data() : "SmartShunt";
}

bool instrument_metric_supported(DataSourceType source, DataMetric metric)
{
    if (source == DataSourceType::SmartShunt) {
        return metric >= DataMetric::BatteryVoltage && metric <= DataMetric::BatteryTemperature;
    }
    return (metric > DataMetric::None && metric < DataMetric::BatteryVoltage) || (metric > DataMetric::BatteryTemperature && metric < DataMetric::Count);
}

void format_coordinate(double degrees, bool latitude, LatLonFormat format, char *out, size_t size)
{
    const char hemi = latitude ? (degrees < 0 ? 'S' : 'N') : (degrees < 0 ? 'W' : 'E');
    const double a = std::abs(degrees);
    const unsigned whole = static_cast<unsigned>(std::floor(a));
    const double minutes_full = (a - whole) * 60.0;
    const unsigned minutes = static_cast<unsigned>(std::floor(minutes_full));
    const double seconds = (minutes_full - minutes) * 60.0;
    switch (format) {
    case LatLonFormat::DecimalDegrees:
        std::snprintf(out, size, "%.5f %c", a, hemi);
        break;
    case LatLonFormat::DegreesMinutesSeconds:
        std::snprintf(out, size, "%u %02u %04.1f %c", whole, minutes, seconds, hemi);
        break;
    case LatLonFormat::DegreesMinutes:
    default:
        std::snprintf(out, size, "%u %06.3f %c", whole, minutes_full, hemi);
        break;
    }
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
    if (!v.valid || v.stale) { std::snprintf(value_out, value_out_size, "--"); return; }

    const DataMetric metric = selection.metric;
    if (n2k_sources_is_depth(metric) && (v.value > 1000 || !std::isfinite(v.value))) { std::snprintf(value_out,value_out_size,"--");return; }
    if (n2k_sources_is_depth(metric) || metric == DataMetric::Altitude || metric == DataMetric::CrossTrackError) {
        if (units.depth == DepthUnit::Feet) { std::snprintf(value_out, value_out_size, "%.1f", v.value * M_TO_FT); std::snprintf(unit_out, unit_out_size, "ft"); }
        else { std::snprintf(value_out, value_out_size, "%.1f", v.value); std::snprintf(unit_out, unit_out_size, "m"); }
    } else if (is_wind_speed(metric)) {
        format_speed(v.value, units.wind_speed, value_out, value_out_size, unit_out, unit_out_size);
    } else if (is_vessel_speed(metric)) {
        format_speed(v.value, units.vessel_speed, value_out, value_out_size, unit_out, unit_out_size);
    } else if (metric == DataMetric::Heading) {
        std::snprintf(unit_out, unit_out_size, "°%s", units.heading_reference == HeadingReference::Magnetic ? "M" : "T");
        if (v.heading_reference == HeadingReference::Unknown ||
            (v.heading_reference != units.heading_reference && !v.variation_valid)) {
            std::snprintf(value_out, value_out_size, "--");
            return;
        }
        double radians = v.value;
        if (v.heading_reference != units.heading_reference) {
            // East-positive variation: true = magnetic + variation.
            radians += units.heading_reference == HeadingReference::True ? v.variation_radians : -v.variation_radians;
        }
        double degrees = std::fmod(radians * RAD_TO_DEG, 360.0);
        if (degrees < 0) degrees += 360.0;
        std::snprintf(value_out, value_out_size, "%u", static_cast<unsigned>(std::lround(degrees)) % 360U);
    } else if (metric == DataMetric::WaypointName) {
        std::snprintf(value_out,value_out_size,"%s",v.text[0]?v.text.data():"--");
    } else if (metric == DataMetric::Position) {
        if (!v.secondary_valid) { std::snprintf(value_out,value_out_size,"--"); return; }
        char lat[32]{}, lon[32]{};
        format_coordinate(v.value, true, units.lat_lon_format, lat, sizeof(lat));
        format_coordinate(v.secondary_value, false, units.lat_lon_format, lon, sizeof(lon));
        std::snprintf(value_out, value_out_size, "%s\n%s", lat, lon);
        std::snprintf(unit_out, unit_out_size, "%s",
            units.lat_lon_format==LatLonFormat::DecimalDegrees?"DD":
            units.lat_lon_format==LatLonFormat::DegreesMinutesSeconds?"DMS":"DM");
    } else if (metric == DataMetric::Latitude || metric == DataMetric::Longitude) {
        format_coordinate(v.value, metric==DataMetric::Latitude, units.lat_lon_format, value_out, value_out_size);
        std::snprintf(unit_out, unit_out_size, "%s",
            units.lat_lon_format==LatLonFormat::DecimalDegrees?"DD":
            units.lat_lon_format==LatLonFormat::DegreesMinutesSeconds?"DMS":"DM");
    } else if (metric == DataMetric::EngineRpm) {
        std::snprintf(value_out,value_out_size,"%.0f",v.value);std::snprintf(unit_out,unit_out_size,"rpm");
    } else if (metric == DataMetric::TankLevel || metric == DataMetric::EngineLoad || metric == DataMetric::EngineTorque || metric == DataMetric::Humidity) {
        std::snprintf(value_out,value_out_size,"%.1f",v.value);std::snprintf(unit_out,unit_out_size,"%%");
    } else if (metric == DataMetric::TankCapacity || metric == DataMetric::EngineFuelRate) {
        std::snprintf(value_out,value_out_size,"%.1f",v.value);std::snprintf(unit_out,unit_out_size,"%s",metric==DataMetric::TankCapacity?"L":"L/h");
    } else if (metric == DataMetric::EngineHours) {
        std::snprintf(value_out,value_out_size,"%.1f",v.value/3600);std::snprintf(unit_out,unit_out_size,"h");
    } else if (metric == DataMetric::EngineOilPressure || metric == DataMetric::EngineBoostPressure || metric == DataMetric::EngineCoolantPressure || metric == DataMetric::EngineFuelPressure || metric == DataMetric::AtmosphericPressure) {
        std::snprintf(value_out,value_out_size,"%.2f",v.value/(metric==DataMetric::AtmosphericPressure?100:100000));
        std::snprintf(unit_out,unit_out_size,"%s",metric==DataMetric::AtmosphericPressure?"hPa":"bar");
    } else if (metric == DataMetric::EngineAlternatorVoltage) {
        std::snprintf(value_out,value_out_size,"%.2f",v.value);std::snprintf(unit_out,unit_out_size,"V");
    } else if (is_angle(metric)) {
        std::snprintf(value_out, value_out_size, "%.0f", v.value * RAD_TO_DEG);
        if((metric==DataMetric::CourseOverGround || metric==DataMetric::WaypointBearing) && v.heading_reference!=HeadingReference::Unknown)
            std::snprintf(unit_out, unit_out_size, "°%s",v.heading_reference==HeadingReference::True?"T":"M");
        else std::snprintf(unit_out, unit_out_size, "°");
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
