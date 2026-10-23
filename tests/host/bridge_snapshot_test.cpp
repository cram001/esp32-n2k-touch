#include <cassert>
#include <cstdio>
#include <atomic>
#include "app_settings.hpp"
#include "gateway/sdk.hpp"
AppSettings g_settings,g_cached_settings;
SemaphoreHandle_t g_settings_mutex=reinterpret_cast<void *>(1);
bool timeout=false;
int xSemaphoreTake(SemaphoreHandle_t,uint32_t){return timeout?0:pdTRUE;}
void xSemaphoreGive(SemaphoreHandle_t){}
#include "n2k_settings_snapshot.inc"
int main(){
 g_settings.n2k_input.mode=N2kInputMode::W2kTcp;g_settings.day_brightness=42;
 assert(settings_snapshot().n2k_input.mode==N2kInputMode::W2kTcp);
 timeout=true;auto copy=settings_snapshot();
 assert(copy.n2k_input.mode==N2kInputMode::W2kTcp && copy.day_brightness==42);
 timeout=false;g_settings.n2k_input.mode=N2kInputMode::Wired;
 assert(settings_snapshot().n2k_input.mode==N2kInputMode::Wired);
 std::puts("PASS: snapshot timeout retains settings and input mode");
}
