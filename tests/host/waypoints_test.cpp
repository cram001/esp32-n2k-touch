#include "n2k_waypoints.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
int main(){
    // Hand-authored standard PGN 129285: fields 1-7 (9 bytes), Route Name,
    // the separate field-9 reserved byte, then WP ID/name/latitude/longitude.
    // This fixture is independent of the NMEA2000 library encoder.
    const uint8_t route[]={0,0,1,0,1,0,1,0,0xe0,7,1,'R','o','u','t','e',0xff,
        42,0,6,1,'H','o','m','e',0,0,0,0,0,0,0,0};
    std::array<N2kWaypoint,16> route_out{};size_t route_count=0;
    assert(n2k_decode_waypoints(129285,route,sizeof(route),route_out.data(),route_out.size(),route_count));
    assert(route_count==1 && route_out[0].id==42 && !std::strcmp(route_out[0].name.data(),"Home"));
    for(size_t length=0;length<sizeof(route);++length){
        assert(!n2k_decode_waypoints(129285,route,length,route_out.data(),route_out.size(),route_count));assert(!route_count);
    }
    std::vector<uint8_t> missing_reserved(route,route+sizeof(route));missing_reserved.erase(missing_reserved.begin()+16);
    assert(!n2k_decode_waypoints(129285,missing_reserved.data(),missing_reserved.size(),route_out.data(),route_out.size(),route_count));
    std::array<uint8_t,sizeof(route)> high_id{};std::memcpy(high_id.data(),route,sizeof(route));high_id[17]=255;
    assert(n2k_decode_waypoints(129285,high_id.data(),high_id.size(),route_out.data(),route_out.size(),route_count));
    assert(route_out[0].id==255); // Do not confuse an ID ending in ff with the reserved byte.
    std::vector<uint8_t> packet(10,0);packet[2]=1;
    packet.insert(packet.end(),{42,0,6,1,'H','o','m','e'});packet.resize(packet.size()+8,0);
    std::array<N2kWaypoint,16> out{};size_t count=0;
    assert(n2k_decode_waypoints(130074,packet.data(),packet.size(),out.data(),out.size(),count));
    assert(count==1 && out[0].id==42 && !std::strcmp(out[0].name.data(),"Home"));
    for(size_t length=0;length<packet.size();++length){
        assert(!n2k_decode_waypoints(130074,packet.data(),length,out.data(),out.size(),count));assert(!count);
    }
    packet[13]=0; // UTF-16LE: A followed by a surrogate pair (sailboat).
    packet[12]=8;packet.resize(22,0);packet[14]='A';packet[15]=0;
    packet[16]=0x3d;packet[17]=0xd8;packet[18]=0xa5;packet[19]=0xde;packet.resize(28,0);
    assert(n2k_decode_waypoints(130074,packet.data(),packet.size(),out.data(),out.size(),count));
    assert(!std::strcmp(out[0].name.data(),"A\xf0\x9f\x9a\xa5"));
    packet[18]=0;packet[19]=0;assert(!n2k_decode_waypoints(130074,packet.data(),packet.size(),out.data(),out.size(),count));
    // Deterministic malformed-input sweep; CI also runs this under address/undefined sanitizers.
    uint32_t random=123456;
    std::array<uint8_t,223> bytes{};
    for(unsigned i=0;i<20000;++i){
        for(auto &b:bytes){random=random*1664525+1013904223;b=random>>24;}
        n2k_decode_waypoints(i&1?129285:130074,bytes.data(),i%224,out.data(),i%17,count);
        assert(count<=i%17);
    }
    std::puts("PASS: waypoint names, every truncation boundary, UTF-16 surrogate validation and 20,000 malformed packets");
}
