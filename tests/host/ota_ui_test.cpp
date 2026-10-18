#include <cassert>
#include <cstring>
#include <cstdio>
#include "ota.hpp"
#include "wifi_service.hpp"
struct lv_event_t {};
using lv_obj_t=int;
int obj=1; int *g_ota_status=nullptr,*g_ota_screen=&obj,*g_ota_web_button=nullptr,*g_ota_url=&obj,*g_ota_keyboard=&obj;
char g_ota_feedback[128]{}; unsigned g_ota_feedback_time=0;
bool active=false,enabled=false; WifiState state=WifiState::Disabled;
unsigned lv_tick_elaps(unsigned){return 0;} int *lv_screen_active(){return nullptr;}
void lv_label_set_text(int *,const char *){} int *button_label(int *p){return p;}
void lv_screen_load(int *){} void hide_wifi_keyboard(){} void esp_restart(){}
void lv_keyboard_set_textarea(int *,int *){} void lv_obj_remove_flag(int *,int){} void lv_obj_move_foreground(int *){} void lv_obj_add_flag(int *,int){}
int lv_event_get_code(lv_event_t *){return 0;} const char *lv_textarea_get_text(int *){return "";}
constexpr int LV_OBJ_FLAG_HIDDEN=1,LV_EVENT_READY=2,LV_EVENT_CANCEL=3;
void ota_feedback(const char *s){std::snprintf(g_ota_feedback,sizeof(g_ota_feedback),"%s",s);}
OtaStatus ota_get_status(){return {};} WifiStatus wifi_service_get_status(){WifiStatus s;s.state=state;return s;}
LocalServerStatus ota_local_server_status(){return {};} uint32_t ota_local_server_seconds_remaining(){return 0;}
bool ota_local_server_window_active(){return active;} const char *ota_running_version(){return "test";}
bool ota_partition_layout_valid(){return true;} const char *ota_local_server_stage_name(LocalServerStage){return "test";}
bool wifi_service_is_connected(){return state==WifiState::Connected;} bool ota_start_https(const char *){return true;}
bool ota_disable_local_server(){return true;} bool ota_enable_local_server(uint32_t){enabled=true;return true;}
#include "ui_ota.inc"
int main(){
    ota_web_cb(nullptr);assert(!enabled && std::strstr(g_ota_feedback,"Station"));
    state=WifiState::Connecting;ota_web_cb(nullptr);assert(!enabled);
    state=WifiState::Connected;ota_web_cb(nullptr);assert(enabled);
    assert(!std::strstr(g_ota_feedback,"Bluetooth"));
    std::puts("PASS: connected Station can enable the local upload window");
}
