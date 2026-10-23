#include <cassert>
#include <cstdio>
#include "local_server_window.hpp"
#include "ota.hpp"
#include "esp_err.h"
#include "wifi_service.hpp"
#include "gateway/sdk.hpp"
SemaphoreHandle_t g_mutex=reinterpret_cast<void *>(1);
LocalServerWindow g_window;
void *g_server_worker=nullptr;
std::atomic<int64_t> g_server_deadline_us{0};
bool g_active=false;OtaStatus g_status;
constexpr int ESP_ERR_NO_MEM=0x101;
int64_t now=1000;int creates=0;
void ensure_mutex(){}
WifiStatus wifi_service_get_status(){WifiStatus s;s.state=WifiState::Connected;return s;}
int64_t esp_timer_get_time(){return now;}
int server_heap(){return 0;} void publish_server_status(LocalServerStatus,int){} void ota_log_local_server_status(){}
int xSemaphoreTake(SemaphoreHandle_t,uint32_t){return pdTRUE;} void xSemaphoreGive(SemaphoreHandle_t){}
void local_server_worker(void *){}
int xTaskCreate(void (*)(void *),const char *,unsigned,void *,int,void *handle){++creates;if(handle)*static_cast<void **>(handle)=reinterpret_cast<void *>(1);return pdPASS;}
#include "ota_window_api.inc"
int main(){
 assert(ota_enable_local_server(120));assert(creates==1);
 g_window.state=LocalServerWindow::State::Listening;
 g_window.deadline=now;
 assert(g_window.stop_if_due(now,false));
 auto deadline=g_window.deadline;
 assert(!ota_enable_local_server(120));assert(g_window.deadline==deadline && creates==1);
 g_window.stopped();now+=1000;
 assert(ota_enable_local_server(120));assert(creates==2 && g_window.deadline==now+120000000);
 // Extension and stop decisions serialize on the same mutex in production.
 g_window.state=LocalServerWindow::State::Listening;
 assert(ota_enable_local_server(60));assert(creates==2);
 assert(!g_window.stop_if_due(now,false));
 assert(ota_disable_local_server());assert(g_window.stop_if_due(now,false));
 std::puts("PASS: enable during stop rejected; restart retains deadline; extension wins expiry");
}
