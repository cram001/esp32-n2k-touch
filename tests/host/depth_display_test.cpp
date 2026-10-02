#include "depth_display.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
int main(){
    DepthDisplay filter;DataFieldSelection field{};field.metric=DataMetric::Depth;
    InstrumentValue v{};v.valid=true;v.stale=false;v.source_identity=42;
    auto sample=[&](double metres,double expected,bool fresh=true){
        v.value=metres;if(fresh)++v.sample_us;
        const auto out=filter.apply(field,DepthUnit::Metres,v);
        assert(std::abs(out.value-expected)<0.00001);
    };
    sample(10.1,10.1);sample(10.2,10.1);
    for(int i=0;i<10;++i)sample(10.2,10.1,false);
    sample(10.1,10.1);sample(10.2,10.1);sample(10.1,10.1);
    sample(10.2,10.1);sample(10.3,10.3);sample(10.4,10.3);sample(10.6,10.6);sample(11.5,11.5);
    sample(11.4,11.5);sample(11.3,11.3);sample(11.4,11.3);sample(11.4,11.4);
    sample(11.3,11.4);sample(11.3,11.3); // sustained reversal
    ++v.source_identity;sample(11.4,11.4);
    ++v.display_context;sample(11.5,11.5);
    ++v.source_instance;sample(11.4,11.4);
    ++v.depth_reference;sample(11.5,11.5);
    v.stale=true;v.value=11.6;assert(filter.apply(field,DepthUnit::Metres,v).stale);
    v.stale=false;sample(11.6,11.6);
    v.valid=false;assert(!filter.apply(field,DepthUnit::Metres,v).valid);
    v.valid=true;sample(11.5,11.5);
    sample(1000.01,1000.01);sample(11.6,11.6);
    v.value=std::numeric_limits<double>::quiet_NaN();assert(std::isnan(filter.apply(field,DepthUnit::Metres,v).value));
    sample(-0.1,-0.1); // signed keel clearance
    filter.reset();
    auto feet=[&](double value,double expected){v.value=value/3.280839895;++v.sample_us;
        assert(std::abs(filter.apply(field,DepthUnit::Feet,v).value*3.280839895-expected)<0.00001);};
    feet(33.1,33.1);feet(33.2,33.1);feet(33.1,33.1);feet(33.2,33.1);feet(33.3,33.3);feet(33.2,33.3);feet(33.2,33.2);
    sample(10.2,10.2); // changing units resets even on the same sample
    v.sample_us+=30000000;sample(10.3,10.3); // loss while page is hidden
    std::puts("PASS: depth alternation, fresh samples, trends, reversals, jumps, source/settings resets, loss, signed clearance and feet");
}
