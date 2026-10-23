#include "metric_groups.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    const auto depth=metric_group(DataMetric::Depth);
    const auto speed=metric_group(DataMetric::BoatSpeed);
    assert(depth!=speed);
    assert(std::strcmp(metric_group_name(depth),"Depth")==0);
    assert(std::strcmp(metric_group_name(speed),"Speed & log")==0);
    for(auto m:{DataMetric::DepthTransducer,DataMetric::DepthBelowKeel,
                DataMetric::DepthWaterline,DataMetric::DepthSensorOffset})
        assert(metric_group(m)==depth);
    for(auto m:{DataMetric::SpeedOverGround,DataMetric::WaypointVmg,DataMetric::TripDistance})
        assert(metric_group(m)==speed);
    for(auto m:{DataMetric::ApparentWindSpeed,DataMetric::ApparentWindAngle,
                DataMetric::TrueWindSpeed,DataMetric::TrueWindAngle})
        assert(metric_group(m)==MetricGroup::Wind);
    for(auto m:{DataMetric::WaterTemperature,DataMetric::AirTemperature,
                DataMetric::CabinTemperature,DataMetric::EngineRoomTemperature,
                DataMetric::EnvironmentalTemperature,DataMetric::AtmosphericPressure,DataMetric::Humidity})
        assert(metric_group(m)==MetricGroup::Environment);
    for(unsigned i=0;i<unsigned(DataMetric::Count);++i) {
        const auto m=DataMetric(i);
        if((m>=DataMetric::EngineRpm && m<=DataMetric::EngineExhaustTemperature) ||
           (m>=DataMetric::EngineTrim && m<=DataMetric::EngineInstantFuelRate))
            assert(metric_group(m)==MetricGroup::Engine);
        assert(metric_group(m)<MetricGroup::Count);
        assert((metric_group(m)==MetricGroup::General)==(m==DataMetric::None));
    }
    std::puts("PASS: separate depth and speed groups, SOG/STW together, wind, environment, engines and complete coverage");
}
