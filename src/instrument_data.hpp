#pragma once

#include <cstddef>
#include <cstdint>

#include "app_settings.hpp"

struct InstrumentValue {
    bool valid = false;
    bool stale = true;
    double value = 0.0;
    bool secondary_valid = false;
    double secondary_value = 0.0;
    uint32_t age_ms = 0;
    int64_t sample_us = 0;
    uint64_t source_identity = 0;
    uint32_t display_context = 0;
    HeadingReference heading_reference = HeadingReference::Unknown;
    bool variation_valid = false;
    double variation_radians = 0.0;
    bool depth_offset_valid = false;
    double depth_offset_m = 0.0;
    uint8_t depth_reference = 0;
    uint8_t source_kind = 255;
    uint8_t source_instance = 255;
    std::array<char, 64> text{};
};

// NMEA 2000 services publish canonical values here: metres, m/s, radians, kelvin.
void instrument_data_reset_nmea();
void instrument_data_update_nmea(DataMetric metric, double value);
void instrument_data_update_heading(double radians, HeadingReference reference);
void instrument_data_update_variation(double radians);
InstrumentValue instrument_data_get(const DataFieldSelection &selection, uint8_t field_id = 255);

const char *instrument_metric_name(DataMetric metric);
const char *instrument_source_name(const DataFieldSelection &selection, const AppSettings &settings, uint8_t field_id = 255);
bool instrument_metric_supported(DataSourceType source, DataMetric metric);

// Formats a value and unit independently so the UI can size them differently.
void instrument_format_value(const DataFieldSelection &selection,
                             const UnitsSettings &units,
                             const InstrumentValue &value,
                             char *value_out,
                             size_t value_out_size,
                             char *unit_out,
                             size_t unit_out_size);
