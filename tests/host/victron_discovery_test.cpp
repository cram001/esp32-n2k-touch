#include "victron_discovery.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <random>
using namespace victron_discovery;
int main(){
    // Independent BLE AD fixtures: company ID little-endian, product data,
    // padding in advertisement, complete device name only in scan response.
    const uint8_t adv[]={2,1,6,5,0xff,0xe1,0x02,0x10,0x01,0,0};
    const uint8_t response[]={10,0x09,'H','o','u','s','e',' ','B','a','t'};
    auto field=manufacturer(adv,sizeof(adv),response,sizeof(response));
    assert(field.data && field.size==2 && field.data[0]==0x10);
    char text[25]{};bool complete=false;
    assert(name(adv,sizeof(adv),response,sizeof(response),text,sizeof(text),&complete));
    assert(!std::strcmp(text,"House Bat") && complete);
    const uint8_t short_name[]={3,0x08,'A','B'};
    assert(name(short_name,sizeof(short_name),response,sizeof(response),text,sizeof(text),&complete));
    assert(!std::strcmp(text,"House Bat") && complete); // Complete wins over abbreviated.
    assert(name(short_name,sizeof(short_name),nullptr,0,text,sizeof(text),&complete) && !complete);
    const uint8_t several[]={3,0xff,0x4c,0,4,0xff,0xe1,0x02,0x10};
    field=manufacturer(several,sizeof(several),nullptr,0);
    assert(field.data && field.size==1 && field.data[0]==0x10);
    const uint8_t unrelated[]={3,0xff,0x4c,0};
    assert(!manufacturer(unrelated,sizeof(unrelated),response,sizeof(response)).data);
    assert(manufacturer(nullptr,0,adv,sizeof(adv)).data); // Manufacturer in response.
    const uint8_t malformed[]={30,0x09,'X'};
    assert(!name(malformed,sizeof(malformed),nullptr,0,text,sizeof(text)));
    assert(name(malformed,sizeof(malformed),response,sizeof(response),text,sizeof(text)));
    assert(!manufacturer(adv,5,nullptr,0).data); // Truncated manufacturer field.
    char tiny[2]={'?','?'};
    assert(name(nullptr,0,response,sizeof(response),tiny,sizeof(tiny)) && tiny[0]=='H' && tiny[1]==0);
    assert(!name(adv,sizeof(adv),response,sizeof(response),nullptr,0));
    // Exercise every input length plus random malformed AD streams. CI runs
    // this with ASan/UBSan so a parser overread fails the job.
    std::mt19937 rng(1234);
    for(unsigned n=0;n<20000;++n){
        std::vector<uint8_t> a(rng()%32),b(rng()%32);
        for(auto &x:a)x=static_cast<uint8_t>(rng());
        for(auto &x:b)x=static_cast<uint8_t>(rng());
        manufacturer(a.data(),a.size(),b.data(),b.size());
        name(a.data(),a.size(),b.data(),b.size(),text,sizeof(text));
    }
    std::puts("PASS: padded/separate AD streams, scan-response names, complete-name preference, Victron filtering, truncation and 20000 malformed packets");
}
