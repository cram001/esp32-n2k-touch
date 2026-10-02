#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace victron_discovery {
struct Field { const uint8_t *data=nullptr; size_t size=0; };
// Advertisement and scan response are separate AD streams. Padding or a
// malformed field must never consume bytes belonging to the other stream.
inline Field find(const uint8_t *bytes,size_t size,uint8_t type,bool victron=false) {
    if(!bytes)return {};
    for(size_t at=0;at<size;){
        const size_t length=bytes[at];
        if(!length || length>size-at-1)return {};
        if(bytes[at+1]==type && length>=2){
            Field field{bytes+at+2,length-1};
            if(!victron || (field.size>=2 && field.data[0]==0xe1 && field.data[1]==0x02))return field;
        }
        at+=length+1;
    }
    return {};
}
inline Field manufacturer(const uint8_t *adv,size_t adv_size,const uint8_t *response,size_t response_size) {
    Field field=find(adv,adv_size,0xff,true);
    if(!field.data)field=find(response,response_size,0xff,true);
    if(field.data){field.data+=2;field.size-=2;}
    return field;
}
inline bool name(const uint8_t *adv,size_t adv_size,const uint8_t *response,size_t response_size,char *out,size_t capacity,bool *complete=nullptr) {
    if(!out || !capacity)return false;
    Field field=find(adv,adv_size,0x09);
    if(!field.data)field=find(response,response_size,0x09);
    if(complete)*complete=field.data!=nullptr;
    if(!field.data)field=find(adv,adv_size,0x08);
    if(!field.data)field=find(response,response_size,0x08);
    if(!field.data)return false;
    const size_t count=field.size<capacity-1?field.size:capacity-1;
    std::memcpy(out,field.data,count);out[count]='\0';return true;
}
}
