#include <cassert>
#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "n2k_sources.hpp"
#include "smartshunt_ble.hpp"
int64_t fake_time_us=1000000;
SmartShuntData smartshunt_ble_get_data(size_t){return {};}
int main(){
 assert(n2k_sources_init());
 n2k_sources_name(10,123);n2k_sources_model(10,"test");
 n2k_sources_publish(DataMetric::Depth,10,255,255,5);
 N2kSourceOption options[4];assert(n2k_sources_options(DataMetric::Depth,options,4)==1);
 InstrumentValue value;assert(n2k_sources_get(DataMetric::Depth,0,value)&&value.valid);
 char label[100];n2k_sources_label(DataMetric::Depth,0,label,sizeof(label));
 instrument_data_reset_nmea();
 assert(fake_critical_entries==0 && fake_mutex_takes>0);
 std::puts("PASS: source formatting, copies and resets use task mutexes without interrupt-off sections");
}
