#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "N2kMessages.h"
#include "n2k_instruments.hpp"
#include "n2k_sources.hpp"
#include "nvs_fake.hpp"
#include "smartshunt_ble.hpp"
SmartShuntData smartshunt_ble_get_data(size_t){return {};}
int64_t fake_time_us=1000000;
uint32_t N2kMillis(){return static_cast<uint32_t>(fake_time_us/1000);}
uint64_t N2kMillis64(){return fake_time_us/1000;}
void send(tN2kMsg &msg,uint8_t source=10){msg.Source=source;n2k_instruments_receive(msg);}
void raw_sog(tN2kMsg &msg,uint16_t raw){msg.Data[4]=raw&255;msg.Data[5]=raw>>8;}
InstrumentValue get(DataMetric metric){DataFieldSelection field{};field.metric=metric;return instrument_data_get(field,0);}
void display(const InstrumentValue &v,const char *expected){
    DataFieldSelection field{};field.metric=DataMetric::SpeedOverGround;
    UnitsSettings units{};units.vessel_speed=SpeedUnit::Knots;
    char number[32],unit[16];instrument_format_value(field,units,v,number,sizeof(number),unit,sizeof(unit));
    assert(!std::strcmp(number,expected));
}
int main(){
    fake_nvs::clear();assert(n2k_sources_init());tN2kMsg msg;
    // Exact field symptom, reproduced with the pinned library, before filtering.
    SetN2kPGN129026(msg,0,N2khr_true,1,0);raw_sog(msg,0xfffe);
    unsigned char sid;tN2kHeadingReference ref;double cog,sog;
    assert(ParseN2kPGN129026(msg,sid,ref,cog,sog));
    assert(std::fabs(sog-655.34)<0.00001);
    assert(std::round(sog*3600/1852*10)/10==1273.9);
    InstrumentValue unfiltered{};unfiltered.valid=true;unfiltered.stale=false;unfiltered.value=sog;display(unfiltered,"1273.9");
    for(uint16_t reserved:{0xfffd,0xfffe,0xffff}){
        n2k_sources_reset();SetN2kPGN129026(msg,0,N2khr_true,1,0);raw_sog(msg,reserved);send(msg);
        assert(!get(DataMetric::SpeedOverGround).valid);display(get(DataMetric::SpeedOverGround),"--");
        assert(get(DataMetric::CourseOverGround).valid);
    }
    n2k_sources_reset();SetN2kPGN129026(msg,0,N2khr_true,1,0);send(msg);
    assert(get(DataMetric::SpeedOverGround).valid && get(DataMetric::SpeedOverGround).value==0);display(get(DataMetric::SpeedOverGround),"0.0");
    SetN2kPGN129026(msg,0,N2khr_true,1,2.57);send(msg);
    const auto previous=get(DataMetric::SpeedOverGround);assert(previous.valid && std::fabs(previous.value-2.57)<0.001);
    for(uint16_t reserved:{0xfffd,0xfffe,0xffff}){
        const auto stamp=get(DataMetric::SpeedOverGround).sample_us;
        fake_time_us+=100000;raw_sog(msg,reserved);send(msg);
        auto v=get(DataMetric::SpeedOverGround);assert(!v.valid && v.sample_us==stamp);
        SetN2kPGN129026(msg,0,N2khr_true,1,2.57);fake_time_us+=100000;send(msg);
    }
    // Missing speed does not fall back to another GPS or invalidate its data.
    SetN2kPGN129026(msg,0,N2khr_true,2,4);send(msg,11);
    N2kSourceOption options[4];assert(n2k_sources_options(DataMetric::SpeedOverGround,options,4)==2);
    raw_sog(msg,0xfffe);send(msg,10);assert(!get(DataMetric::SpeedOverGround).valid);
    assert(n2k_sources_save_choice(N2K_GPS_CHOICE,options[1].choice));
    assert(get(DataMetric::SpeedOverGround).valid && get(DataMetric::SpeedOverGround).value==4);
    raw_sog(msg,0xffff);send(msg,10);assert(get(DataMetric::SpeedOverGround).valid);
    raw_sog(msg,0xfffe);send(msg,11);assert(!get(DataMetric::SpeedOverGround).valid);
    // Valid range is protocol-defined, not an arbitrary boat-speed cap.
    raw_sog(msg,0xfffc);send(msg,11);assert(get(DataMetric::SpeedOverGround).valid);
    SetN2kPGN129026(msg,0,N2khr_true,1,0);send(msg,11);
    assert(get(DataMetric::SpeedOverGround).valid && get(DataMetric::SpeedOverGround).value==0);display(get(DataMetric::SpeedOverGround),"0.0");
    msg.Data[2]=0xfe;msg.Data[3]=0xff;send(msg,11);
    assert(!get(DataMetric::CourseOverGround).valid && get(DataMetric::SpeedOverGround).valid);
    SetN2kPGN129026(msg,0,N2khr_true,1,0);send(msg,11);
    msg.Data[1]=0xff;send(msg,11);assert(!get(DataMetric::CourseOverGround).valid);
    SetN2kPGN129026(msg,0,N2khr_true,1,2.57);send(msg,11);
    raw_sog(msg,0xfffe);msg.DataLen=5;send(msg,11);assert(get(DataMetric::SpeedOverGround).valid);
    std::puts("PASS: exact 1273.9kn reproduction, reserved SOG/COG rejection, stationary zero, immediate invalidation, recovery, source isolation and truncation");
}
