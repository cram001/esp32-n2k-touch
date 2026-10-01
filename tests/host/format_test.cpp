#include "instrument_data.hpp"
#include "smartshunt_ble.hpp"
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
    UnitsSettings units{};units.depth=DepthUnit::Feet;
    expect(DataMetric::Depth,1000,units,"3280.8","ft");
    expect(DataMetric::Depth,1000.01,units,"--","");
    expect(DataMetric::DepthBelowKeel,-1,units,"-3.3","ft");
    expect(DataMetric::Latitude,48.12345,units,"48 07.407 N","DM");
    expect(DataMetric::Longitude,-123.12345,units,"123 07.407 W","DM");
    units.lat_lon_format=LatLonFormat::DecimalDegrees;
    expect(DataMetric::Latitude,48.12345,units,"48.12345 N","DD");
    expect(DataMetric::Longitude,-123.12345,units,"123.12345 W","DD");
    units.lat_lon_format=LatLonFormat::DegreesMinutesSeconds;
    expect(DataMetric::Latitude,48.12345,units,"48 07 24.4 N","DMS");
    units.lat_lon_format=LatLonFormat::DegreesMinutes;
    expect(DataMetric::EngineOilPressure,200000,units,"2.00","bar");
    expect(DataMetric::AtmosphericPressure,101325,units,"1013.25","hPa");
    expect(DataMetric::EngineHours,7200,units,"2.0","h");
    expect(DataMetric::EngineFuelRate,10,units,"10.0","L/h");
    expect(DataMetric::TankCapacity,500,units,"500.0","L");
    units.temperature=TemperatureUnit::Fahrenheit;
    expect(DataMetric::CabinTemperature,293.15,units,"68.0","F");
    DataFieldSelection field{};field.metric=DataMetric::Depth;
    instrument_data_update_nmea(field.metric,10);fake_time_us+=29999000;
    assert(!instrument_data_get(field).stale);fake_time_us+=1000;
    assert(instrument_data_get(field).stale);
    field.source=DataSourceType::SmartShunt;field.metric=DataMetric::BatteryVoltage;
    battery.valid=true;battery.voltage_valid=true;battery.voltage_v=12.5;battery.stale=true;
    battery.age_ms=29999;assert(instrument_data_get(field).valid && !instrument_data_get(field).stale);
    battery.age_ms=30000;assert(instrument_data_get(field).stale);
    char text[80],suffix[16];instrument_format_value(field,units,instrument_data_get(field),text,sizeof(text),suffix,sizeof(suffix));
    assert(std::strcmp(text,"--")==0);
    DataFieldSelection position{};position.metric=DataMetric::Position;
    InstrumentValue pos{};pos.valid=true;pos.stale=false;pos.value=48.12345;pos.secondary_valid=true;pos.secondary_value=-123.12345;
    instrument_format_value(position,units,pos,text,sizeof(text),suffix,sizeof(suffix));
    assert(std::strcmp(text,"48 07.407 N\n123 07.407 W")==0 && std::strcmp(suffix,"DM")==0);

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

    std::puts("PASS: 30s NMEA/BLE display timeout, depth limit, position formats, directional references, signed keel clearance and instrument units");
}
