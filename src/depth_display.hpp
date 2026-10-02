#pragma once
#include "instrument_data.hpp"
#include <cmath>

// UI-owned state: never writes the sensor cache or CAN values.
class DepthDisplay {
    bool ready=false;
    int shown=0, pending=0;
    int64_t sample=0;
    uint64_t source=0;
    uint32_t context=0;
    DataMetric metric=DataMetric::None;
    DepthUnit unit=DepthUnit::Metres;
    uint8_t instance=255,kind=255,reference=0;
    double offset=0;
public:
    void reset() { ready=false; }
    InstrumentValue apply(const DataFieldSelection &field,DepthUnit units,InstrumentValue v) {
        const bool depth=field.metric==DataMetric::Depth || field.metric==DataMetric::DepthTransducer ||
            field.metric==DataMetric::DepthBelowKeel || field.metric==DataMetric::DepthWaterline || field.metric==DataMetric::DepthSensorOffset;
        if(!depth || field.source!=DataSourceType::Nmea2000 || !v.valid || v.stale || !std::isfinite(v.value) || v.value>1000) {
            reset();return v;
        }
        const double factor=units==DepthUnit::Feet?3.280839895:1.0;
        const int next=static_cast<int>(std::lround(v.value*factor*10));
        if(!ready || source!=v.source_identity || context!=v.display_context || metric!=field.metric || unit!=units ||
           instance!=v.source_instance || kind!=v.source_kind || reference!=v.depth_reference || offset!=v.depth_offset_m ||
           v.sample_us<sample || v.sample_us-sample>=30000000) {
            ready=true;shown=pending=next;source=v.source_identity;context=v.display_context;metric=field.metric;unit=units;sample=v.sample_us;
            instance=v.source_instance;kind=v.source_kind;reference=v.depth_reference;offset=v.depth_offset_m;
        } else if(sample!=v.sample_us) {
            sample=v.sample_us;
            // A two-tenth change is immediate. One tenth needs two fresh
            // observations on the same side; alternating tenths stay steady.
            if(std::abs(next-shown)>=2 || (next!=shown && next==pending))shown=next;
            pending=next;
        }
        v.value=shown/(10*factor);return v;
    }
};
