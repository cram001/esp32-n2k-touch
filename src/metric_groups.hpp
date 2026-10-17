#pragma once
#include "app_settings.hpp"

enum class MetricGroup : uint8_t { General, Navigation, DepthSpeed, Wind, Engine, Electrical, Tanks, Environment, Count };
inline const char *metric_group_name(MetricGroup group) {
    switch(group) {
    case MetricGroup::Navigation:return "Navigation & steering";
    case MetricGroup::DepthSpeed:return "Depth, speed & log";
    case MetricGroup::Wind:return "Wind";
    case MetricGroup::Engine:return "Engine & transmission";
    case MetricGroup::Electrical:return "Electrical & solar";
    case MetricGroup::Tanks:return "Tanks";
    case MetricGroup::Environment:return "Environment";
    default:return "General";
    }
}
inline MetricGroup metric_group(DataMetric m) {
    switch(m) {
    case DataMetric::None:return MetricGroup::General;
    case DataMetric::Depth:case DataMetric::DepthTransducer:case DataMetric::DepthBelowKeel:
    case DataMetric::DepthWaterline:case DataMetric::DepthSensorOffset:case DataMetric::BoatSpeed:case DataMetric::TripDistance:return MetricGroup::DepthSpeed;
    case DataMetric::ApparentWindSpeed:case DataMetric::ApparentWindAngle:case DataMetric::TrueWindSpeed:case DataMetric::TrueWindAngle:return MetricGroup::Wind;
    case DataMetric::TankLevel:case DataMetric::TankCapacity:return MetricGroup::Tanks;
    case DataMetric::WaterTemperature:case DataMetric::AirTemperature:case DataMetric::CabinTemperature:
    case DataMetric::EngineRoomTemperature:case DataMetric::EnvironmentalTemperature:case DataMetric::AtmosphericPressure:case DataMetric::Humidity:return MetricGroup::Environment;
    default:
        if((m>=DataMetric::EngineRpm&&m<=DataMetric::EngineExhaustTemperature) ||
           (m>=DataMetric::EngineTrim&&m<=DataMetric::EngineInstantFuelRate))return MetricGroup::Engine;
        if((m>=DataMetric::BatteryVoltage&&m<=DataMetric::BatteryTemperature) ||
           (m>=DataMetric::SolarVoltage&&m<=DataMetric::SolarPower) ||
           (m>=DataMetric::DcVoltage&&m<=DataMetric::DcCapacity))return MetricGroup::Electrical;
        return MetricGroup::Navigation;
    }
}
inline bool metric_graphable(DataMetric m) {
    return m!=DataMetric::None && m!=DataMetric::Latitude && m!=DataMetric::Longitude &&
        m!=DataMetric::Position && m!=DataMetric::WaypointName && m!=DataMetric::TransmissionGear &&
        m!=DataMetric::UtcTime && m!=DataMetric::BatteryTimeToGo && m<DataMetric::Count;
}
