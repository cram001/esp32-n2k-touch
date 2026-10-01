#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

constexpr size_t MAX_SMARTSHUNTS = 4;
constexpr size_t MAX_DATA_PAGES = 6;
constexpr size_t MAX_DATA_FIELDS_PER_PAGE = 6;

enum class DisplayTheme : uint8_t {
    Day = 0,
    Night = 1,
};

enum class PageLayout : uint8_t {
    One = 1,
    Two = 2,
    Four = 4,
    Six = 6,
};

enum class DataSourceType : uint8_t {
    Nmea2000 = 0,
    SmartShunt = 1,
};

enum class DataMetric : uint8_t {
    None = 0,
    Depth,
    BoatSpeed,
    SpeedOverGround,
    CourseOverGround,
    Heading,
    ApparentWindSpeed,
    ApparentWindAngle,
    TrueWindSpeed,
    TrueWindAngle,
    WaterTemperature,
    AirTemperature,
    DistanceToWaypoint,
    TripDistance,
    BatteryVoltage,
    BatteryCurrent,
    BatterySoc,
    BatteryConsumedAh,
    BatteryTimeToGo,
    BatteryTemperature,
    EngineRpm,
    EngineCoolantTemperature,
    EngineOilPressure,
    EngineOilTemperature,
    EngineBoostPressure,
    EngineAlternatorVoltage,
    EngineFuelRate,
    EngineHours,
    EngineCoolantPressure,
    EngineFuelPressure,
    EngineLoad,
    EngineTorque,
    EngineExhaustTemperature,
    TankLevel,
    TankCapacity,
    Latitude,
    Longitude,
    Altitude,
    WaypointBearing,
    WaypointVmg,
    CrossTrackError,
    WaypointName,
    CabinTemperature,
    EngineRoomTemperature,
    EnvironmentalTemperature,
    AtmosphericPressure,
    Humidity,
    MagneticVariation,
    DepthTransducer,
    DepthBelowKeel,
    DepthWaterline,
    DepthSensorOffset,
    Count,
};

enum class DepthUnit : uint8_t { Metres = 0, Feet = 1 };
enum class TemperatureUnit : uint8_t { Celsius = 0, Fahrenheit = 1 };
enum class SpeedUnit : uint8_t { Knots = 0, KilometresPerHour = 1, MetresPerSecond = 2 };
enum class DistanceUnit : uint8_t { NauticalMiles = 0, Kilometres = 1 };
enum class ShortDistanceUnit : uint8_t { Metres = 0, Feet = 1, Yards = 2 };
enum class HeadingReference : uint8_t { True = 0, Magnetic = 1, Unknown = 255 };

struct UnitsSettings {
    DepthUnit depth = DepthUnit::Metres;
    TemperatureUnit temperature = TemperatureUnit::Celsius;
    SpeedUnit wind_speed = SpeedUnit::Knots;
    SpeedUnit vessel_speed = SpeedUnit::Knots;
    DistanceUnit distance = DistanceUnit::NauticalMiles;
    ShortDistanceUnit short_distance = ShortDistanceUnit::Metres;
    // Stored separately in NVS; this byte occupies former ABI padding.
    HeadingReference heading_reference = HeadingReference::True;
    // Distances below this threshold use short_distance. Stored in nautical miles.
    float short_distance_threshold_nm = 0.2f;
};

struct DataFieldSelection {
    DataSourceType source = DataSourceType::Nmea2000;
    DataMetric metric = DataMetric::None;
    // Used for SmartShunt source; ignored for NMEA 2000 fields.
    uint8_t source_index = 0;
};

struct DataPageConfig {
    bool enabled = false;
    PageLayout layout = PageLayout::Four;
    std::array<char, 17> name{};
    std::array<DataFieldSelection, MAX_DATA_FIELDS_PER_PAGE> fields{};
};

struct SmartShuntConfig {
    bool configured = false;
    bool enabled = true;
    bool n2k_enabled = false;
    uint8_t battery_instance = 0;
    std::array<char, 25> name{};
    std::array<char, 18> mac{};       // Internal stable identity; normally hidden from UI.
    std::array<char, 33> bindkey{};   // 128-bit key as 32 hex chars.
};

enum class WifiMode : uint8_t { Station = 0, AccessPoint = 1 };

struct WifiConfig {
    bool enabled = false;
    bool open_network = false;
    std::array<char, 33> ssid{};
    std::array<char, 65> password{};
    WifiMode mode = WifiMode::Station;
    std::array<char, 33> ap_ssid{"esp32-n2k-touch"};
    std::array<char, 65> ap_password{};
};

enum class N2kInputMode : uint8_t { Wired = 0, W2kTcp = 1 };
struct N2kInputConfig {
    N2kInputMode mode = N2kInputMode::Wired;
    std::array<char, 16> ip{};
    uint16_t port = 60001;
};

struct AppSettings {
    DisplayTheme theme = DisplayTheme::Day;
    uint8_t day_brightness = 80;
    uint8_t night_brightness = 20;
    WifiConfig wifi{};
    UnitsSettings units{};
    N2kInputConfig n2k_input{};
    std::array<DataPageConfig, MAX_DATA_PAGES> pages{};
    std::array<SmartShuntConfig, MAX_SMARTSHUNTS> smartshunts{};
};

bool settings_init();
AppSettings settings_load();
bool settings_save(const AppSettings &settings);
