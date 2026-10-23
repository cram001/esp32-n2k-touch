#include "graph_history.hpp"
#include "metric_groups.hpp"
#include <cassert>
#include <cstdio>
static_assert(unsigned(DataMetric::BatteryVoltage)==14 && unsigned(DataMetric::EngineRpm)==20 && unsigned(DataMetric::Position)==52 && unsigned(DataMetric::SolarPower)==55,"Existing persisted IDs moved");
int main(){
 GraphHistory g;
 assert(g.sample(0,1,0,true,0));assert(!g.sample(500000,1,0,true,9));
 assert(g.sample(1000000,1,0,true,-10));g.prepare_plot();
 assert(g.count==2 && g.capacity==60 && g.has_range && g.minimum<-10 && g.maximum>0);
 assert(g.plot[57]==GraphHistory::Missing && g.plot[58]>g.plot[59]);
 g.sample(4000000,1,0,true,20);g.prepare_plot();
 assert(g.count==5 && g.plot[57]==GraphHistory::Missing && g.plot[58]==GraphHistory::Missing);
 g.sample(5000000,1,0,false,1000);g.prepare_plot();assert(g.plot[59]==GraphHistory::Missing);
 for(int i=6;i<1000;++i)g.sample(int64_t(i)*1000000,1,0,true,42);
 g.prepare_plot();assert(g.count==60 && g.minimum<42 && g.maximum>42);
 g.sample(1000000000,2,0,true,0);assert(g.count==1); // Source/config switch cannot join histories.
 g.sample(1001000000,2,2,true,7);assert(g.capacity==300 && g.count==1);
 g.sample(9000000000,2,2,false,0);g.prepare_plot();assert(g.count==300 && !g.has_range);
 g.sample(0,2,2,true,-1);assert(g.count==1); // Clock reset.
 g.sample(6000000,2,2,true,std::numeric_limits<double>::infinity());g.prepare_plot();assert(g.plot[299]==GraphHistory::Missing);
 for(unsigned i=1;i<unsigned(DataMetric::Count);++i)assert(metric_group(DataMetric(i))!=MetricGroup::General);
 assert(!metric_graphable(DataMetric::Position) && !metric_graphable(DataMetric::UtcTime));
 assert(metric_group(DataMetric::EngineTripFuelUsed)==MetricGroup::Engine);
 assert(metric_group(DataMetric::DcCapacity)==MetricGroup::Electrical);
 puts("PASS: graph zero/negative values, windows, gaps, stale data, ring wrap, bounded catch-up, context/clock reset and field groups");
}
