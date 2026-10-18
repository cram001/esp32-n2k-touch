#include <cassert>
#include <cstdio>
#include "n2k_sources.cpp"
#include "nvs_fake.hpp"
int64_t fake_time_us=1000000;
int main(){
 assert(n2k_sources_init());
 for(unsigned a=0;a<200;++a)n2k_sources_invalidate(DataMetric::Depth,a);
 size_t count=0;for(const auto &d:devices)count+=d.used;
 assert(count==0);
 Record r;r.source.address=201;char text[100];label(r,0,text,sizeof(text));
 for(const auto &d:devices)assert(!d.used);
 n2k_sources_name(202,(229ULL<<21)|1);
 n2k_sources_publish(DataMetric::Depth,202,255,255,5);
 N2kSourceOption option;assert(n2k_sources_options(DataMetric::Depth,&option,1)==1);
 assert(option.choice.name && std::strstr(option.label.data(),"Garmin"));
 ProfileBytes bytes{};bytes[0]=1;bytes[4]=1;bytes[5]=1;bytes[17]=255;
 fake_nvs::durable["depth_src_v1"]={bytes.begin(),bytes.end()};load_profiles();
 assert(!depth_profiles[0].used);
 std::puts("PASS: invalidate and label lookups preserve device slots");
}
