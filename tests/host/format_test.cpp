#include "instrument_data.hpp"
#include "smartshunt_ble.hpp"
#include "depth_display.hpp"
#include "n2k_sources.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#undef assert
#define assert(condition) do {if(!(condition)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#condition);std::exit(1);}} while(false)
int64_t fake_time_us=1000000;
SmartShuntData battery{};
SmartShuntData smartshunt_ble_get_data(size_t){return battery;}
void expect(DataMetric metric,double value,const UnitsSettings &units,const char *number,const char *unit){
    DataFieldSelection field{};field.metric=metric;
    InstrumentValue reading{};reading.valid=true;reading.stale=false;reading.value=value;
    char text[40],suffix[16];
    instrument_format_value(field,units,reading,text,sizeof(text),suffix,sizeof(suffix));
    if(std::strcmp(text,number) || std::strcmp(suffix,unit))std::fprintf(stderr,"metric %u: got [%s] [%s], expected [%s] [%s]\n",static_cast<unsigned>(metric),text,suffix,number,unit);
    assert(std::strcmp(text,number)==0 && std::strcmp(suffix,unit)==0);
}
int main(){
    assert(std::strcmp(instrument_metric_name(DataMetric::BoatSpeed),"STW (through water)")==0);
    assert(std::strcmp(instrument_metric_name(DataMetric::SpeedOverGround),"SOG")==0);
    UnitsSettings distance{};
    expect(DataMetric::RateOfTurn,0.01,distance,"+34.4","deg/min");
    expect(DataMetric::RudderAngle,-0.2,distance,"-11.5","deg");
    expect(DataMetric::TransmissionOilPressure,250000,distance,"2.50","bar");
    expect(DataMetric::TransmissionOilTemperature,300,distance,"26.9","C");
    expect(DataMetric::DcCapacity,360000,distance,"100.0","Ah");
    expect(DataMetric::EngineTripFuelUsed,15,distance,"15.0","L");
    expect(DataMetric::EngineAverageFuelRate,3.5,distance,"3.5","L/h");
    expect(DataMetric::TransmissionGear,2,distance,"REV","");
    expect(DataMetric::UtcTime,20000.0*86400+45296,distance,"12:34:56","UTC");

    DepthDisplay filter;DataFieldSelection depth{};depth.metric=DataMetric::Depth;
    n2k_sources_publish(DataMetric::Depth,10,255,255,10.1);
    auto initial=instrument_data_get(depth,0);assert(initial.sample_us==fake_time_us);
    assert(filter.apply(depth,DepthUnit::Metres,initial).value==10.1);
    fake_time_us+=100000;n2k_sources_publish(DataMetric::Depth,10,255,255,10.2);
    for(int i=0;i<5;++i)assert(filter.apply(depth,DepthUnit::Metres,instrument_data_get(depth,0)).value==10.1);
    fake_time_us+=100000;n2k_sources_publish(DataMetric::Depth,10,255,255,10.2);
    assert(filter.apply(depth,DepthUnit::Metres,instrument_data_get(depth,0)).value==10.2);
    const auto context=instrument_data_get(depth,0).display_context;
    assert(n2k_sources_save_depth(DepthConfig{}));
    assert(instrument_data_get(depth,0).display_context!=context);
    fake_time_us+=100000;n2k_sources_publish(DataMetric::Depth,10,255,255,1000.01);
    assert(!filter.apply(depth,DepthUnit::Metres,instrument_data_get(depth,0)).valid);
    instrument_data_reset_nmea();
    expect(DataMetric::Depth,10.15,distance,"10.2","m");
    expect(DataMetric::DistanceToWaypoint,1852,distance,"1.0","NM");
    expect(DataMetric::TripDistance,2315,distance,"1.2","NM");
    distance.distance=DistanceUnit::Kilometres;
    expect(DataMetric::TripDistance,1852,distance,"1.9","km");
    expect(DataMetric::DistanceToWaypoint,100.4,distance,"100","m");
    distance.short_distance=ShortDistanceUnit::Feet;
    expect(DataMetric::DistanceToWaypoint,100.4,distance,"329","ft");
    UnitsSettings units{};units.depth=DepthUnit::Feet;
    expect(DataMetric::Depth,1000,units,"3280.8","ft");
    expect(DataMetric::Depth,1000.01,units,"--","");
    expect(DataMetric::DepthBelowKeel,-1,units,"-3.3","ft");
    expect(DataMetric::Latitude,48.12345,units,"48 07.407 N","DM");
    expect(DataMetric::Longitude,-123.12345,units,"123 07.407 W","DM");
    expect(DataMetric::Latitude,89.9999997,units,"90 00.000 N","DM");
    expect(DataMetric::Latitude,-89.9999997,units,"90 00.000 S","DM");
    expect(DataMetric::Longitude,179.9999997,units,"180 00.000 E","DM");
    expect(DataMetric::Longitude,-179.9999997,units,"180 00.000 W","DM");
    units.lat_lon_format=LatLonFormat::DecimalDegrees;
    expect(DataMetric::Latitude,48.12345,units,"48.12345 N","DD");
    expect(DataMetric::Longitude,-123.12345,units,"123.12345 W","DD");
    units.lat_lon_format=LatLonFormat::DegreesMinutesSeconds;
    expect(DataMetric::Latitude,48.12345,units,"48 07 24.4 N","DMS");
    expect(DataMetric::Longitude,-123.12345,units,"123 07 24.4 W","DMS");
    expect(DataMetric::Latitude,89.9999997,units,"90 00 00.0 N","DMS");
    expect(DataMetric::Longitude,-179.9999997,units,"180 00 00.0 W","DMS");
    expect(DataMetric::Latitude,48.1333333,units,"48 08 00.0 N","DMS");
    units.lat_lon_format=LatLonFormat::DegreesMinutes;
    expect(DataMetric::EngineOilPressure,200000,units,"2.00","bar");
    expect(DataMetric::AtmosphericPressure,101325,units,"1013.25","hPa");
    expect(DataMetric::EngineHours,7200,units,"2.0","h");
    expect(DataMetric::EngineFuelRate,10,units,"10.0","L/h");
    expect(DataMetric::TankCapacity,500,units,"500.0","L");
    units.temperature=TemperatureUnit::Fahrenheit;
    expect(DataMetric::CabinTemperature,293.15,units,"68.0","F");
    DataFieldSelection field{};field.metric=DataMetric::Depth;
    n2k_sources_publish(field.metric,10,255,255,10);fake_time_us+=2999000;
    assert(!instrument_data_get(field).stale);fake_time_us+=1000;
    assert(instrument_data_get(field).stale);
    char stale_text[20],stale_units[8];
    instrument_format_value(field,units,instrument_data_get(field),stale_text,sizeof(stale_text),stale_units,sizeof(stale_units));
    assert(std::strcmp(stale_text,"STALE")==0);
    fake_time_us+=27000000;
    instrument_format_value(field,units,instrument_data_get(field),stale_text,sizeof(stale_text),stale_units,sizeof(stale_units));
    assert(std::strcmp(stale_text,"--")==0);
    field.source=DataSourceType::SmartShunt;field.metric=DataMetric::BatteryVoltage;
    battery.valid=true;battery.voltage_valid=true;battery.voltage_v=12.5;battery.stale=true;
    battery.age_ms=29999;assert(instrument_data_get(field).valid && !instrument_data_get(field).stale);
    battery.age_ms=30000;assert(instrument_data_get(field).stale);
    char text[80],suffix[16];instrument_format_value(field,units,instrument_data_get(field),text,sizeof(text),suffix,sizeof(suffix));
    assert(std::strcmp(text,"--")==0);
    DataFieldSelection position{};position.metric=DataMetric::Position;
    InstrumentValue pos{};pos.valid=true;pos.stale=false;pos.value=48.12345;pos.secondary_valid=true;pos.secondary_value=-123.12345;
    instrument_format_value(position,units,pos,text,sizeof(text),suffix,sizeof(suffix));
    assert(std::strcmp(text,"N48 07.41\nW123 07.41")==0 && std::strcmp(suffix,"DM")==0);

    pos.value=51+14.34/60;pos.secondary_value=-(124+3.54/60);
    instrument_format_value(position,units,pos,text,sizeof(text),suffix,sizeof(suffix));
    assert(!std::strcmp(text,"N51 14.34\nW124 03.54"));
    pos.value=-89.9999997;pos.secondary_value=179.9999997;
    instrument_format_value(position,units,pos,text,sizeof(text),suffix,sizeof(suffix));
    assert(!std::strcmp(text,"S90 00.00\nE180 00.00"));
    pos.value=0;pos.secondary_value=0;
    instrument_format_value(position,units,pos,text,sizeof(text),suffix,sizeof(suffix));
    assert(!std::strcmp(text,"N0 00.00\nE0 00.00"));
    units.lat_lon_format=LatLonFormat::DecimalDegrees;
    instrument_format_value(position,units,pos,text,sizeof(text),suffix,sizeof(suffix));
    assert(!std::strcmp(text,"N0.00000\nE0.00000") && !std::strcmp(suffix,"DD"));
    units.lat_lon_format=LatLonFormat::DegreesMinutesSeconds;
    instrument_format_value(position,units,pos,text,sizeof(text),suffix,sizeof(suffix));
    assert(!std::strcmp(text,"N0 00 00.0\nE0 00 00.0") && !std::strcmp(suffix,"DMS"));
    pos.secondary_valid=false;
    instrument_format_value(position,units,pos,text,sizeof(text),suffix,sizeof(suffix));
    assert(!std::strcmp(text,"--"));
    pos.secondary_valid=true;pos.value=91;
    instrument_format_value(position,units,pos,text,sizeof(text),suffix,sizeof(suffix));
    assert(!std::strcmp(text,"--"));
    units.lat_lon_format=LatLonFormat::DegreesMinutes;

    DataFieldSelection dir{};dir.metric=DataMetric::CourseOverGround;
    InstrumentValue bearing{};bearing.valid=true;bearing.stale=false;bearing.value=1.0;bearing.heading_reference=HeadingReference::True;
    instrument_format_value(dir,units,bearing,text,sizeof(text),suffix,sizeof(suffix));
    assert(std::strcmp(suffix,"°T")==0);
    bearing.heading_reference=HeadingReference::Magnetic;
    instrument_format_value(dir,units,bearing,text,sizeof(text),suffix,sizeof(suffix));
    assert(std::strcmp(suffix,"°M")==0);
    dir.metric=DataMetric::WaypointBearing;
    bearing.heading_reference=HeadingReference::True;
    instrument_format_value(dir,units,bearing,text,sizeof(text),suffix,sizeof(suffix));
    assert(std::strcmp(suffix,"°T")==0);

    expect(DataMetric::SolarVoltage,14.2,units,"14.20","V");
    expect(DataMetric::SolarCurrent,12.3,units,"+12.3","A");
    expect(DataMetric::SolarPower,187,units,"187","W");
    field.source=DataSourceType::SmartShunt;field.metric=DataMetric::BatteryVoltage;
    battery.record_type=1;battery.age_ms=0;
    assert(!std::strcmp(instrument_field_name(field),"Solar V"));
    field.metric=DataMetric::BatteryCurrent;
    assert(!std::strcmp(instrument_field_name(field),"Solar A"));
    field.metric=DataMetric::SolarPower;battery.power_valid=true;battery.power_w=187;
    assert(instrument_data_get(field).valid && instrument_data_get(field).value==187);
    battery.record_type=2;assert(!instrument_data_get(field).valid);
    field.metric=DataMetric::BatteryVoltage;assert(!std::strcmp(instrument_field_name(field),"Battery V"));
    assert(!instrument_metric_supported(DataSourceType::Nmea2000,DataMetric::SolarPower));
    assert(instrument_metric_supported(DataSourceType::SmartShunt,DataMetric::SolarPower));
    std::puts("PASS: 30s NMEA/BLE display timeout, depth limit, position formats, directional references, signed keel clearance and instrument units");
}
