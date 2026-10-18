#include "third_party/nmea2000_esp32xx/NMEA2000_esp32xx.h"
#include <cassert>
#include <array>
#include <vector>
#include <cstdio>
static twai_event_callbacks_t callbacks;
static void *context;
static std::vector<const twai_frame_t*> pending;
static bool reject=false, bus_off=false, immediate=false;
static int recoveries=0;
int twai_new_node_onchip(const twai_onchip_node_config_t *cfg,twai_node_handle_t *node) {
 assert(cfg->tx_queue_depth==20 && cfg->bit_timing.bitrate==250000);
 assert(cfg->fail_retry_cnt==-1);
 *node=reinterpret_cast<void*>(1); return ESP_OK;
}
int twai_node_register_event_callbacks(twai_node_handle_t,const twai_event_callbacks_t *cb,void *ctx) {
 callbacks=*cb;context=ctx;return ESP_OK;
}
int twai_node_recover(twai_node_handle_t) { ++recoveries;bus_off=false;return ESP_OK; }
int twai_node_transmit(twai_node_handle_t node,const twai_frame_t *frame,int timeout) {
 assert(timeout==0);
 if (reject || bus_off || pending.size()==21) return ESP_ERR_TIMEOUT;
 if (immediate) {
  twai_tx_done_event_data_t event{true,frame};
  callbacks.on_tx_done(node,&event,context);
 } else pending.push_back(frame); // Borrow, exactly as ESP-IDF does. Do not copy.
 return ESP_OK;
}
struct Transport : tNMEA2000_esp32xx {
 using tNMEA2000_esp32xx::CANOpen;
 using tNMEA2000_esp32xx::CANSendFrame;
};
static void complete(size_t index,bool success=true) {
 auto frame=pending.at(index);pending.erase(pending.begin()+index);
 twai_tx_done_event_data_t event{success,frame};
 callbacks.on_tx_done(reinterpret_cast<void*>(1),&event,context);
}
static void send(Transport &t,unsigned i) {
 std::array<uint8_t,8> bytes{};bytes.fill(static_cast<uint8_t>(i));
 assert(t.CANSendFrame(0x1ffffff0+i,8,bytes.data(),false));
 bytes.fill(0xee); // Caller buffer lifetime ends, but queued content must survive.
}
static void check(unsigned start) {
 for(size_t i=0;i<pending.size();++i) {
  const auto *f=pending[i];
  assert(f->header.id==0x1ffffff0+start+i && f->header.ide && f->header.dlc==8);
  assert(f->buffer_len==8);
  for(size_t j=0;j<8;++j) assert(f->buffer[j]==start+i);
  for(size_t j=0;j<i;++j) {
   assert(f!=pending[j]);assert(f->buffer!=pending[j]->buffer);
  }
 }
}
int main() {
 Transport t;
 assert(!t.CANSendFrame(0,0,nullptr,false));
 assert(t.CANOpen());
 assert(fake_rx_capacity==128);
 fake_rx_available=true;
 for(unsigned i=0;i<135;++i)callbacks.on_rx_done(nullptr,nullptr,context);
 assert(t.RxDropped()==7 && fake_rx_queued==128);
 fake_rx_available=false;callbacks.on_rx_done(nullptr,nullptr,context);
 assert(t.RxDropped()==7); // Driver receive errors are not queue drops.

 for(unsigned i=0;i<21;++i) send(t,i);
 check(0);
 assert(!t.CANSendFrame(0,0,nullptr,false));check(0);
 complete(0);send(t,21);check(1); // Only the completed slot may be reused.
 complete(0,false);send(t,22);check(2); // Failed completion releases storage too.
 bus_off=true;
 twai_state_change_event_data_t state{0,TWAI_ERROR_BUS_OFF};
 callbacks.on_state_change(nullptr,&state,context);
 assert(!t.CANSendFrame(0,0,nullptr,false));
 assert(recoveries==1);check(2); // Recovery must not reclaim still-queued frames.
 while(!pending.empty()) complete(0);
 reject=true;
 for(int i=0;i<50;++i) assert(!t.CANSendFrame(0,0,nullptr,false));
 reject=false;
 for(unsigned i=0;i<21;++i) send(t,i);
 check(0);while(!pending.empty()) complete(0);
 immediate=true;
 for(unsigned i=0;i<50;++i) send(t,i); // ISR may complete before transmit returns.
 puts("TWAI asynchronous frame lifetime and backpressure tests passed");
}
