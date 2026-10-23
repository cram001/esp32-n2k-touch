#include "instrument_data.hpp"
#include "smartshunt_ble.hpp"
#include "n2k_instruments.hpp"
#include "n2k_sources.hpp"
#include "N2kMessages.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
int64_t fake_time_us=1000000;
uint32_t N2kMillis(){return static_cast<uint32_t>(fake_time_us/1000);}
uint64_t N2kMillis64(){return static_cast<uint64_t>(fake_time_us/1000);}
SmartShuntData smartshunt_ble_get_data(size_t) { return {}; }
constexpr double DEG = 3.14159265358979323846 / 180.0;
void heading(double radians,HeadingReference ref){
 tN2kMsg m;SetN2kPGN127250(m,0,std::isfinite(radians)?radians:N2kDoubleNA,N2kDoubleNA,N2kDoubleNA,
   ref==HeadingReference::True?N2khr_true:ref==HeadingReference::Magnetic?N2khr_magnetic:static_cast<tN2kHeadingReference>(3));
 m.Source=10;n2k_instruments_receive(m);
}
void variation(double radians){tN2kMsg m;SetN2kPGN127258(m,0,N2kmagvar_Manual,0,radians);m.Source=10;n2k_instruments_receive(m);}
void expect(HeadingReference target, const char *number, const char *unit) {
    DataFieldSelection field{}; field.metric=DataMetric::Heading;
    UnitsSettings settings{}; settings.heading_reference=target;
    char value[32], units[16];
    instrument_format_value(field,settings,instrument_data_get(field),value,sizeof(value),units,sizeof(units));
    assert(std::strcmp(value,number)==0);
    assert(std::strcmp(units,unit)==0);
}
int main() {
    heading(100*DEG,HeadingReference::Magnetic);
    expect(HeadingReference::Magnetic,"100","°M");
    expect(HeadingReference::True,"--","°T");
    variation(15*DEG); // East variation.
    expect(HeadingReference::True,"115","°T");
    heading(115*DEG,HeadingReference::True);
    expect(HeadingReference::Magnetic,"100","°M");
    variation(-20*DEG); // West variation.
    expect(HeadingReference::Magnetic,"135","°M");
    heading(350*DEG,HeadingReference::Magnetic);
    variation(15*DEG);
    expect(HeadingReference::True,"5","°T");
    heading(5*DEG,HeadingReference::True);
    expect(HeadingReference::Magnetic,"350","°M");
    heading(359.8*DEG,HeadingReference::True);
    expect(HeadingReference::True,"0","°T");
    heading(359*DEG,HeadingReference::True);
    expect(HeadingReference::True,"359","°T");
    heading(std::numeric_limits<double>::quiet_NaN(),HeadingReference::True);
    expect(HeadingReference::True,"359","°T");
    fake_time_us+=61000000;
    heading(20*DEG,HeadingReference::True);
    expect(HeadingReference::Magnetic,"--","°M"); // Variation expired.
    expect(HeadingReference::True,"20","°T");
    variation(3.2); // Invalid variation cannot revive cache.
    expect(HeadingReference::Magnetic,"--","°M");
    fake_time_us+=30000000;
    expect(HeadingReference::True,"--",""); // Heading expired.
    instrument_data_reset_nmea();heading(42*DEG,HeadingReference::Unknown);
    expect(HeadingReference::True,"--",""); // Unknown wire reference is ignored.
    DataFieldSelection cog{}; cog.metric=DataMetric::CourseOverGround;
    tN2kMsg course;SetN2kPGN129026(course,0,N2khr_true,42*DEG,0);course.Source=10;n2k_instruments_receive(course);
    UnitsSettings units{};units.heading_reference=HeadingReference::Magnetic;
    char value[32], label[16];
    instrument_format_value(cog,units,instrument_data_get(cog),value,sizeof(value),label,sizeof(label));
    assert(std::strcmp(value,"42")==0 && std::strcmp(label,"°T")==0);
    heading(20*DEG,HeadingReference::True);
    variation(2*DEG);
    instrument_data_reset_nmea();
    assert(!instrument_data_get(cog).valid);
    expect(HeadingReference::True,"--","");
    std::puts("PASS: heading reference, signed conversion, wrap/rounding, missing/stale variation, heading expiry, source reset and unchanged COG");
}
