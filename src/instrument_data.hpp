#pragma once

#include <cstddef>
#include <cstdint>

#include "app_settings.hpp"

struct InstrumentValue {
    bool valid = false;
    bool stale = true;
    double value = 0.0;
    uint32_t age_ms = 0;
};

// NMEA 2000 services publish canonical values here: metres, m/s, radians, kelvin.
void instrument_data_update_nmea(DataMetric metric, double value);
InstrumentValue instrument_data_get(const DataFieldSelection &selection);

const char *instrument_metric_name(DataMetric metric);
const char *instrument_source_name(const DataFieldSelection &selection, const AppSettings &settings);
bool instrument_metric_supported(DataSourceType source, DataMetric metric);

// Formats a value and unit independently so the UI can size them differently.
void instrument_format_value(const DataFieldSelection &selection,
                             const UnitsSettings &units,
                             const InstrumentValue &value,
                             char *value_out,
                             size_t value_out_size,
                             char *unit_out,
                             size_t unit_out_size);
