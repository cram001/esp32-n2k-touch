#include "n2k_waypoints.hpp"
#include <algorithm>
namespace {
uint16_t u16(const uint8_t *p){return static_cast<uint16_t>(p[0])|(static_cast<uint16_t>(p[1])<<8);}
bool text(const uint8_t *data,size_t length,size_t &at,std::array<char,33> &out) {
    if(at+2>length)return false;
    const size_t size=data[at];const uint8_t type=data[at+1];
    if(size<2 || at+size>length || type>1)return false;
    size_t written=0;bool truncated=false;
    for(size_t i=at+2;i<at+size;){
        uint32_t code=data[i++];
        if(type==0){
            if(i>=at+size)return false;
            code|=static_cast<uint32_t>(data[i++])<<8;
            if(code>=0xd800 && code<=0xdbff){
                if(i+2>at+size)return false;
                const uint16_t low=u16(data+i);i+=2;if(low<0xdc00||low>0xdfff)return false;
                code=0x10000+((code-0xd800)<<10)+(low-0xdc00);
            }else if(code>=0xdc00 && code<=0xdfff)return false;
        }
        if(code<32)code=' ';
        if(type==1 && code>=128)code='?';
        char encoded[4];size_t n=1;
        if(code<128)encoded[0]=code;
        else if(code<0x800){n=2;encoded[0]=0xc0|(code>>6);encoded[1]=0x80|(code&63);}
        else if(code<0x10000){n=3;encoded[0]=0xe0|(code>>12);encoded[1]=0x80|((code>>6)&63);encoded[2]=0x80|(code&63);}
        else{n=4;encoded[0]=0xf0|(code>>18);encoded[1]=0x80|((code>>12)&63);encoded[2]=0x80|((code>>6)&63);encoded[3]=0x80|(code&63);}
        if(!truncated && written+n<out.size()){for(size_t j=0;j<n;++j)out[written++]=encoded[j];}
        else truncated=true;
    }
    out[written]=0;at+=size;return true;
}
}
bool n2k_decode_waypoints(uint32_t pgn,const uint8_t *data,size_t length,N2kWaypoint *out,size_t capacity,size_t &count) {
    count=0;if(!data||!out||length<10||(pgn!=129285&&pgn!=130074))return false;
    const uint16_t items=u16(data+2);if(items>32)return false;
    size_t at=10;
    if(pgn==129285){
        at=9;std::array<char,33> route{};
        if(!text(data,length,at,route)||at>=length)return false;
        // PGN 129285 field 9 is a separate 8-bit Reserved field AFTER Route Name.
        // Byte 8 also contains three reserved bits, but is a different field.
        // CANboat's PGN definition and NMEA2000 SetN2kPGN129285 agree on this.
        ++at;
    }
    for(unsigned i=0;i<items;++i){
        if(at+2>length){count=0;return false;}
        N2kWaypoint item;item.id=u16(data+at);at+=2;
        if(item.id>=65533||!text(data,length,at,item.name)||at+8>length){count=0;return false;}
        at+=8;
        if(count<capacity)out[count++]=item;
    }
    return true;
}
