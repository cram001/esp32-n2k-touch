#include "N2kMessages.h"
#include "n2k_instruments.hpp"
#include "n2k_sources.hpp"
#include "nvs_fake.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
int64_t fake_time_us=1000000;
uint32_t N2kMillis(){return fake_time_us/1000;}
uint64_t N2kMillis64(){return fake_time_us/1000;}
static void send(tN2kMsg &m,uint8_t source=12){m.Source=source;n2k_instruments_receive(m);}
static void expect(DataMetric metric,double expected){InstrumentValue v;assert(n2k_sources_get(metric,255,v)&&v.valid&&!v.stale);if(std::abs(v.value-expected)>0.02)std::fprintf(stderr,"metric %u got %f expected %f\n",unsigned(metric),v.value,expected);assert(std::abs(v.value-expected)<0.02);}
int main(){
 fake_nvs::clear();assert(n2k_sources_init());tN2kMsg m;
 SetN2kPGN127245(m,-0.2,1);send(m);expect(DataMetric::RudderAngle,-0.2);
 SetN2kPGN127251(m,1,-0.01);send(m);expect(DataMetric::RateOfTurn,-0.01);
 SetN2kPGN127257(m,1,0.4,-0.1,0.2);send(m);expect(DataMetric::Yaw,0.4);expect(DataMetric::Pitch,-0.1);expect(DataMetric::Roll,0.2);
 SetN2kPGN128000(m,1,-0.05);send(m);expect(DataMetric::Leeway,-0.05);
 SetN2kPGN127488(m,0,1400,100000,25);send(m);expect(DataMetric::EngineTrim,25);
 SetN2kPGN127493(m,1,N2kTG_Reverse,250000,320);send(m);expect(DataMetric::TransmissionGear,2);expect(DataMetric::TransmissionOilPressure,250000);expect(DataMetric::TransmissionOilTemperature,320);
 SetN2kPGN127497(m,1,15,3.5,2.8,4);send(m);expect(DataMetric::EngineTripFuelUsed,15);expect(DataMetric::EngineAverageFuelRate,3.5);expect(DataMetric::EngineEconomyFuelRate,2.8);expect(DataMetric::EngineInstantFuelRate,4);
 SetN2kPGN127508(m,2,13.4,-8.5,296,1);send(m);expect(DataMetric::BatteryVoltage,13.4);expect(DataMetric::BatteryCurrent,-8.5);expect(DataMetric::BatteryTemperature,296);
 SetN2kPGN127506(m,1,2,N2kDCt_Battery,85,94,3600,0.12,360000);send(m);expect(DataMetric::BatterySoc,85);expect(DataMetric::BatteryTimeToGo,60);expect(DataMetric::DcStateOfHealth,94);expect(DataMetric::DcRippleVoltage,0.12);expect(DataMetric::DcCapacity,360000);
 SetN2kPGN127751(m,3,14.1,-2.5,1);m.DataLen=8;send(m);expect(DataMetric::DcVoltage,14.1);expect(DataMetric::DcCurrent,-2.5);
 SetN2kPGN130576(m,20,80);send(m);expect(DataMetric::PortTrimTab,20);expect(DataMetric::StarboardTrimTab,80);
 SetN2kPGN126992(m,1,20000,45296);send(m);expect(DataMetric::UtcTime,20000.0*86400+45296);
 N2kSourceOption options[8];SetN2kPGN127245(m,0.3,2);send(m,13);
 assert(n2k_sources_options(DataMetric::RudderAngle,options,8)==2);assert(n2k_sources_save_choice(0,options[1].choice));
 InstrumentValue selected;assert(n2k_sources_get(DataMetric::RudderAngle,0,selected)&&std::abs(selected.value-0.3)<0.001);
 SetN2kPGN127245(m,0.8,2);m.DataLen=3;send(m,13);assert(n2k_sources_get(DataMetric::RudderAngle,0,selected)&&std::abs(selected.value-0.3)<0.001);
 SetN2kPGN127493(m,1,N2kTG_Unknown,250000,320);send(m);assert(n2k_sources_get(DataMetric::TransmissionGear,255,selected)&&!selected.valid);
 fake_time_us+=30000000;assert(n2k_sources_get(DataMetric::DcCurrent,255,selected)&&selected.stale);
 // All-NA fields must not manufacture valid zero values on a new device.
 n2k_sources_reset();SetN2kPGN127508(m,2,N2kDoubleNA,N2kDoubleNA,N2kDoubleNA,1);send(m);
 (void)n2k_sources_get(DataMetric::BatteryVoltage,255,selected);assert(!selected.valid);
 puts("PASS: pinned-library additional PGNs, signed values/units, source isolation, truncation, NA, gear invalidation and stale readings");
}
