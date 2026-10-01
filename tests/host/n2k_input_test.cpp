#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include <functional>
#include "n2k_input.hpp"
#include "wifi_service.hpp"
#include "gateway/sdk.hpp"
#include "gateway/lwip/sockets.h"

namespace {
struct Stop {};
uint32_t ticks=0; unsigned remaining=0, opens=0, closes=0;
void (*worker)(void *)=nullptr;
std::function<void()> hook;
std::deque<std::vector<unsigned char>> packets;
size_t packet_size=0; unsigned capacity=0;
WifiStatus wifi;
std::deque<std::string> reads;
bool eof=false, connect_error=false, wait_connect=false;
std::string target_ip;uint16_t target_port=0;
void run(unsigned n) {
    remaining=n;
    try { worker(nullptr); } catch (const Stop &) {}
}
}
SemaphoreHandle_t xSemaphoreCreateMutex(){return reinterpret_cast<void *>(1);}
int xSemaphoreTake(SemaphoreHandle_t,uint32_t){return pdTRUE;}
void xSemaphoreGive(SemaphoreHandle_t){}
void vSemaphoreDelete(SemaphoreHandle_t){}
QueueHandle_t xQueueCreate(unsigned n,size_t size){capacity=n;packet_size=size;return reinterpret_cast<void *>(2);}
int xQueueSend(QueueHandle_t,const void *data,uint32_t wait){
    assert(wait==0);if(packets.size()>=capacity)return 0;
    const auto *p=static_cast<const unsigned char *>(data);packets.emplace_back(p,p+packet_size);return pdTRUE;
}
int xQueueReceive(QueueHandle_t,void *data,uint32_t wait){
    assert(wait==0);if(packets.empty())return 0;
    std::memcpy(data,packets.front().data(),packet_size);packets.pop_front();return pdTRUE;
}
void vQueueDelete(QueueHandle_t){}
int xTaskCreate(void (*task)(void *),const char *,unsigned stack,void *,int,void *){assert(stack>=4096);worker=task;return pdPASS;}
TickType_t xTaskGetTickCount(){return ticks;}
void vTaskDelay(uint32_t delay){assert(delay==20);ticks+=delay;if(hook)hook();if(--remaining==0)throw Stop{};}
WifiStatus wifi_service_get_status(){return wifi;}
int gateway_socket(int,int,int){++opens;return 3;}
int gateway_fcntl(int,int,int){return 0;}
int gateway_connect(int,const sockaddr *p,size_t){
    target_port=reinterpret_cast<const sockaddr_in *>(p)->sin_port;
    errno=EINPROGRESS;return -1;
}
int gateway_select(int,void *,gateway_fd_set *,gateway_fd_set *,gateway_timeval *t){assert(t->tv_sec==0&&t->tv_usec==0);return wait_connect?0:1;}
int gateway_getsockopt(int,int,int,int *error,socklen_t *){*error=connect_error?ECONNREFUSED:0;return 0;}
int gateway_recv(int,char *out,size_t size,int){
    if(!reads.empty()){
        std::string &front=reads.front();const size_t n=std::min(size,front.size());
        std::memcpy(out,front.data(),n);front.erase(0,n);if(front.empty())reads.pop_front();return n;
    }
    if(eof)return 0;errno=EAGAIN;return -1;
}
int gateway_close(int){++closes;return 0;}
uint16_t htons(uint16_t port){return port;}
int inet_pton(int,const char *ip,in_addr *){target_ip=ip;return 1;}

int main(){
    N2kInputConfig cfg;assert(n2k_input_start(cfg));run(2);
    assert(opens==0 && std::strstr(n2k_input_status().message.data(),"Wired"));
    cfg.mode=N2kInputMode::W2kTcp;cfg.port=60003;std::strcpy(cfg.ip.data(),"192.168.4.1");
    n2k_input_apply(cfg);run(2);assert(opens==0);
    wifi.state=WifiState::Connected;
    const std::string line="A173321 01FF2 1F112 0102030405060708\r\n";
    reads.push_back(line.substr(0,20));reads.push_back(line.substr(20));
    // Model two successive reads without restarting the infinite worker.
    unsigned step=0;ActisenseMessage msg;bool received=false;
    hook=[&](){if(++step==2){received=n2k_input_receive(msg);}};
    run(3);hook={};
    assert(received && msg.pgn==127250 && msg.length==8);
    assert(target_ip=="192.168.4.1" && target_port==60003);
    assert(n2k_input_status().received==1);
    // Queue retains a complete old-endpoint packet until receive rejects it.
    reads.push_back(line);run(1);
    cfg.port=60002;n2k_input_apply(cfg);assert(!n2k_input_receive(msg));
    reads.push_back("wrong format\n");run(2);
    assert(n2k_input_status().rejected==1);
    // Wrong port is retried after the bounded five-second backoff.
    eof=true;connect_error=true;
    const unsigned before=opens;run(10);assert(opens==before+1);
    run(260);assert(opens>=before+2);
    assert(n2k_input_status().socket_error==ECONNREFUSED);
    // A connection that never completes times out rather than blocking.
    connect_error=false;wait_connect=true;eof=false;cfg.port=60001;n2k_input_apply(cfg);
    run(260);assert(n2k_input_status().socket_error==ETIMEDOUT);
    wait_connect=false;cfg.port=60003;n2k_input_apply(cfg);
    for(unsigned i=0;i<20;++i)reads.push_back(line);
    run(22);assert(packets.size()==16 && n2k_input_status().dropped==4);
    cfg.mode=N2kInputMode::Wired;n2k_input_apply(cfg);assert(!n2k_input_receive(msg));
    run(2);assert(closes>0 && std::strstr(n2k_input_status().message.data(),"Wired"));
    std::puts("PASS: TCP framing, endpoint changes, stale queue rejection, network wait, refusal/backoff, timeout and queue bounds");
}
