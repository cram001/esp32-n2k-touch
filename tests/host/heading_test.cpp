#include "instrument_data.hpp"
#include "smartshunt_ble.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
int64_t fake_time_us=1000000;
SmartShuntData smartshunt_ble_get_data(size_t) { return {}; }
constexpr double DEG = 3.14159265358979323846 / 180.0;
void expect(HeadingReference target, const char *number, const char *unit) {
    DataFieldSelection field{}; field.metric=DataMetric::Heading;
    UnitsSettings settings{}; settings.heading_reference=target;
    char value[32], units[16];
    instrument_format_value(field,settings,instrument_data_get(field),value,sizeof(value),units,sizeof(units));
    assert(std::strcmp(value,number)==0);
    assert(std::strcmp(units,unit)==0);
}
int main() {
    instrument_data_update_heading(100*DEG,HeadingReference::Magnetic);
    expect(HeadingReference::Magnetic,"100","deg M");
    expect(HeadingReference::True,"--","deg T");
    instrument_data_update_variation(15*DEG); // East variation.
    expect(HeadingReference::True,"115","deg T");
    instrument_data_update_heading(115*DEG,HeadingReference::True);
    expect(HeadingReference::Magnetic,"100","deg M");
    instrument_data_update_variation(-20*DEG); // West variation.
    expect(HeadingReference::Magnetic,"135","deg M");
    instrument_data_update_heading(350*DEG,HeadingReference::Magnetic);
    instrument_data_update_variation(15*DEG);
    expect(HeadingReference::True,"5","deg T");
    instrument_data_update_heading(5*DEG,HeadingReference::True);
    expect(HeadingReference::Magnetic,"350","deg M");
    instrument_data_update_heading(359.8*DEG,HeadingReference::True);
    expect(HeadingReference::True,"0","deg T");
    instrument_data_update_heading(-1*DEG,HeadingReference::True);
    expect(HeadingReference::True,"359","deg T");
    instrument_data_update_heading(std::numeric_limits<double>::quiet_NaN(),HeadingReference::True);
    expect(HeadingReference::True,"359","deg T");
    fake_time_us+=61000000;
    instrument_data_update_heading(20*DEG,HeadingReference::True);
    expect(HeadingReference::Magnetic,"--","deg M"); // Variation expired.
    expect(HeadingReference::True,"20","deg T");
    instrument_data_update_variation(4); // Invalid variation cannot revive cache.
    expect(HeadingReference::Magnetic,"--","deg M");
    fake_time_us+=30000000;
    expect(HeadingReference::True,"--",""); // Heading expired.
    instrument_data_update_nmea(DataMetric::Heading,42*DEG);
    expect(HeadingReference::True,"--","deg T"); // Unknown reference.
    DataFieldSelection cog{}; cog.metric=DataMetric::CourseOverGround;
    instrument_data_update_nmea(cog.metric,42*DEG);
    UnitsSettings units{};units.heading_reference=HeadingReference::Magnetic;
    char value[32], label[16];
    instrument_format_value(cog,units,instrument_data_get(cog),value,sizeof(value),label,sizeof(label));
    assert(std::strcmp(value,"42")==0 && std::strcmp(label,"deg")==0);
    instrument_data_update_heading(20*DEG,HeadingReference::True);
    instrument_data_update_variation(2*DEG);
    instrument_data_reset_nmea();
    assert(!instrument_data_get(cog).valid);
    expect(HeadingReference::True,"--","");
    std::puts("PASS: heading reference, signed conversion, wrap/rounding, missing/stale variation, heading expiry, source reset and unchanged COG");
}
