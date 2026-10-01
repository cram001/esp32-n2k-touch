#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "N2kMessages.h"
#include "n2k_instruments.hpp"
#include "n2k_sources.hpp"
#include "actisense_ascii.hpp"
#include "nvs_fake.hpp"
int64_t fake_time_us=1000000;
uint32_t N2kMillis(){return static_cast<uint32_t>(fake_time_us/1000);}
uint64_t N2kMillis64(){return fake_time_us/1000;}
void close_to(DataMetric m,double expected,uint8_t field=255){InstrumentValue v;assert(n2k_sources_get(m,field,v)&&v.valid&&!v.stale);assert(std::abs(v.value-expected)<0.01);}
void send(tN2kMsg &message,uint8_t source=10){message.Source=source;n2k_instruments_receive(message);}
int main(){
    fake_nvs::clear();assert(n2k_sources_init());tN2kMsg message;
    // Exercise the full wireless path with real library-encoded PGN bytes.
    SetN2kPGN128267(message,0,10,-2);std::string wire="A123456.789 0AFF3 1F50B ";
    char byte[3];for(int i=0;i<message.DataLen;++i){std::snprintf(byte,sizeof(byte),"%02X",message.Data[i]);wire+=byte;}wire+="\r\n";
    ActisenseAsciiDecoder decoder;ActisenseMessage packet;unsigned decoded=0;
    for(char c:wire)if(decoder.feed(c,packet))++decoded;
    assert(decoded==1);message.SetPGN(packet.pgn);message.Source=packet.source;message.Destination=packet.destination;
    message.DataLen=packet.length;std::memcpy(message.Data,packet.data.data(),packet.length);n2k_instruments_receive(message);
    close_to(DataMetric::Depth,10);
    DepthConfig depth;depth.reference=DepthReference::SensorOffset;assert(n2k_sources_save_depth(depth));close_to(DataMetric::Depth,8);
    SetN2kPGN128267(message,1,12,-2);send(message);close_to(DataMetric::Depth,10);
    N2kSourceOption options[16];assert(n2k_sources_options(DataMetric::Depth,options,16)==1); // SID is not a source instance.
    SetN2kPGN128267(message,2,15,-2);send(message,11);assert(n2k_sources_options(DataMetric::Depth,options,16)==2);
    SetN2kPGN128267(message,3,1200,-2);send(message);InstrumentValue invalid;assert(n2k_sources_get(DataMetric::Depth,0,invalid)&&!invalid.valid);
    SetN2kPGN127488(message,0,1500,120000,0);send(message);close_to(DataMetric::EngineRpm,1500);
    SetN2kPGN127488(message,1,2200,130000,0);send(message);
    assert(n2k_sources_options(DataMetric::EngineRpm,options,16)==2);assert(n2k_sources_save_choice(0,options[1].choice));close_to(DataMetric::EngineRpm,2200,0);
    SetN2kPGN127489(message,0,200000,350,360,14.2,10,7200,100000,50000,50,40,tN2kEngineDiscreteStatus1{},tN2kEngineDiscreteStatus2{});send(message);
    close_to(DataMetric::EngineOilPressure,200000);close_to(DataMetric::EngineCoolantTemperature,360);
    close_to(DataMetric::EngineAlternatorVoltage,14.2);close_to(DataMetric::EngineHours,7200);
    SetN2kPGN127505(message,0,N2kft_Water,40,500);send(message);close_to(DataMetric::TankLevel,40);close_to(DataMetric::TankCapacity,500);
    SetN2kPGN127505(message,0,N2kft_BlackWater,80,150);send(message);
    assert(n2k_sources_options(DataMetric::TankLevel,options,16)==2);
    SetN2kPGN129025(message,48.12345,-123.12345);send(message);close_to(DataMetric::Latitude,48.12345);close_to(DataMetric::Longitude,-123.12345);
    SetN2kPGN129029(message,0,20000,40000,48.12345,-123.12345,100,N2kGNSSt_GPS,N2kGNSSm_GNSSfix,10,1,1,30);send(message);close_to(DataMetric::Altitude,100);
    SetN2kPGN129284(message,0,1852,N2khr_true,false,false,N2kdct_GreatCircle,40000,20000,1,1.2,1,42,48,-123,3);send(message);
    close_to(DataMetric::DistanceToWaypoint,1852);close_to(DataMetric::WaypointBearing,1.2);close_to(DataMetric::WaypointVmg,3);
    SetN2kPGN129285(message,0,1,1,N2kdir_forward,"Route");assert(AppendN2kPGN129285(message,42,"Home",48,-123));send(message);
    InstrumentValue name;assert(n2k_sources_get(DataMetric::WaypointName,255,name)&&std::strcmp(name.text.data(),"Home")==0);
    message.DataLen=11;send(message);assert(n2k_sources_get(DataMetric::WaypointName,255,name)&&std::strcmp(name.text.data(),"Home")==0); // Broken list does not replace valid metadata.
    SetN2kPGN130312(message,0,0,N2kts_MainCabinTemperature,295);send(message);close_to(DataMetric::CabinTemperature,295);
    SetN2kPGN130312(message,0,1,N2kts_EngineRoomTemperature,310);send(message);close_to(DataMetric::EngineRoomTemperature,310);
    SetN2kPGN130316(message,0,1,N2kts_ExhaustGasTemperature,500);send(message);close_to(DataMetric::EngineExhaustTemperature,500);
    // A truncated engine message cannot update even its early fields.
    SetN2kPGN127489(message,0,300000,350,360,14.2,10,7200,100000,50000,50,40,tN2kEngineDiscreteStatus1{},tN2kEngineDiscreteStatus2{});
    message.DataLen=4;send(message);close_to(DataMetric::EngineOilPressure,200000);
    SetN2kPGN129284(message,0,1852,N2khr_true,false,false,N2kdct_GreatCircle,40000,20000,1,1.2,1,43,48,-123,3);send(message);
    assert(n2k_sources_get(DataMetric::WaypointName,255,name)&&!name.text[0]); // New target cannot retain Home.
    message.SetPGN(60928);message.DataLen=8;std::memset(message.Data,0,8);message.Data[0]=1;send(message);
    SetN2kPGN129284(message,0,1852,N2khr_true,false,false,N2kdct_GreatCircle,40000,20000,1,1.2,1,42,48,-123,3);send(message);
    assert(n2k_sources_get(DataMetric::WaypointName,255,name)&&!name.text[0]); // Identity discovery discards unidentified metadata.
    SetN2kPGN129285(message,0,1,1,N2kdir_forward,"Route");assert(AppendN2kPGN129285(message,42,"New",48,-123));send(message);
    assert(n2k_sources_get(DataMetric::WaypointName,255,name)&&!std::strcmp(name.text.data(),"New"));
    message.SetPGN(60928);message.DataLen=8;std::memset(message.Data,0,8);message.Data[0]=2;send(message);
    SetN2kPGN129284(message,0,1852,N2khr_true,false,false,N2kdct_GreatCircle,40000,20000,1,1.2,1,42,48,-123,3);send(message);
    N2kSourceOption names[16];const size_t name_count=n2k_sources_options(DataMetric::WaypointName,names,16);
    assert(name_count==2 && names[1].choice.name==2);
    assert(n2k_sources_save_choice(5,names[1].choice));
    assert(n2k_sources_get(DataMetric::WaypointName,5,name)&&!name.text[0]); // Reused address does not inherit the previous device's name.
    SetN2kPGN129285(message,0,1,1,N2kdir_forward,"Route");assert(AppendN2kPGN129285(message,42,"Third",48,-123));send(message);
    assert(n2k_sources_get(DataMetric::WaypointName,5,name)&&!name.stale&&!std::strcmp(name.text.data(),"Third"));
    fake_time_us+=30000000;send(message);
    assert(n2k_sources_get(DataMetric::WaypointName,5,name)&&name.stale); // Periodic route lists cannot refresh an expired navigation target.
    std::puts("PASS: actual N2K library PGNs through wireless framing, depth/SID, two engines, tank types, GPS, navigation/name and environmental sensors");
}
