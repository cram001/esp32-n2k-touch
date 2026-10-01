#include "n2k_instruments.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include "N2kMessages.h"
#include "n2k_sources.hpp"
#include "n2k_waypoints.hpp"

namespace {
struct Waypoint { bool used=false;uint8_t source=255;N2kWaypoint item; };
std::array<Waypoint,32> waypoints{};
std::array<uint64_t,254> names{};
std::array<uint32_t,254> targets = [] {std::array<uint32_t,254> t{};t.fill(UINT32_MAX);return t;}();
void publish(const tN2kMsg &msg,DataMetric metric,double value,uint8_t instance=255,uint8_t kind=255,HeadingReference reference=HeadingReference::Unknown) {
    if(value!=N2kDoubleNA && std::isfinite(value))n2k_sources_publish(metric,msg.Source,instance,kind,value,reference);
}
HeadingReference reference(tN2kHeadingReference ref) {
    return ref==N2khr_true?HeadingReference::True:ref==N2khr_magnetic?HeadingReference::Magnetic:HeadingReference::Unknown;
}
void temperature(const tN2kMsg &msg,tN2kTempSource source,uint8_t instance,double value) {
    DataMetric metric=DataMetric::EnvironmentalTemperature;
    switch(source){
    case N2kts_SeaTemperature:metric=DataMetric::WaterTemperature;break;
    case N2kts_OutsideTemperature:metric=DataMetric::AirTemperature;break;
    case N2kts_InsideTemperature:case N2kts_MainCabinTemperature:metric=DataMetric::CabinTemperature;break;
    case N2kts_EngineRoomTemperature:metric=DataMetric::EngineRoomTemperature;break;
    case N2kts_ExhaustGasTemperature:metric=DataMetric::EngineExhaustTemperature;break;
    default:break;
    }
    publish(msg,metric,value,instance,static_cast<uint8_t>(source));
}
void waypoint_name(const tN2kMsg &msg) {
    for(const auto &w:waypoints)if(w.used && w.source==msg.Source && w.item.id==targets[msg.Source] && w.item.name[0]){
        n2k_sources_publish(DataMetric::WaypointName,msg.Source,255,255,0,HeadingReference::Unknown,false,0,w.item.name.data());return;
    }
    // A changed target without a matching name must not retain the old name.
    n2k_sources_publish(DataMetric::WaypointName,msg.Source,255,255,0,HeadingReference::Unknown,false,0,"");
}
}
void n2k_instruments_reset(){waypoints={};names={};targets.fill(UINT32_MAX);}
void n2k_instruments_receive(const tN2kMsg &msg) {
    if(msg.Source>=254)return;
    if(msg.PGN==60928 && msg.DataLen==8){
        uint64_t name=0;for(unsigned i=0;i<8;++i)name|=static_cast<uint64_t>(msg.Data[i])<<(8*i);
        if(!name || name==UINT64_MAX)return;
        if(names[msg.Source]!=name){
            for(auto &w:waypoints)if(w.used && w.source==msg.Source)w={};
            targets[msg.Source]=UINT32_MAX;
            names[msg.Source]=name;
        }
        n2k_sources_name(msg.Source,name);return;
    }
    if(msg.PGN==126996 && msg.DataLen>=134){
        char model[33]{};for(unsigned i=0;i<32 && msg.Data[4+i] && msg.Data[4+i]!=255;++i)model[i]=msg.Data[4+i]>=32&&msg.Data[4+i]<127?msg.Data[4+i]:'?';
        n2k_sources_model(msg.Source,model);return;
    }
    if(msg.PGN==129285 || msg.PGN==130074){
        std::array<N2kWaypoint,16> decoded{};size_t count=0;
        if(!n2k_decode_waypoints(msg.PGN,msg.Data,msg.DataLen,decoded.data(),decoded.size(),count))return;
        for(size_t i=0;i<count;++i){
            auto found=std::find_if(waypoints.begin(),waypoints.end(),[&](const Waypoint &w){return w.used&&w.source==msg.Source&&w.item.id==decoded[i].id;});
            if(found==waypoints.end())found=std::find_if(waypoints.begin(),waypoints.end(),[](const Waypoint&w){return !w.used;});
            if(found!=waypoints.end()){found->used=true;found->source=msg.Source;found->item=decoded[i];}
        }
        waypoint_name(msg);return;
    }
    const int minimum=msg.PGN==127489?26:msg.PGN==129029?43:msg.PGN==129284?34:msg.PGN==128275?14:8;
    if(msg.DataLen<minimum)return;
    switch(msg.PGN){
    case 127250:{
        unsigned char sid;double h,d,v;tN2kHeadingReference ref;
        if(ParseN2kPGN127250(msg,sid,h,d,v,ref)){
            if(v!=N2kDoubleNA && std::abs(v)<=3.141592653589793)publish(msg,DataMetric::MagneticVariation,v);
            if(reference(ref)!=HeadingReference::Unknown)publish(msg,DataMetric::Heading,h,255,255,reference(ref));
        }break;
    }
    case 127258:{unsigned char sid;tN2kMagneticVariation source;uint16_t days;double v;
        if(ParseN2kPGN127258(msg,sid,source,days,v) && v!=N2kDoubleNA && std::abs(v)<=3.141592653589793)publish(msg,DataMetric::MagneticVariation,v);
        break;}
    case 127488:{unsigned char instance;double rpm,boost;int8_t trim;
        if(ParseN2kPGN127488(msg,instance,rpm,boost,trim)&&instance<2){publish(msg,DataMetric::EngineRpm,rpm,instance);publish(msg,DataMetric::EngineBoostPressure,boost,instance);}break;}
    case 127489:{unsigned char instance;double oilp,oilt,coolt,voltage,rate,hours,coolp,fuelp;int8_t load,torque;
        if(ParseN2kPGN127489(msg,instance,oilp,oilt,coolt,voltage,rate,hours,coolp,fuelp,load,torque)&&instance<2){
            publish(msg,DataMetric::EngineOilPressure,oilp,instance);publish(msg,DataMetric::EngineOilTemperature,oilt,instance);
            publish(msg,DataMetric::EngineCoolantTemperature,coolt,instance);publish(msg,DataMetric::EngineAlternatorVoltage,voltage,instance);
            publish(msg,DataMetric::EngineFuelRate,rate,instance);publish(msg,DataMetric::EngineHours,hours,instance);
            publish(msg,DataMetric::EngineCoolantPressure,coolp,instance);publish(msg,DataMetric::EngineFuelPressure,fuelp,instance);
            if(load>=0&&load<=100)publish(msg,DataMetric::EngineLoad,load,instance);
            if(torque>=-100&&torque<=100)publish(msg,DataMetric::EngineTorque,torque,instance);
        }break;
    }
    case 127505:{unsigned char instance;tN2kFluidType fluid;double level,capacity;
        if(ParseN2kPGN127505(msg,instance,fluid,level,capacity)&&static_cast<uint8_t>(fluid)<7){
            if(level>=0&&level<=100)publish(msg,DataMetric::TankLevel,level,instance,fluid);
            publish(msg,DataMetric::TankCapacity,capacity,instance,fluid);
        }break;
    }
    case 128259:{unsigned char sid;double water,ground;tN2kSpeedWaterReferenceType ref;
        if(ParseN2kPGN128259(msg,sid,water,ground,ref))publish(msg,DataMetric::BoatSpeed,water);
        break;}
    case 128267:{unsigned char sid;double depth,offset,range;
        if(ParseN2kPGN128267(msg,sid,depth,offset,range)&&depth!=N2kDoubleNA)n2k_sources_publish(DataMetric::Depth,msg.Source,255,255,depth,HeadingReference::Unknown,offset!=N2kDoubleNA,offset);
        break;}
    case 128275:{uint16_t days;double seconds;uint32_t log,trip;
        if(ParseN2kPGN128275(msg,days,seconds,log,trip)&&trip!=N2kUInt32NA)publish(msg,DataMetric::TripDistance,trip);
        break;}
    case 129025:{double lat,lon;if(ParseN2kPGN129025(msg,lat,lon)){
        if(lat!=N2kDoubleNA&&std::abs(lat)<=90)publish(msg,DataMetric::Latitude,lat);
        if(lon!=N2kDoubleNA&&std::abs(lon)<=180)publish(msg,DataMetric::Longitude,lon);
    }break;}
    case 129026:{unsigned char sid;tN2kHeadingReference ref;double cog,sog;
        if(ParseN2kPGN129026(msg,sid,ref,cog,sog)){publish(msg,DataMetric::CourseOverGround,cog,255,255,reference(ref));publish(msg,DataMetric::SpeedOverGround,sog);}break;}
    case 129029:{unsigned char sid,sats,stations;uint16_t days,station;double seconds,lat,lon,alt,hdop,pdop,geoid,age;tN2kGNSStype type,station_type;tN2kGNSSmethod method;
        if(ParseN2kPGN129029(msg,sid,days,seconds,lat,lon,alt,type,method,sats,hdop,pdop,geoid,stations,station_type,station,age) && static_cast<unsigned>(method)>0 && static_cast<unsigned>(method)<=8){
            if(lat!=N2kDoubleNA&&std::abs(lat)<=90)publish(msg,DataMetric::Latitude,lat);
            if(lon!=N2kDoubleNA&&std::abs(lon)<=180)publish(msg,DataMetric::Longitude,lon);
            publish(msg,DataMetric::Altitude,alt);
        }break;
    }
    case 129283:{unsigned char sid;tN2kXTEMode mode;bool terminated;double xte;
        if(ParseN2kPGN129283(msg,sid,mode,terminated,xte)&&!terminated)publish(msg,DataMetric::CrossTrackError,xte);
        break;}
    case 129284:{unsigned char sid;double distance,eta,origin_bearing,bearing,lat,lon,vmg;tN2kHeadingReference ref;bool crossed,arrived;tN2kDistanceCalculationType calc;int16_t date;uint32_t origin,destination;
        if(ParseN2kPGN129284(msg,sid,distance,ref,crossed,arrived,calc,eta,date,origin_bearing,bearing,origin,destination,lat,lon,vmg)){
            publish(msg,DataMetric::DistanceToWaypoint,distance);publish(msg,DataMetric::WaypointBearing,bearing,255,255,reference(ref));publish(msg,DataMetric::WaypointVmg,vmg);
            targets[msg.Source]=destination;waypoint_name(msg);
        }break;
    }
    case 130306:{unsigned char sid;double speed,angle;tN2kWindReference ref;
        if(ParseN2kPGN130306(msg,sid,speed,angle,ref)){
            if(ref==N2kWind_Apparent){publish(msg,DataMetric::ApparentWindSpeed,speed,255,ref);publish(msg,DataMetric::ApparentWindAngle,angle,255,ref);}
            else if(ref==N2kWind_True_boat||ref==N2kWind_True_water||ref==N2kWind_True_North||ref==N2kWind_Magnetic){publish(msg,DataMetric::TrueWindSpeed,speed,255,ref);publish(msg,DataMetric::TrueWindAngle,angle,255,ref);}
        }break;
    }
    case 130310:{unsigned char sid;double water,air,pressure;
        if(ParseN2kPGN130310(msg,sid,water,air,pressure)){temperature(msg,N2kts_SeaTemperature,255,water);temperature(msg,N2kts_OutsideTemperature,255,air);publish(msg,DataMetric::AtmosphericPressure,pressure);}break;}
    case 130311:{unsigned char sid;tN2kTempSource source;tN2kHumiditySource hsource;double temp,humidity,pressure;
        if(ParseN2kPGN130311(msg,sid,source,temp,hsource,humidity,pressure)){temperature(msg,source,255,temp);publish(msg,DataMetric::Humidity,humidity,255,hsource);publish(msg,DataMetric::AtmosphericPressure,pressure);}break;}
    case 130312:case 130316:{unsigned char sid,instance;tN2kTempSource source;double actual,set;
        const bool parsed=msg.PGN==130312?ParseN2kPGN130312(msg,sid,instance,source,actual,set):ParseN2kPGN130316(msg,sid,instance,source,actual,set);
        if(parsed)temperature(msg,source,instance,actual);
        break;}
    case 130313:{unsigned char sid,instance;tN2kHumiditySource source;double actual,set;
        if(ParseN2kPGN130313(msg,sid,instance,source,actual,set))publish(msg,DataMetric::Humidity,actual,instance,source);
        break;}
    case 130314:{unsigned char sid,instance;tN2kPressureSource source;double pressure;
        if(ParseN2kPGN130314(msg,sid,instance,source,pressure)&&source==N2kps_Atmospheric)publish(msg,DataMetric::AtmosphericPressure,pressure,instance,source);
        break;}
    default:break;
    }
}
