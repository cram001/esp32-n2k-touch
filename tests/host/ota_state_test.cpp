#include <algorithm>
#include <cassert>
#include <cstdio>
#include "ota.hpp"
#include "esp_err.h"
#include "gateway/sdk.hpp"
SemaphoreHandle_t g_mutex=reinterpret_cast<void *>(1);
OtaStatus g_status;bool g_active=true;
void ensure_mutex(){}
int xSemaphoreTake(SemaphoreHandle_t,uint32_t wait){return wait==portMAX_DELAY?pdTRUE:0;}
void xSemaphoreGive(SemaphoreHandle_t){}
#include "ota_state.inc"
int main(){
 set_active(false);assert(!g_active);
 set_status(OtaState::Failed,101,ESP_FAIL,"failed");
 assert(g_status.state==OtaState::Failed && g_status.progress_percent==100 && g_status.last_error==ESP_FAIL);
 set_status(OtaState::Idle,-1,ESP_OK,nullptr);assert(g_status.progress_percent==0 && !g_status.message[0]);
 std::puts("PASS: OTA active/status updates wait through transient mutex contention");
}
