#pragma once
#include "instrument_data.hpp"

constexpr size_t N2K_SOURCE_CHOICES = MAX_DATA_PAGES * MAX_DATA_FIELDS_PER_PAGE;
constexpr uint8_t N2K_GPS_CHOICE = N2K_SOURCE_CHOICES;
enum class SourceChoiceMode : uint8_t { Automatic = 0, Name = 1, ThisBootAddress = 2 };
struct N2kSourceChoice {
    uint64_t name = 0;
    uint8_t address = 255, instance = 255, kind = 255;
    SourceChoiceMode mode = SourceChoiceMode::Automatic;
};
struct N2kSourceOption {
    N2kSourceChoice choice{};
    std::array<char, 80> label{};
    bool stale = true;
};
enum class DepthReference : uint8_t { Transducer = 0, SensorOffset = 1, Keel = 2, Waterline = 3 };
struct DepthConfig {
    DepthReference reference = DepthReference::Transducer;
    bool keel_set = false, waterline_set = false;
    float transducer_to_keel_m = 0, transducer_to_waterline_m = 0;
};

bool n2k_sources_init();
void n2k_sources_reset(); // Clear live data, retain source and installation preferences.
void n2k_sources_name(uint8_t address, uint64_t name);
void n2k_sources_model(uint8_t address, const char *model);
void n2k_sources_publish(DataMetric metric, uint8_t address, uint8_t instance, uint8_t kind,
                         double value, HeadingReference reference = HeadingReference::Unknown,
                         bool offset_valid = false, double offset = 0, const char *text = nullptr);
bool n2k_sources_get(DataMetric metric, uint8_t field_id, InstrumentValue &value);
size_t n2k_sources_options(DataMetric metric, N2kSourceOption *out, size_t capacity);
void n2k_sources_label(DataMetric metric, uint8_t field_id, char *out, size_t capacity);
bool n2k_sources_is_gps(DataMetric metric);
bool n2k_sources_is_depth(DataMetric metric);
N2kSourceChoice n2k_sources_choice(uint8_t field_id);
bool n2k_sources_save_choice(uint8_t field_id, const N2kSourceChoice &choice);
DepthConfig n2k_sources_depth(const N2kSourceChoice *source = nullptr);
bool n2k_sources_save_depth(const DepthConfig &config, const N2kSourceChoice *source = nullptr);
bool n2k_depth_adjust(const DepthConfig &config, double raw, bool sensor_offset_valid,
                     double sensor_offset, double &adjusted);
