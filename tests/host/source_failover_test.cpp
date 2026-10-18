#include <cassert>
#include <cstdio>
#include "nvs_fake.hpp"
#include "n2k_sources.cpp"
int64_t fake_time_us=1000000;
int main(){
 fake_nvs::clear();assert(n2k_sources_init());InstrumentValue v;
 auto publish=[](DataMetric m,int a,double value){n2k_sources_publish(m,a,255,255,value);};
 publish(DataMetric::BoatSpeed,10,1);publish(DataMetric::BoatSpeed,11,2);
 assert(n2k_sources_get(DataMetric::BoatSpeed,0,v)&&v.value==1);
 fake_time_us+=30000000;publish(DataMetric::BoatSpeed,11,3);
 assert(n2k_sources_get(DataMetric::BoatSpeed,0,v)&&v.value==3&&!v.stale);
 N2kSourceOption options[3];assert(n2k_sources_options(DataMetric::BoatSpeed,options,3)==2);
 assert(n2k_sources_save_choice(0,options[0].choice));
 assert(n2k_sources_get(DataMetric::BoatSpeed,0,v)&&v.value==1&&v.stale);
 publish(DataMetric::Heading,10,1);++fake_time_us;publish(DataMetric::Heading,11,2);
 n2k_sources_invalidate(DataMetric::Heading,10,255,255);
 assert(n2k_sources_get(DataMetric::Heading,1,v)&&v.value==2&&v.valid);
 publish(DataMetric::Latitude,20,40);publish(DataMetric::Longitude,20,-120);
 fake_time_us+=30000000;publish(DataMetric::Latitude,21,41);publish(DataMetric::Longitude,21,-121);
 assert(n2k_sources_get(DataMetric::Latitude,1,v)&&v.value==41);
 assert(n2k_sources_get(DataMetric::Longitude,1,v)&&v.value==-121);
 // Reclaim stale slots and clear automatic indices before reusing them.
 fake_time_us+=600000000;
 for(unsigned i=0;i<192;++i)n2k_sources_publish(DataMetric::EngineRpm,30,i,255,i);
 n2k_sources_publish(DataMetric::BoatSpeed,31,255,255,9);
 // Table full of fresh records is bounded; after expiry a new source fits.
 fake_time_us+=600000000;publish(DataMetric::BoatSpeed,31,9);
 assert(n2k_sources_get(DataMetric::BoatSpeed,1,v)&&v.valid&&v.value==9);
 std::puts("PASS: automatic stale/invalid failover, GPS pairing, manual lock and record expiry");
}
