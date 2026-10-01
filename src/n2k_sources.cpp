#include "n2k_sources.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

namespace {
constexpr size_t METRICS = static_cast<size_t>(DataMetric::Count);
constexpr size_t MAX_RECORDS = 192, MAX_DEVICES = 64;
constexpr uint16_t NO_RECORD = UINT16_MAX;
struct Record {
    bool used = false;
    DataMetric metric = DataMetric::None;
    N2kSourceChoice source{};
    InstrumentValue value{};
    int64_t updated = 0;
};
struct Device { bool used = false; uint8_t address = 255; uint64_t name = 0; std::array<char,33> model{}; };
struct Preferences {
    std::array<N2kSourceChoice, N2K_SOURCE_CHOICES + 1> sources{};
    DepthConfig depth{};
};
std::array<Record,MAX_RECORDS> records{};
std::array<Device,MAX_DEVICES> devices{};
std::array<uint16_t,METRICS> automatic = [] { std::array<uint16_t,METRICS> a{}; a.fill(NO_RECORD); return a; }();
N2kSourceChoice automatic_gps{};
Preferences preferences{};
struct DepthProfile { bool used = false; N2kSourceChoice source{}; DepthConfig config{}; };
std::array<DepthProfile,16> depth_profiles{};
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
bool initialized = false;
using Bytes = std::array<uint8_t,460>;

Device *device(uint8_t address) {
    Device *free = nullptr;
    for (auto &d : devices) { if (d.used && d.address == address) return &d; if (!d.used && !free) free = &d; }
    if (free) { free->used = true; free->address = address; }
    return free;
}
bool same_device(const N2kSourceChoice &a, const N2kSourceChoice &b) {
    return a.name ? a.name == b.name : a.address < 254 && a.address == b.address;
}
bool matches(const N2kSourceChoice &a, const N2kSourceChoice &b) {
    return same_device(a,b) && a.instance == b.instance && a.kind == b.kind;
}
bool temporary_source_exists(const N2kSourceChoice &source) {
    bool found=false;portENTER_CRITICAL(&mux);
    for(const auto &r:records)if(r.used && !r.source.name && matches(source,r.source)){found=true;break;}
    portEXIT_CRITICAL(&mux);return found;
}
uint8_t choice_id(DataMetric metric, uint8_t field) {
    return n2k_sources_is_gps(metric) ? N2K_GPS_CHOICE : field;
}
uint16_t selected(DataMetric metric, uint8_t field) {
    if(n2k_sources_is_depth(metric))metric=DataMetric::Depth;
    const size_t m = static_cast<size_t>(metric);
    if (m >= METRICS) return NO_RECORD;
    const uint8_t id = choice_id(metric,field);
    const N2kSourceChoice choice = id <= N2K_GPS_CHOICE ? preferences.sources[id] : N2kSourceChoice{};
    if (choice.mode != SourceChoiceMode::Automatic || n2k_sources_is_gps(metric)) {
        const auto &target = choice.mode != SourceChoiceMode::Automatic ? choice : automatic_gps;
        for (size_t i=0;i<records.size();++i) {
            const auto &r=records[i];
            if (r.used && r.metric==metric && (n2k_sources_is_gps(metric) ? same_device(target,r.source) : matches(target,r.source))) return i;
        }
        return NO_RECORD;
    }
    return automatic[m];
}
void label(const Record &r, size_t index, char *out, size_t capacity) {
    const Device *d = device(r.source.address);
    const char *model = d && d->name == r.source.name && d->model[0] ? d->model.data() : "N2K device";
    char instance[32]{};
    if (r.metric >= DataMetric::EngineRpm && r.metric <= DataMetric::EngineTorque) std::snprintf(instance,sizeof(instance),"Engine %u",r.source.instance);
    else if (r.metric == DataMetric::TankLevel || r.metric == DataMetric::TankCapacity) {
        const char *fluid[] = {"Fuel","Water","Grey water","Live well","Oil","Sewage","Gasoline"};
        std::snprintf(instance,sizeof(instance),"%s tank %u",r.source.kind<7?fluid[r.source.kind]:"Fluid",r.source.instance);
    } else if (r.metric==DataMetric::TrueWindSpeed || r.metric==DataMetric::TrueWindAngle) {
        const char *wind=r.source.kind==0?"True north":r.source.kind==1?"Magnetic":r.source.kind==3?"True boat":"True water";
        std::snprintf(instance,sizeof(instance),"%s wind %u",wind,static_cast<unsigned>(index));
    } else if (r.source.instance != 255) std::snprintf(instance,sizeof(instance),"Sensor %u/type %u",r.source.instance,r.source.kind);
    else std::snprintf(instance,sizeof(instance),"%s %u",n2k_sources_is_gps(r.metric)?"GPS":r.metric==DataMetric::Depth?"Depth":"Source",static_cast<unsigned>(index));
    const unsigned maker_code=static_cast<unsigned>((r.source.name>>21)&0x7ff);
    const char *maker=nullptr;
    // Manufacturer assignments: CANboat's factual MANUFACTURER_CODE registry.
    switch(maker_code){case 229:maker="Garmin";break;case 1851:maker="Raymarine";break;
    case 135:maker="Airmar";break;case 137:maker="Maretron";break;case 273:maker="Actisense";break;
    case 275:maker="Navico";break;case 1857:maker="Simrad";break;case 1855:maker="Furuno";break;default:break;}
    char manufacturer[20];if(maker)std::snprintf(manufacturer,sizeof(manufacturer),"%s",maker);
    else if(r.source.name)std::snprintf(manufacturer,sizeof(manufacturer),"Maker %u",maker_code);
    else std::snprintf(manufacturer,sizeof(manufacturer),"Unknown maker");
    if(r.source.name)std::snprintf(out,capacity,"%s: %s %.16s [dev %u/addr %u]",instance,manufacturer,model,
        static_cast<unsigned>((r.source.name>>32)&255),r.source.address);
    else std::snprintf(out,capacity,"%s: %.16s [addr %u] (this boot)",instance,model,r.source.address);
}
void encode(Bytes &b,const Preferences &p) {
    b.fill(0);b[0]=1;size_t at=4;
    for(const auto &s:p.sources){
        b[at++]=static_cast<uint8_t>(s.mode);
        for(unsigned i=0;i<8;++i)b[at++]=static_cast<uint8_t>(s.name>>(i*8));
        b[at++]=s.address;b[at++]=s.instance;b[at++]=s.kind;
    }
    b[at++]=static_cast<uint8_t>(p.depth.reference);
    b[at++]=(p.depth.keel_set?1:0)|(p.depth.waterline_set?2:0);
    // ESP32 and supported host tests use IEEE-754 little-endian float32.
    std::memcpy(b.data()+at,&p.depth.transducer_to_keel_m,4);at+=4;
    std::memcpy(b.data()+at,&p.depth.transducer_to_waterline_m,4);
}
bool depth_valid(const DepthConfig &c) {
    return static_cast<uint8_t>(c.reference)<=3 && std::isfinite(c.transducer_to_keel_m) && std::isfinite(c.transducer_to_waterline_m) &&
        c.transducer_to_keel_m>=0 && c.transducer_to_keel_m<=100 && c.transducer_to_waterline_m>=0 && c.transducer_to_waterline_m<=100;
}
DepthConfig depth_for(const N2kSourceChoice &source) {
    for(const auto &p:depth_profiles)if(p.used && matches(p.source,source))return p.config;
    return preferences.depth;
}
using ProfileBytes=std::array<uint8_t,356>;
void load_profiles() {
    ProfileBytes bytes{};nvs_handle_t handle;size_t length=bytes.size();depth_profiles={};
    if(nvs_open("app",NVS_READONLY,&handle)!=ESP_OK)return;
    const esp_err_t error=nvs_get_blob(handle,"depth_src_v1",bytes.data(),&length);nvs_close(handle);
    if(error!=ESP_OK || length!=bytes.size() || bytes[0]!=1 || bytes[1] || bytes[2] || bytes[3])return;
    size_t at=4;
    for(auto &p:depth_profiles){
        const uint8_t mode=bytes[at++];
        for(unsigned i=0;i<8;++i)p.source.name|=static_cast<uint64_t>(bytes[at++])<<(i*8);
        p.source.address=bytes[at++];p.source.instance=bytes[at++];p.source.kind=bytes[at++];
        p.config.reference=static_cast<DepthReference>(bytes[at++]);const uint8_t flags=bytes[at++];
        p.config.keel_set=flags&1;p.config.waterline_set=flags&2;
        std::memcpy(&p.config.transducer_to_keel_m,bytes.data()+at,4);at+=4;
        std::memcpy(&p.config.transducer_to_waterline_m,bytes.data()+at,4);at+=4;
        p.used=mode==1 && p.source.name && p.source.name!=UINT64_MAX && depth_valid(p.config);
        p.source.mode=SourceChoiceMode::Name;
    }
}
bool persist(const Preferences &next) {
    Bytes bytes;encode(bytes,next);nvs_handle_t handle;
    if(nvs_open("app",NVS_READWRITE,&handle)!=ESP_OK)return false;
    esp_err_t error=nvs_set_blob(handle,"sources_v1",bytes.data(),bytes.size());
    if(error==ESP_OK)error=nvs_commit(handle);
    nvs_close(handle);if(error!=ESP_OK)return false;
    portENTER_CRITICAL(&mux);preferences=next;portEXIT_CRITICAL(&mux);return true;
}
}
bool n2k_sources_is_gps(DataMetric m) {
    return m==DataMetric::Latitude || m==DataMetric::Longitude || m==DataMetric::Altitude || m==DataMetric::SpeedOverGround || m==DataMetric::CourseOverGround;
}
bool n2k_sources_is_depth(DataMetric m) {
    return m==DataMetric::Depth || m==DataMetric::DepthTransducer || m==DataMetric::DepthBelowKeel || m==DataMetric::DepthWaterline || m==DataMetric::DepthSensorOffset;
}
bool n2k_sources_init() {
    if(initialized)return true;
    initialized=true;n2k_sources_reset();preferences={};depth_profiles={};
    nvs_handle_t handle;const esp_err_t opened=nvs_open("app",NVS_READONLY,&handle);
    if(opened==ESP_ERR_NVS_NOT_FOUND)return true;
    if(opened!=ESP_OK)return false;
    Bytes bytes{};size_t size=bytes.size();const esp_err_t read=nvs_get_blob(handle,"sources_v1",bytes.data(),&size);nvs_close(handle);
    load_profiles();
    if(read!=ESP_OK || size!=bytes.size() || bytes[0]!=1 || bytes[1] || bytes[2] || bytes[3])return true;
    Preferences decoded{};size_t at=4;
    for(auto &s:decoded.sources){
        const uint8_t mode=bytes[at++];
        for(unsigned i=0;i<8;++i)s.name|=static_cast<uint64_t>(bytes[at++])<<(i*8);
        s.address=bytes[at++];s.instance=bytes[at++];s.kind=bytes[at++];
        if(mode==1 && s.name && s.name!=UINT64_MAX)s.mode=SourceChoiceMode::Name;
        else if(mode==2){s.mode=SourceChoiceMode::ThisBootAddress;s.address=255;s.name=0;}
        else s={};
    }
    decoded.depth.reference=static_cast<DepthReference>(bytes[at++]);const uint8_t flags=bytes[at++];
    decoded.depth.keel_set=flags&1;decoded.depth.waterline_set=flags&2;
    std::memcpy(&decoded.depth.transducer_to_keel_m,bytes.data()+at,4);at+=4;
    std::memcpy(&decoded.depth.transducer_to_waterline_m,bytes.data()+at,4);
    if(!depth_valid(decoded.depth))decoded.depth={};
    if(decoded.depth.reference==DepthReference::Keel || decoded.depth.reference==DepthReference::Waterline)decoded.depth.reference=DepthReference::Transducer;
    decoded.depth.keel_set=false;decoded.depth.waterline_set=false;
    decoded.depth.transducer_to_keel_m=0;decoded.depth.transducer_to_waterline_m=0;
    preferences=decoded;return true;
}
void n2k_sources_reset() {
    portENTER_CRITICAL(&mux);records={};devices={};automatic.fill(NO_RECORD);automatic_gps={};
    for(auto &s:preferences.sources)if(s.mode==SourceChoiceMode::ThisBootAddress){s.address=255;s.name=0;}
    for(auto &p:depth_profiles)if(p.source.mode==SourceChoiceMode::ThisBootAddress)p.used=false;
    portEXIT_CRITICAL(&mux);
}
void n2k_sources_name(uint8_t address,uint64_t name) {
    if(address>=254 || !name || name==UINT64_MAX)return;
    portENTER_CRITICAL(&mux);
    Device *d=device(address);
    if(d){
        if(d->name && d->name!=name)d->model={};
        for(auto &other:devices)if(&other!=d && other.used && other.name==name){d->model=other.model;other.address=255;}
        d->name=name;
        for(auto &s:preferences.sources)if(s.mode==SourceChoiceMode::ThisBootAddress && s.address==address){s.address=255;s.name=0;}
        for(auto &p:depth_profiles)if(p.used && p.source.mode==SourceChoiceMode::ThisBootAddress && p.source.address==address)p.used=false;
        for(auto &r:records)if(r.used && (r.source.name==name || (!r.source.name && r.source.address==address))){
            if(!r.source.name)r.value.valid=false; // Do not relabel historical unidentified values.
            r.source.name=name;r.source.address=address;r.source.mode=SourceChoiceMode::Name;
        }
        if(!automatic_gps.name && automatic_gps.address==address)automatic_gps.name=name;
    }
    portEXIT_CRITICAL(&mux);
}
void n2k_sources_model(uint8_t address,const char *model) {
    if(address>=254 || !model)return;
    portENTER_CRITICAL(&mux);Device *d=device(address);if(d)std::snprintf(d->model.data(),d->model.size(),"%s",model);portEXIT_CRITICAL(&mux);
}
void n2k_sources_publish(DataMetric metric,uint8_t address,uint8_t instance,uint8_t kind,double value,HeadingReference reference,bool offset_valid,double offset,const char *text) {
    const size_t m=static_cast<size_t>(metric);
    if(address>=254 || !m || m>=METRICS || !std::isfinite(value))return;
    const int64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&mux);
    Device *d=device(address);N2kSourceChoice source;
    source.name=d?d->name:0;source.address=address;source.instance=instance;source.kind=kind;
    source.mode=source.name?SourceChoiceMode::Name:SourceChoiceMode::ThisBootAddress;
    size_t index=records.size(),free=records.size();
    for(size_t i=0;i<records.size();++i){
        if(records[i].used && records[i].metric==metric && matches(source,records[i].source)){index=i;break;}
        if(!records[i].used && free==records.size())free=i;
    }
    if(index==records.size())index=free;
    if(index<records.size()){
        Record &r=records[index];r.used=true;r.metric=metric;r.source=source;r.updated=now;r.value={};
        r.value.valid=metric!=DataMetric::Depth || (value>=0 && value<=1000);
        r.value.value=value;r.value.heading_reference=reference;
        r.value.depth_offset_valid=offset_valid && std::isfinite(offset) && std::abs(offset)<=100;
        r.value.depth_offset_m=offset;
        if(text)std::snprintf(r.value.text.data(),r.value.text.size(),"%s",text);
        if(automatic[m]==NO_RECORD)automatic[m]=index;
        if(n2k_sources_is_gps(metric) && automatic_gps.address==255 && !automatic_gps.name)automatic_gps=source;
    }
    portEXIT_CRITICAL(&mux);
}
bool n2k_depth_adjust(const DepthConfig &c,double raw,bool valid,double offset,double &result) {
    if(!depth_valid(c) || !std::isfinite(raw) || raw<0 || raw>1000)return false;
    result=raw;
    switch(c.reference){
    case DepthReference::Transducer:break;
    case DepthReference::SensorOffset:if(!valid || !std::isfinite(offset))return false;result+=offset;break;
    case DepthReference::Keel:if(!c.keel_set)return false;result-=c.transducer_to_keel_m;break;
    case DepthReference::Waterline:if(!c.waterline_set)return false;result+=c.transducer_to_waterline_m;break;
    }
    return std::isfinite(result) && result<=1000;
}
bool n2k_sources_get(DataMetric metric,uint8_t field,InstrumentValue &out) {
    const int64_t now=esp_timer_get_time();Record copy;DepthConfig depth;bool handled=false;Record variation;
    portENTER_CRITICAL(&mux);
    const uint16_t i=selected(metric,field);
    const uint8_t id=choice_id(metric,field);
    handled=(id<=N2K_GPS_CHOICE && preferences.sources[id].mode!=SourceChoiceMode::Automatic);
    if(i!=NO_RECORD){copy=records[i];handled=true;}
    const N2kSourceChoice target=copy.used?copy.source:(id<=N2K_GPS_CHOICE?preferences.sources[id]:N2kSourceChoice{});
    depth=depth_for(target);
    if(copy.used){
        for(const auto &r:records)if(r.used && r.metric==DataMetric::MagneticVariation && same_device(copy.source,r.source)){variation=r;break;}
        const auto &gps=preferences.sources[N2K_GPS_CHOICE].mode==SourceChoiceMode::Automatic?automatic_gps:preferences.sources[N2K_GPS_CHOICE];
        if(!variation.used)for(const auto &r:records)if(r.used && r.metric==DataMetric::MagneticVariation && same_device(gps,r.source)){variation=r;break;}
    }
    portEXIT_CRITICAL(&mux);
    if(metric==DataMetric::DepthTransducer)depth.reference=DepthReference::Transducer;
    else if(metric==DataMetric::DepthBelowKeel)depth.reference=DepthReference::Keel;
    else if(metric==DataMetric::DepthWaterline)depth.reference=DepthReference::Waterline;
    else if(metric==DataMetric::DepthSensorOffset)depth.reference=DepthReference::SensorOffset;
    out=copy.value;out.depth_reference=static_cast<uint8_t>(depth.reference);
    if(!copy.used)return handled;
    out.age_ms=static_cast<uint32_t>(std::max<int64_t>(0,now-copy.updated)/1000);
    out.stale=out.age_ms>=30000;
    out.variation_valid=variation.used && variation.value.valid && now-variation.updated<30000000;
    out.variation_radians=variation.value.value;
    if(n2k_sources_is_depth(metric) && out.valid)out.valid=n2k_depth_adjust(depth,out.value,out.depth_offset_valid,out.depth_offset_m,out.value);
    return true;
}
size_t n2k_sources_options(DataMetric metric,N2kSourceOption *out,size_t capacity) {
    if(n2k_sources_is_depth(metric))metric=DataMetric::Depth;
    if(!out)return 0;
    const int64_t now=esp_timer_get_time();size_t count=0;
    portENTER_CRITICAL(&mux);
    for(const auto &r:records){
        if(!r.used || r.metric!=metric || count>=capacity)continue;
        out[count].choice=r.source;out[count].stale=now-r.updated>=30000000;
        label(r,count,out[count].label.data(),out[count].label.size());++count;
    }
    portEXIT_CRITICAL(&mux);return count;
}
void n2k_sources_label(DataMetric metric,uint8_t field,char *out,size_t capacity) {
    if(n2k_sources_is_depth(metric))metric=DataMetric::Depth;
    if(!out || !capacity)return;
    portENTER_CRITICAL(&mux);const uint16_t i=selected(metric,field);const uint8_t id=choice_id(metric,field);
    if(i!=NO_RECORD){size_t number=0;for(size_t j=0;j<i;++j)if(records[j].used && records[j].metric==metric)++number;label(records[i],number,out,capacity);}
    else std::snprintf(out,capacity,"%s",id<=N2K_GPS_CHOICE && preferences.sources[id].mode!=SourceChoiceMode::Automatic?"Selected source unavailable":"Waiting for N2K source");
    portEXIT_CRITICAL(&mux);
}
N2kSourceChoice n2k_sources_choice(uint8_t id) {
    N2kSourceChoice choice;portENTER_CRITICAL(&mux);if(id<=N2K_GPS_CHOICE)choice=preferences.sources[id];portEXIT_CRITICAL(&mux);return choice;
}
bool n2k_sources_save_choice(uint8_t id,const N2kSourceChoice &choice) {
    if(id>N2K_GPS_CHOICE || static_cast<uint8_t>(choice.mode)>2)return false;
    if(choice.mode==SourceChoiceMode::Name && (!choice.name || choice.name==UINT64_MAX))return false;
    if(choice.mode==SourceChoiceMode::ThisBootAddress && (choice.address>=254 || !temporary_source_exists(choice)))return false;
    Preferences next;portENTER_CRITICAL(&mux);next=preferences;portEXIT_CRITICAL(&mux);next.sources[id]=choice;
    return persist(next);
}
DepthConfig n2k_sources_depth(const N2kSourceChoice *source) { portENTER_CRITICAL(&mux);const DepthConfig copy=source?depth_for(*source):preferences.depth;portEXIT_CRITICAL(&mux);return copy; }
bool n2k_sources_save_depth(const DepthConfig &depth,const N2kSourceChoice *source) {
    if(!depth_valid(depth))return false;
    if(source && static_cast<uint8_t>(source->mode)>2)return false;
    if(source && source->mode!=SourceChoiceMode::Automatic){
        if(source->mode==SourceChoiceMode::Name && (!source->name || source->name==UINT64_MAX))return false;
        if(source->mode==SourceChoiceMode::ThisBootAddress && (source->address>=254 || !temporary_source_exists(*source)))return false;
        std::array<DepthProfile,16> next;
        portENTER_CRITICAL(&mux);next=depth_profiles;portEXIT_CRITICAL(&mux);
        auto slot=std::find_if(next.begin(),next.end(),[&](const DepthProfile &p){return p.used && matches(p.source,*source);});
        if(slot==next.end())slot=std::find_if(next.begin(),next.end(),[](const DepthProfile&p){return !p.used;});
        if(slot==next.end())return false;
        slot->used=true;slot->source=*source;slot->config=depth;
        ProfileBytes bytes{};bytes[0]=1;size_t at=4;
        for(const auto &p:next){
            bytes[at++]=p.used?static_cast<uint8_t>(p.source.mode):0;
            for(unsigned i=0;i<8;++i)bytes[at++]=static_cast<uint8_t>(p.source.name>>(i*8));
            bytes[at++]=p.source.address;bytes[at++]=p.source.instance;bytes[at++]=p.source.kind;
            bytes[at++]=static_cast<uint8_t>(p.config.reference);bytes[at++]=(p.config.keel_set?1:0)|(p.config.waterline_set?2:0);
            std::memcpy(bytes.data()+at,&p.config.transducer_to_keel_m,4);at+=4;
            std::memcpy(bytes.data()+at,&p.config.transducer_to_waterline_m,4);at+=4;
        }
        nvs_handle_t handle;if(nvs_open("app",NVS_READWRITE,&handle)!=ESP_OK)return false;
        esp_err_t error=nvs_set_blob(handle,"depth_src_v1",bytes.data(),bytes.size());if(error==ESP_OK)error=nvs_commit(handle);nvs_close(handle);
        if(error!=ESP_OK)return false;
        portENTER_CRITICAL(&mux);depth_profiles=next;portEXIT_CRITICAL(&mux);return true;
    }
    // Manual mounting distances belong to one physical sensor, not every source.
    if(depth.reference==DepthReference::Keel || depth.reference==DepthReference::Waterline || depth.keel_set || depth.waterline_set)return false;
    Preferences next;portENTER_CRITICAL(&mux);next=preferences;portEXIT_CRITICAL(&mux);next.depth=depth;return persist(next);
}
