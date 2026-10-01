#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include "nvs_fake.hpp"
// Include the real service so tests can simulate a process reboot without a test-only firmware API.
#include "n2k_sources.cpp"
int64_t fake_time_us=1000000;
void reboot(){initialized=false;assert(n2k_sources_init());}
uint64_t name(unsigned maker,unsigned unique,unsigned instance=0){return (static_cast<uint64_t>(maker)<<21)|unique|(static_cast<uint64_t>(instance)<<32)|(1ULL<<63);}
int main(){
    fake_nvs::clear();reboot();InstrumentValue value;
    const uint64_t a=name(229,1),b=name(229,2),c=name(1851,1);
    n2k_sources_name(10,a);n2k_sources_model(10,"GPS19x");
    n2k_sources_name(11,b);n2k_sources_model(11,"GPS19x");n2k_sources_name(12,c);
    n2k_sources_publish(DataMetric::Depth,10,255,255,10);
    n2k_sources_publish(DataMetric::Depth,11,255,255,20);
    assert(n2k_sources_get(DataMetric::Depth,0,value)&&value.value==10); // Sticky first source.
    N2kSourceOption options[16];assert(n2k_sources_options(DataMetric::Depth,options,16)==2);
    assert(std::strstr(options[0].label.data(),"Garmin") && options[0].choice.name!=options[1].choice.name);
    assert(n2k_sources_save_choice(0,options[1].choice));
    assert(n2k_sources_get(DataMetric::Depth,0,value)&&value.value==20);
    n2k_sources_publish(DataMetric::Depth,10,255,255,30);
    assert(n2k_sources_get(DataMetric::Depth,0,value)&&value.value==20);
    // The selected NAME follows a new address, while its old address belongs to someone else.
    n2k_sources_name(21,b);n2k_sources_name(11,c);
    n2k_sources_publish(DataMetric::Depth,21,255,255,22);
    n2k_sources_publish(DataMetric::Depth,11,255,255,99);
    assert(n2k_sources_get(DataMetric::Depth,0,value)&&value.value==22);
    fake_time_us+=29999000;assert(n2k_sources_get(DataMetric::Depth,0,value)&&!value.stale);
    fake_time_us+=1000;assert(n2k_sources_get(DataMetric::Depth,0,value)&&value.stale);
    // Greater than 1000 metres invalidates immediately, before unit conversions or offsets.
    n2k_sources_publish(DataMetric::Depth,21,255,255,1000.01);
    assert(n2k_sources_get(DataMetric::Depth,0,value)&&!value.valid);
    n2k_sources_publish(DataMetric::Depth,21,255,255,1000);
    assert(n2k_sources_get(DataMetric::Depth,0,value)&&value.valid);
    // Two engine instances on one device, and two tank types with the same instance.
    n2k_sources_publish(DataMetric::EngineRpm,10,0,255,1000);n2k_sources_publish(DataMetric::EngineRpm,10,1,255,2000);
    assert(n2k_sources_options(DataMetric::EngineRpm,options,16)==2);
    assert(n2k_sources_save_choice(1,options[1].choice));assert(n2k_sources_get(DataMetric::EngineRpm,1,value)&&value.value==2000);
    n2k_sources_publish(DataMetric::TankLevel,10,0,1,40);n2k_sources_publish(DataMetric::TankLevel,10,0,5,80);
    assert(n2k_sources_options(DataMetric::TankLevel,options,16)==2);
    assert(n2k_sources_save_choice(2,options[1].choice));assert(n2k_sources_get(DataMetric::TankLevel,2,value)&&value.value==80);
    // Five GPS units: a single choice applies to position, height, SOG and COG.
    for(unsigned i=0;i<5;++i){n2k_sources_name(30+i,name(i<2?229:1851,100+i));
        n2k_sources_publish(DataMetric::Latitude,30+i,255,255,40+i);
        n2k_sources_publish(DataMetric::Longitude,30+i,255,255,-100.0-i);
        n2k_sources_publish(DataMetric::Altitude,30+i,255,255,100+i);
        n2k_sources_publish(DataMetric::SpeedOverGround,30+i,255,255,5+i);}
    assert(n2k_sources_options(DataMetric::Latitude,options,16)==5);
    assert(n2k_sources_save_choice(N2K_GPS_CHOICE,options[3].choice));
    for(unsigned field:{0,10,35}){
        assert(n2k_sources_get(DataMetric::Latitude,field,value)&&value.value==43);
        assert(n2k_sources_get(DataMetric::Longitude,field,value)&&value.value==-103);
        assert(n2k_sources_get(DataMetric::Altitude,field,value)&&value.value==103);
        assert(n2k_sources_get(DataMetric::SpeedOverGround,field,value)&&value.value==8);
    }
    N2kSourceChoice depth_source;depth_source.name=b;depth_source.address=21;depth_source.mode=SourceChoiceMode::Name;
    DepthConfig depth;double adjusted;
    assert(n2k_depth_adjust(depth,10,true,-2,adjusted)&&adjusted==10);
    depth.reference=DepthReference::SensorOffset;
    assert(n2k_depth_adjust(depth,10,true,-2,adjusted)&&adjusted==8);
    assert(n2k_depth_adjust(depth,10,true,1.5,adjusted)&&adjusted==11.5);
    assert(!n2k_depth_adjust(depth,10,false,0,adjusted));
    depth.reference=DepthReference::Keel;assert(!n2k_depth_adjust(depth,10,true,-2,adjusted));
    depth.keel_set=true;depth.transducer_to_keel_m=2;
    assert(n2k_depth_adjust(depth,10,true,-2,adjusted)&&adjusted==8); // Never subtract twice.
    assert(n2k_depth_adjust(depth,1,true,-2,adjusted)&&adjusted==-1); // Negative clearance is visible.
    depth.reference=DepthReference::Waterline;depth.waterline_set=true;depth.transducer_to_waterline_m=1.5;
    assert(n2k_depth_adjust(depth,10,true,-2,adjusted)&&adjusted==11.5);
    assert(!n2k_depth_adjust(depth,1000,true,-2,adjusted));
    assert(!n2k_depth_adjust(depth,1000.01,true,-2,adjusted));
    assert(n2k_sources_save_depth(depth,&depth_source));
    fake_nvs::fail_commit=true;DepthConfig failed=depth;failed.reference=DepthReference::Transducer;
    assert(!n2k_sources_save_depth(failed,&depth_source));assert(n2k_sources_depth(&depth_source).reference==DepthReference::Waterline);
    fake_nvs::fail_commit=false;
    assert(fake_nvs::durable["sources_v1"].size()==460);
    assert(fake_nvs::durable["depth_src_v1"].size()==356);
    reboot();assert(n2k_sources_choice(0).name==b && n2k_sources_choice(N2K_GPS_CHOICE).name==name(1851,103));
    assert(n2k_sources_depth(&depth_source).reference==DepthReference::Waterline);
    assert(n2k_sources_get(DataMetric::Depth,0,value)&&!value.valid); // Selection remains locked, data cache is empty.
    n2k_sources_name(50,b);n2k_sources_publish(DataMetric::Depth,50,255,255,10);
    assert(n2k_sources_get(DataMetric::Depth,0,value)&&value.value==11.5);
    assert(n2k_sources_get(DataMetric::DepthBelowKeel,0,value)&&value.valid&&value.value==8);
    assert(n2k_sources_get(DataMetric::DepthTransducer,0,value)&&value.valid&&value.value==10);
    N2kSourceChoice other=depth_source;other.name=a;other.address=10;
    DepthConfig mounted=depth;mounted.reference=DepthReference::Keel;mounted.transducer_to_keel_m=3;mounted.transducer_to_waterline_m=0.5f;
    assert(n2k_sources_save_depth(mounted,&other));assert(n2k_sources_save_choice(4,other));
    n2k_sources_name(10,a);n2k_sources_publish(DataMetric::Depth,10,255,255,10);
    assert(n2k_sources_get(DataMetric::DepthBelowKeel,4,value)&&value.value==7);
    assert(n2k_sources_get(DataMetric::DepthWaterline,4,value)&&value.value==10.5);
    assert(n2k_sources_get(DataMetric::DepthWaterline,0,value)&&value.value==11.5);
    assert(!n2k_sources_save_depth(mounted)); // Mounting distances cannot apply to unrelated sensors.
    n2k_sources_publish(DataMetric::Depth,60,255,255,30);
    assert(n2k_sources_options(DataMetric::Depth,options,16)==3);
    assert(options[2].choice.address==60);
    assert(n2k_sources_save_choice(3,options[2].choice));reboot();
    n2k_sources_publish(DataMetric::Depth,60,255,255,99);
    assert(n2k_sources_get(DataMetric::Depth,3,value)&&!value.valid); // Address-only lock needs reselecting after reboot.
    fake_nvs::durable.erase("sources_v1");reboot();
    n2k_sources_name(10,a);n2k_sources_publish(DataMetric::Depth,10,255,255,10);
    assert(n2k_sources_get(DataMetric::Depth,0,value)&&value.value==7); // Depth profiles load independently.
    std::puts("PASS: device/instance/type isolation, five GPS units, NAME address moves, 30s expiry, >1000m rejection, depth references and NVS reboot/failure");
}
