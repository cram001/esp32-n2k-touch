#include "ui.hpp"
#include "boot_brightness.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bsp/display.h"
#include "esp_err.h"
#include "esp_log.h"
#include "instrument_data.hpp"
#include "lvgl.h"
#include "n2k_bridge.hpp"
#include "n2k_input.hpp"
#include "n2k_sources.hpp"
#include <cmath>
#include "smartshunt_ble.hpp"
#include "tile_number_fit.hpp"
#include "depth_display.hpp"
#include "wifi_service.hpp"
#include "ota.hpp"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "ui";

lv_obj_t *require_obj(lv_obj_t *obj, const char *what)
{
    if (obj == nullptr) {
        ESP_LOGE(TAG, "LVGL allocation failed for %s", what);
        std::abort();
    }
    return obj;
}

AppSettings g_settings;
lv_obj_t *g_rotation_button=nullptr;
size_t g_active_page = 0;
size_t g_edit_page = 0;
size_t g_edit_field = 0;
size_t g_edit_shunt = 0;

lv_obj_t *g_data_screen = nullptr;
lv_obj_t *g_boot_screen = nullptr;
lv_obj_t *g_boot_status = nullptr;
lv_obj_t *g_boot_recovery_status = nullptr;
BootBrightnessRecovery g_boot_recovery;
lv_indev_t *g_boot_indev = nullptr;
bool g_boot_brightness_override = false;
lv_obj_t *g_input_screen = nullptr;
lv_obj_t *g_source_picker = nullptr, *g_source_list = nullptr, *g_source_status = nullptr;
lv_obj_t *g_metric_picker = nullptr, *g_metric_list = nullptr;
lv_obj_t *g_depth_screen = nullptr, *g_depth_mode = nullptr, *g_depth_keel = nullptr, *g_depth_waterline = nullptr;
lv_obj_t *g_depth_keyboard = nullptr, *g_depth_status = nullptr;
std::array<N2kSourceOption,16> g_source_options{};
size_t g_source_count = 0;
DepthConfig g_depth_draft{};
N2kSourceChoice g_depth_source{};
bool g_source_depth_picker=false;
lv_obj_t *g_depth_source_button=nullptr;
void source_picker_cb(lv_event_t *);
void metric_picker_cb(lv_event_t *);
void depth_screen_cb(lv_event_t *);
uint8_t editing_field_id() {return static_cast<uint8_t>(g_edit_page*MAX_DATA_FIELDS_PER_PAGE+g_edit_field);}
uint8_t editing_choice_id() {return n2k_sources_is_gps(g_settings.pages[g_edit_page].fields[g_edit_field].metric)?N2K_GPS_CHOICE:editing_field_id();}
lv_obj_t *g_input_mode = nullptr;
lv_obj_t *g_input_ip = nullptr;
lv_obj_t *g_input_port = nullptr;
lv_obj_t *g_input_status = nullptr;
lv_obj_t *g_input_keyboard = nullptr;
N2kInputConfig g_input_draft{};
bool g_input_feedback = false;
void update_input_status()
{
    const N2kInputStatus input=n2k_input_status();
    if(g_input_status && !g_input_feedback) {
        if(g_input_draft.mode==N2kInputMode::Wired)
            lv_label_set_text(g_input_status,"Source: CAN / TWAI\nVictron Bluetooth enabled; Wi-Fi N2K disabled.");
        else
            lv_label_set_text_fmt(g_input_status,"Source: Wi-Fi / W2K-1 TCP\n%s\nMessages: %lu  Rejected: %lu  Dropped: %lu",
                input.message.data(),static_cast<unsigned long>(input.received),
                static_cast<unsigned long>(input.rejected),static_cast<unsigned long>(input.dropped));
    }
    if(g_boot_status) {
        if(g_settings.n2k_input.mode==N2kInputMode::Wired)
            lv_label_set_text(g_boot_status,"NMEA 2000: CAN / TWAI\nVictron Bluetooth enabled");
        else {
            const WifiStatus wifi=wifi_service_get_status();
            lv_label_set_text_fmt(g_boot_status,"NMEA 2000: Wi-Fi / W2K-1\nWi-Fi: %s\nN2K: %s",wifi.message.data(),input.message.data());
        }
    }
}
void input_keyboard_hide() {
    lv_obj_add_flag(g_input_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(g_input_keyboard, nullptr);
}
void input_keyboard_cb(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_READY || lv_event_get_code(event) == LV_EVENT_CANCEL) input_keyboard_hide();
}
void input_focus_cb(lv_event_t *event) {
    lv_obj_t *target = lv_event_get_target_obj(event);
    lv_keyboard_set_mode(g_input_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(g_input_keyboard, target);
    lv_obj_remove_flag(g_input_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_input_keyboard);
}
void input_mode_cb(lv_event_t *) {
    g_input_draft.mode=g_input_draft.mode==N2kInputMode::Wired?N2kInputMode::W2kTcp:N2kInputMode::Wired;
    lv_label_set_text(button_label(g_input_mode),
        g_input_draft.mode==N2kInputMode::Wired?"SOURCE: CAN NMEA 2000":"SOURCE: WI-FI NMEA 2000");
    update_input_status();
}
void input_screen_cb(lv_event_t *) {
    g_input_feedback=false;
    g_input_draft=g_settings.n2k_input;
    lv_label_set_text(button_label(g_input_mode),
        g_input_draft.mode==N2kInputMode::Wired?"SOURCE: CAN NMEA 2000":"SOURCE: WI-FI NMEA 2000");
    lv_textarea_set_text(g_input_ip,g_input_draft.ip.data());
    char port[8];std::snprintf(port,sizeof(port),"%u",g_input_draft.port);
    lv_textarea_set_text(g_input_port,port);
    input_keyboard_hide();update_input_status();lv_screen_load(g_input_screen);
}
void input_back_cb(lv_event_t *) {input_keyboard_hide();lv_screen_load(g_settings_screen);}
void input_save_cb(lv_event_t *) {
    input_keyboard_hide();
    std::snprintf(g_input_draft.ip.data(),g_input_draft.ip.size(),"%s",lv_textarea_get_text(g_input_ip));
    const char *text=lv_textarea_get_text(g_input_port);char *end=nullptr;
    const unsigned long port=std::strtoul(text,&end,10);
    if(g_input_draft.mode==N2kInputMode::W2kTcp &&
       (!*text || *end || port>65535 || !n2k_endpoint_valid(g_input_draft.ip.data(),static_cast<uint16_t>(port)))) {
        g_input_feedback=true;lv_label_set_text(g_input_status,"Wi-Fi NMEA 2000 requires a valid W2K-1 IPv4 address and port");return;
    }
    if(*text && !*end && port && port<=65535) g_input_draft.port=static_cast<uint16_t>(port);

    AppSettings candidate=g_settings;
    candidate.n2k_input=g_input_draft;
    if(candidate.n2k_input.mode==N2kInputMode::W2kTcp) {
        WifiConfig wifi=candidate.wifi;wifi.enabled=true;wifi.mode=WifiMode::Station;
        const char *reason=nullptr;
        if(!wifi_config_valid(wifi,&reason)) {
            g_input_feedback=true;lv_label_set_text_fmt(g_input_status,"Configure Station Wi-Fi first: %s",reason?reason:"invalid Wi-Fi settings");return;
        }
    }

    const bool source_changed=candidate.n2k_input.mode!=g_settings.n2k_input.mode;
    if(!settings_save(candidate)) {
        g_input_feedback=true;lv_label_set_text(g_input_status,"Could not save NMEA source; please retry");return;
    }
    g_settings=candidate;
    if(source_changed) {
        lv_label_set_text(g_input_status,"NMEA source saved. Rebooting...");
        vTaskDelay(pdMS_TO_TICKS(150));
        esp_restart();
        return;
    }
    g_input_feedback=false;
    n2k_bridge_apply_settings(g_settings);
    update_input_status();
}
void source_choice_cb(lv_event_t *event) {
    const size_t item=reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    const N2kSourceChoice choice=item?g_source_options[item-1].choice:N2kSourceChoice{};
    if(g_source_depth_picker){
        g_depth_source=choice;
        lv_label_set_text(button_label(g_depth_source_button),item?g_source_options[item-1].label.data():"DEFAULT FOR UNCONFIGURED SENSORS");
        depth_screen_cb(nullptr);return;
    }
    if(!n2k_sources_save_choice(editing_choice_id(),choice)){lv_label_set_text(g_source_status,"Could not save selection; please retry");return;}
    update_field_editor();lv_screen_load(g_field_editor_screen);
}
void source_back_cb(lv_event_t *) {
    if(lv_screen_active()==g_source_picker && g_source_depth_picker){lv_screen_load(g_depth_screen);return;}
    update_field_editor();lv_screen_load(g_field_editor_screen);
}
void source_refresh_cb(lv_event_t *) {n2k_bridge_request_sources();source_picker_cb(nullptr);}
void source_picker_cb(lv_event_t *) {
    const auto &field=g_settings.pages[g_edit_page].fields[g_edit_field];
    lv_obj_clean(g_source_list);
    const DataMetric metric=g_source_depth_picker?DataMetric::Depth:field.metric;
    g_source_count=n2k_sources_options(metric,g_source_options.data(),g_source_options.size());
    lv_label_set_text(g_source_status,n2k_sources_is_gps(metric)?"One GPS for position, altitude, speed and course":"Selection stays locked; missing data shows --");
    lv_obj_t *button=make_button(g_source_list,g_source_depth_picker?"DEFAULT FOR UNCONFIGURED SENSORS":"AUTOMATIC: FIRST SOURCE (NO FALLBACK)",source_choice_cb,410,54);
    lv_obj_set_pos(button,0,0);
    for(size_t i=0;i<g_source_count;++i){
        char text[110];std::snprintf(text,sizeof(text),"%s%s",g_source_options[i].label.data(),g_source_options[i].stale?" / stale":"");
        button=make_button(g_source_list,text,source_choice_cb,410,74,reinterpret_cast<void *>(i+1));
        lv_obj_set_width(button_label(button),390);lv_label_set_long_mode(button_label(button),LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_pos(button,0,static_cast<int>(60+i*80));
    }
    if(!g_source_count)lv_label_set_text(g_source_status,"No sources received yet. Wait for data, then Refresh.");
    lv_screen_load(g_source_picker);
}
void metric_choice_cb(lv_event_t *event) {
    const auto metric=static_cast<DataMetric>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    auto &field=g_settings.pages[g_edit_page].fields[g_edit_field];
    if(field.source==DataSourceType::Nmea2000 && field.metric!=metric && !n2k_sources_save_choice(editing_field_id(),N2kSourceChoice{}))return;
    field.metric=metric;persist();update_field_editor();lv_screen_load(g_field_editor_screen);
}
void metric_picker_cb(lv_event_t *) {
    lv_obj_clean(g_metric_list);
    const bool nmea=g_settings.pages[g_edit_page].fields[g_edit_field].source==DataSourceType::Nmea2000;
    size_t row=0;
    for(unsigned i=0;i<static_cast<unsigned>(DataMetric::Count);++i){
        const auto metric=static_cast<DataMetric>(i);
        if(metric!=DataMetric::None && !instrument_metric_supported(nmea?DataSourceType::Nmea2000:DataSourceType::SmartShunt,metric))continue;
        lv_obj_t *button=make_button(g_metric_list,instrument_metric_name(metric),metric_choice_cb,410,48,reinterpret_cast<void *>(i));
        lv_obj_set_pos(button,0,static_cast<int>(row++*54));
    }
    lv_screen_load(g_metric_picker);
}
const char *depth_reference_name(DepthReference ref) {
    switch(ref){case DepthReference::SensorOffset:return "SENSOR-PROVIDED OFFSET";case DepthReference::Keel:return "BELOW KEEL";
    case DepthReference::Waterline:return "BELOW WATER SURFACE";default:return "BELOW TRANSDUCER";}
}
void depth_keyboard_hide(){lv_obj_add_flag(g_depth_keyboard,LV_OBJ_FLAG_HIDDEN);lv_keyboard_set_textarea(g_depth_keyboard,nullptr);}
void depth_keyboard_cb(lv_event_t *event){if(lv_event_get_code(event)==LV_EVENT_READY||lv_event_get_code(event)==LV_EVENT_CANCEL)depth_keyboard_hide();}
void depth_focus_cb(lv_event_t *event){lv_keyboard_set_mode(g_depth_keyboard,LV_KEYBOARD_MODE_NUMBER);lv_keyboard_set_textarea(g_depth_keyboard,lv_event_get_target_obj(event));lv_obj_remove_flag(g_depth_keyboard,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(g_depth_keyboard);}
void depth_mode_cb(lv_event_t *) {g_depth_draft.reference=static_cast<DepthReference>((static_cast<unsigned>(g_depth_draft.reference)+1)%4);lv_label_set_text(button_label(g_depth_mode),depth_reference_name(g_depth_draft.reference));}
void depth_screen_cb(lv_event_t *) {
    g_depth_draft=n2k_sources_depth(&g_depth_source);depth_keyboard_hide();
    lv_label_set_text(button_label(g_depth_mode),depth_reference_name(g_depth_draft.reference));
    char value[20];std::snprintf(value,sizeof(value),"%.3f",g_depth_draft.transducer_to_keel_m);lv_textarea_set_text(g_depth_keel,g_depth_draft.keel_set?value:"");
    std::snprintf(value,sizeof(value),"%.3f",g_depth_draft.transducer_to_waterline_m);lv_textarea_set_text(g_depth_waterline,g_depth_draft.waterline_set?value:"");
    lv_label_set_text(g_depth_status,"Sensor offset is used once; local modes use raw depth.");lv_screen_load(g_depth_screen);
}
void depth_back_cb(lv_event_t *) {depth_keyboard_hide();lv_screen_load(g_settings_screen);}
bool depth_distance(lv_obj_t *field,float &value,bool &set) {
    const char *text=lv_textarea_get_text(field);set=*text;
    if(!set){value=0;return true;}
    char *end=nullptr;value=std::strtof(text,&end);return end && !*end && std::isfinite(value) && value>=0 && value<=100;
}
void depth_save_cb(lv_event_t *) {
    depth_keyboard_hide();
    if(g_depth_source.mode==SourceChoiceMode::Automatic && (g_depth_draft.reference==DepthReference::Keel || g_depth_draft.reference==DepthReference::Waterline || *lv_textarea_get_text(g_depth_keel) || *lv_textarea_get_text(g_depth_waterline))){
        lv_label_set_text(g_depth_status,"Select the depth sensor before entering installation distances.");return;
    }
    if(!depth_distance(g_depth_keel,g_depth_draft.transducer_to_keel_m,g_depth_draft.keel_set) ||
       !depth_distance(g_depth_waterline,g_depth_draft.transducer_to_waterline_m,g_depth_draft.waterline_set) ||
       (g_depth_draft.reference==DepthReference::Keel&&!g_depth_draft.keel_set) ||
       (g_depth_draft.reference==DepthReference::Waterline&&!g_depth_draft.waterline_set)){
        lv_label_set_text(g_depth_status,"Enter the required distance in metres (0-100).");return;
    }
    lv_label_set_text(g_depth_status,n2k_sources_save_depth(g_depth_draft,&g_depth_source)?"Depth settings saved":"Could not save depth settings; please retry");render_active_page();
}
void depth_source_cb(lv_event_t *) {depth_keyboard_hide();g_source_depth_picker=true;n2k_bridge_request_sources();source_picker_cb(nullptr);}
void create_source_depth_screens() {
    auto picker=[](const char *title,lv_obj_t *&list){
        lv_obj_t *screen=require_obj(lv_obj_create(nullptr),"source/metric screen");lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *label=lv_label_create(screen);lv_label_set_text(label,title);lv_obj_align(label,LV_ALIGN_TOP_MID,0,18);
        list=lv_obj_create(screen);lv_obj_set_size(list,440,305);lv_obj_align(list,LV_ALIGN_TOP_MID,0,78);lv_obj_set_scroll_dir(list,LV_DIR_VER);
        lv_obj_t *back=make_button(screen,"BACK",source_back_cb,150,48);lv_obj_align(back,LV_ALIGN_BOTTOM_LEFT,35,-18);return screen;
    };
    g_source_picker=picker("SELECT NMEA SOURCE",g_source_list);
    g_source_status=lv_label_create(g_source_picker);lv_obj_set_width(g_source_status,440);lv_obj_set_style_text_align(g_source_status,LV_TEXT_ALIGN_CENTER,0);lv_obj_align(g_source_status,LV_ALIGN_TOP_MID,0,45);
    lv_obj_t *button=make_button(g_source_picker,"REFRESH",source_refresh_cb,150,48);lv_obj_align(button,LV_ALIGN_BOTTOM_RIGHT,-35,-18);
    g_metric_picker=picker("SELECT DATA FIELD",g_metric_list);
    g_depth_screen=require_obj(lv_obj_create(nullptr),"depth screen");lv_obj_remove_flag(g_depth_screen,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *label=lv_label_create(g_depth_screen);lv_label_set_text(label,"DEPTH SETUP");lv_obj_set_style_text_font(label,&lv_font_montserrat_24,0);lv_obj_align(label,LV_ALIGN_TOP_MID,0,18);
    g_depth_source_button=make_button(g_depth_screen,"DEFAULT FOR UNCONFIGURED SENSORS",depth_source_cb,410,54);
    lv_obj_set_width(button_label(g_depth_source_button),390);lv_label_set_long_mode(button_label(g_depth_source_button),LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(g_depth_source_button,LV_ALIGN_TOP_MID,0,56);
    g_depth_mode=make_button(g_depth_screen,"BELOW TRANSDUCER",depth_mode_cb,380,48);lv_obj_align(g_depth_mode,LV_ALIGN_TOP_MID,0,120);
    auto field=[](const char *text,int y){
        lv_obj_t *label=lv_label_create(g_depth_screen);lv_label_set_text(label,text);lv_obj_align(label,LV_ALIGN_TOP_LEFT,25,y);
        lv_obj_t *ta=lv_textarea_create(g_depth_screen);lv_obj_set_size(ta,175,48);lv_obj_align(ta,LV_ALIGN_TOP_RIGHT,-25,y-14);
        lv_textarea_set_one_line(ta,true);lv_textarea_set_max_length(ta,8);lv_textarea_set_accepted_chars(ta,"0123456789.");lv_obj_add_event_cb(ta,depth_focus_cb,LV_EVENT_FOCUSED,nullptr);return ta;
    };
    g_depth_keel=field("Transducer to keel (m)",190);g_depth_waterline=field("Transducer to surface (m)",248);
    label=lv_label_create(g_depth_screen);lv_obj_set_width(label,430);
    lv_label_set_text(label,"Distances belong to the selected transducer.\nKeel: raw minus distance; surface: raw plus distance.\nDepth >1000m and 30s-old values show --.");lv_obj_align(label,LV_ALIGN_TOP_MID,0,288);
    g_depth_status=lv_label_create(g_depth_screen);lv_obj_set_width(g_depth_status,430);lv_obj_set_style_text_align(g_depth_status,LV_TEXT_ALIGN_CENTER,0);lv_obj_align(g_depth_status,LV_ALIGN_TOP_MID,0,365);
    button=make_button(g_depth_screen,"SAVE",depth_save_cb,150,48);lv_obj_align(button,LV_ALIGN_BOTTOM_RIGHT,-35,-18);
    button=make_button(g_depth_screen,"BACK",depth_back_cb,150,48);lv_obj_align(button,LV_ALIGN_BOTTOM_LEFT,35,-18);
    g_depth_keyboard=lv_keyboard_create(g_depth_screen);lv_obj_set_size(g_depth_keyboard,460,220);lv_obj_align(g_depth_keyboard,LV_ALIGN_BOTTOM_MID,0,0);lv_obj_add_event_cb(g_depth_keyboard,depth_keyboard_cb,LV_EVENT_ALL,nullptr);depth_keyboard_hide();
}
void create_input_screen() {
    g_input_screen = require_obj(lv_obj_create(nullptr), "NMEA input screen");
    lv_obj_remove_flag(g_input_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lv_label_create(g_input_screen); lv_label_set_text(title, "NMEA 2000 Source");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0); lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);
    g_input_mode = make_button(g_input_screen, "SOURCE: CAN NMEA 2000", input_mode_cb, 400, 48);
    lv_obj_align(g_input_mode, LV_ALIGN_TOP_MID, 0, 64);
    auto field = [](const char *placeholder, int y, size_t max_length, const char *accepted) {
        lv_obj_t *ta = require_obj(lv_textarea_create(g_input_screen), "gateway field");
        lv_obj_set_size(ta, 400, 48); lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, y);
        lv_textarea_set_one_line(ta, true); lv_textarea_set_max_length(ta, max_length);
        lv_textarea_set_placeholder_text(ta, placeholder); lv_textarea_set_accepted_chars(ta, accepted);
        lv_obj_add_event_cb(ta, input_focus_cb, LV_EVENT_FOCUSED, nullptr); return ta;
    };
    g_input_ip = field("W2K-1 IP address", 124, 15, "0123456789.");
    g_input_port = field("TCP port", 182, 5, "0123456789");
    lv_obj_t *hint = lv_label_create(g_input_screen);
    lv_label_set_text(hint, "Choose one NMEA 2000 source only.\nWi-Fi uses W2K-1 TCP / N2K ASCII / Transmit.");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 240);
    g_input_status = lv_label_create(g_input_screen); lv_obj_set_width(g_input_status, 440);
    lv_obj_set_style_text_align(g_input_status, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(g_input_status, LV_ALIGN_TOP_MID, 0, 294);
    lv_obj_t *button = make_button(g_input_screen, "SAVE", input_save_cb, 150, 48);
    lv_obj_align(button, LV_ALIGN_BOTTOM_RIGHT, -40, -22);
    button = make_button(g_input_screen, "BACK", input_back_cb, 150, 48); lv_obj_align(button, LV_ALIGN_BOTTOM_LEFT, 40, -22);
    g_input_keyboard = lv_keyboard_create(g_input_screen); lv_obj_set_size(g_input_keyboard, 460, 220);
    lv_obj_align(g_input_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(g_input_keyboard, input_keyboard_cb, LV_EVENT_ALL, nullptr); input_keyboard_hide();
}
void create_data_screen()
{
    g_data_screen=require_obj(lv_obj_create(nullptr),"screen root");
    lv_obj_remove_flag(g_data_screen,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_data_screen,data_gesture_cb,LV_EVENT_GESTURE,nullptr);
    lv_obj_set_style_bg_color(g_data_screen,ui_bg(),0);
    lv_obj_set_style_pad_all(g_data_screen,0,0);

    g_page_title=lv_label_create(g_data_screen);
    lv_obj_set_style_text_font(g_page_title,&lv_font_montserrat_24,0);
    lv_obj_set_style_text_color(g_page_title,ui_text(),0);
    lv_obj_align(g_page_title,LV_ALIGN_TOP_MID,0,12);

    for(size_t i=0;i<MAX_DATA_FIELDS_PER_PAGE;++i){
        g_tile_boxes[i]=lv_obj_create(g_data_screen);
        lv_obj_remove_flag(g_tile_boxes[i],LV_OBJ_FLAG_SCROLLABLE);
        style_card(g_tile_boxes[i]);

        g_tile_titles[i]=lv_label_create(g_tile_boxes[i]);
        lv_obj_set_style_text_font(g_tile_titles[i],&lv_font_montserrat_20,0);
        g_tile_values[i]=lv_label_create(g_tile_boxes[i]);
        g_tile_units[i]=lv_label_create(g_tile_boxes[i]);
        lv_obj_set_style_text_font(g_tile_units[i],&lv_font_montserrat_20,0);
        g_tile_sources[i]=lv_label_create(g_tile_boxes[i]);
        lv_obj_set_style_text_font(g_tile_sources[i],&lv_font_montserrat_14,0);
    }

    for(size_t i=0;i<MAX_DATA_PAGES;++i){
        g_page_dots[i]=lv_obj_create(g_data_screen);
        lv_obj_remove_flag(g_page_dots[i],LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(g_page_dots[i],8,8);
        lv_obj_set_style_radius(g_page_dots[i],LV_RADIUS_CIRCLE,0);
        lv_obj_set_style_border_width(g_page_dots[i],0,0);
        lv_obj_set_style_pad_all(g_page_dots[i],0,0);
        lv_obj_set_pos(g_page_dots[i],201+static_cast<int>(i)*14,466);
    }
}
void create_settings_screen()
{
    g_settings_screen=require_obj(lv_obj_create(nullptr),"screen root");
    lv_obj_remove_flag(g_settings_screen,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t*l=lv_label_create(g_settings_screen);
    lv_label_set_text(l,"Settings");
    lv_obj_set_style_text_font(l,&lv_font_montserrat_24,0);
    lv_obj_align(l,LV_ALIGN_TOP_MID,0,18);

    lv_obj_t*b=make_button(g_settings_screen,"Pages",page_setup_screen_cb,200,48);
    lv_obj_align(b,LV_ALIGN_TOP_LEFT,30,62);
    b=make_button(g_settings_screen,"UNITS",units_screen_cb,200,48);
    lv_obj_align(b,LV_ALIGN_TOP_RIGHT,-30,62);
    b=make_button(g_settings_screen,"NMEA Source",input_screen_cb,200,48);
    lv_obj_align(b,LV_ALIGN_TOP_LEFT,30,116);
    b=make_button(g_settings_screen,"Depth Setup",depth_screen_cb,200,48);
    lv_obj_align(b,LV_ALIGN_TOP_RIGHT,-30,116);
    b=make_button(g_settings_screen,"Wi-Fi",wifi_screen_cb,260,48);
    lv_obj_align(b,LV_ALIGN_TOP_MID,0,170);
    b=make_button(g_settings_screen,"SmartShunts",shunts_screen_cb,260,48);
    lv_obj_align(b,LV_ALIGN_TOP_MID,0,224);

    b=make_button(g_settings_screen,"Day / Night",theme_toggle_cb,150,46);
    lv_obj_align(b,LV_ALIGN_TOP_LEFT,30,286);
    b=make_button(g_settings_screen,"BR -",brightness_down_cb,80,46);
    lv_obj_align(b,LV_ALIGN_TOP_RIGHT,-125,286);
    b=make_button(g_settings_screen,"BR +",brightness_up_cb,80,46);
    lv_obj_align(b,LV_ALIGN_TOP_RIGHT,-35,286);

    g_rotation_button=make_button(g_settings_screen,"",rotation_cb,380,38);
    lv_obj_align(g_rotation_button,LV_ALIGN_TOP_MID,0,338);rotation_label();

    lv_obj_t *identity = require_obj(lv_label_create(g_settings_screen), "firmware identity");
    const esp_app_desc_t *app = esp_app_get_description();
    lv_label_set_text_fmt(identity, "Firmware: %s\nBuilt: %s %s", app->version, app->date, app->time);
    lv_obj_set_width(identity, 440);
    lv_obj_set_style_text_align(identity, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(identity,&lv_font_montserrat_14,0);
    lv_obj_align(identity, LV_ALIGN_TOP_MID, 0, 382);

    b=make_button(g_settings_screen,"BACK",data_screen_cb,120,46);
    lv_obj_align(b,LV_ALIGN_BOTTOM_MID,0,-18);
}
void create_page_setup_screen()
{
    g_page_setup_screen = require_obj(lv_obj_create(nullptr), "page setup screen");
    lv_obj_remove_flag(g_page_setup_screen, LV_OBJ_FLAG_SCROLLABLE);

    g_page_setup_title = require_obj(lv_label_create(g_page_setup_screen), "page setup title");
    lv_obj_set_style_text_font(g_page_setup_title, &lv_font_montserrat_20, 0);
    lv_obj_align(g_page_setup_title, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *b = make_button(g_page_setup_screen, "< PAGE", edit_prev_page_cb, 90, 42);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, 20, 52);
    b = make_button(g_page_setup_screen, "PAGE >", edit_next_page_cb, 90, 42);
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -20, 52);

    lv_obj_t *l = require_obj(lv_label_create(g_page_setup_screen), "page enabled label");
    lv_label_set_text(l, "Enabled");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 140, 64);

    g_page_enable_switch = require_obj(lv_switch_create(g_page_setup_screen), "page enabled switch");
    lv_obj_align(g_page_enable_switch, LV_ALIGN_TOP_RIGHT, -120, 54);
    lv_obj_add_event_cb(g_page_enable_switch, page_enabled_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    b = make_button(g_page_setup_screen, "", layout_cb, 180, 44);
    g_layout_button_label = button_label(b);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 106);

    for (size_t i = 0; i < MAX_DATA_FIELDS_PER_PAGE; ++i) {
        b = make_button(g_page_setup_screen, "", open_field_editor_cb, 216, 74, nullptr);
        g_field_buttons[i] = b;
        g_field_button_labels[i] = button_label(b);
        lv_label_set_long_mode(g_field_button_labels[i], LV_LABEL_LONG_WRAP);
        lv_obj_set_width(g_field_button_labels[i], 196);
        lv_obj_set_style_text_align(g_field_button_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(b, 16 + static_cast<int>(i % 2) * 232,
                      164 + static_cast<int>(i / 2) * 82);
        lv_obj_remove_event_cb(b, open_field_editor_cb);
        lv_obj_add_event_cb(b, open_field_editor_cb, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(i));
    }

    b = make_button(g_page_setup_screen, "BACK", settings_screen_cb, 120, 46);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -12);
}

void create_field_editor_screen()
{
    g_field_editor_screen = require_obj(lv_obj_create(nullptr), "field editor screen");
    lv_obj_remove_flag(g_field_editor_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l = require_obj(lv_label_create(g_field_editor_screen), "field editor title");
    lv_label_set_text(l, "DATA FIELD");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 24);

    l = require_obj(lv_label_create(g_field_editor_screen), "field source label");
    lv_label_set_text(l, "Source");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 40, 95);
    lv_obj_t *b = make_button(g_field_editor_screen, "SOURCE", field_source_cb, 190, 52);
    g_field_source_label = button_label(b);
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -40, 80);

    l = require_obj(lv_label_create(g_field_editor_screen), "field data label");
    lv_label_set_text(l, "Data");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 40, 175);
    b = make_button(g_field_editor_screen, "<", field_metric_prev_cb, 55, 50);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, 130, 157);
    b = make_button(g_field_editor_screen, "", metric_picker_cb, 190, 50);
    g_field_metric_label = button_label(b);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 55, 157);
    b = make_button(g_field_editor_screen, ">", field_metric_next_cb, 55, 50);
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -25, 157);

    l = require_obj(lv_label_create(g_field_editor_screen), "field device label");
    lv_label_set_text(l, "Device");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 40, 250);
    g_field_device_button = make_button(g_field_editor_screen, "", field_device_cb, 320, 100);
    lv_obj_set_width(button_label(g_field_device_button),300);
    lv_label_set_long_mode(button_label(g_field_device_button),LV_LABEL_LONG_MODE_WRAP);
    g_field_device_label = button_label(g_field_device_button);
    lv_obj_align(g_field_device_button, LV_ALIGN_TOP_RIGHT, -40, 232);

    b = make_button(g_field_editor_screen, "DONE", field_done_cb, 150, 54);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -40);
}

void create_units_screen()
{
    g_units_screen = require_obj(lv_obj_create(nullptr), "units screen");
    lv_obj_remove_flag(g_units_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l = require_obj(lv_label_create(g_units_screen), "units title");
    lv_label_set_text(l, "UNITS");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 18);

    const char *names[] = {"Heading", "Depth", "Temperature", "Wind speed", "Boat speed", "Distance", "Short distance", "Lat / long"};
    lv_obj_t **vals[] = {&g_units_heading, &g_units_depth, &g_units_temp, &g_units_wind,
                         &g_units_vessel, &g_units_distance, &g_units_short, &g_units_latlon};
    lv_event_cb_t cbs[] = {unit_heading_cb, unit_depth_cb, unit_temp_cb, unit_wind_cb,
                           unit_vessel_cb, unit_distance_cb, unit_short_cb, unit_latlon_cb};

    for (int i = 0; i < 8; ++i) {
        l = require_obj(lv_label_create(g_units_screen), "unit row label");
        lv_label_set_text(l, names[i]);
        lv_obj_set_style_text_font(l,&lv_font_montserrat_14,0);
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 32, 58 + i * 38);

        lv_obj_t *b = make_button(g_units_screen, "", cbs[i], 170, 36);
        *vals[i] = button_label(b);
        lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -32, 48 + i * 38);
    }

    l = require_obj(lv_label_create(g_units_screen), "short-distance threshold label");
    lv_label_set_text(l, "Short if");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 32, 366);

    lv_obj_t *b = make_button(g_units_screen, "-", unit_threshold_down_cb, 48, 36);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, 145, 354);
    b = make_button(g_units_screen, "", unit_threshold_up_cb, 125, 36);
    g_units_threshold = button_label(b);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 60, 354);
    b = make_button(g_units_screen, "+", unit_threshold_up_cb, 48, 36);
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -32, 354);

    b = make_button(g_units_screen, "BACK", settings_screen_cb, 120, 46);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -12);
}

void create_wifi_screen()
{
    g_wifi_screen=require_obj(lv_obj_create(nullptr),"Wi-Fi screen");
    lv_obj_remove_flag(g_wifi_screen,LV_OBJ_FLAG_SCROLLABLE);
    auto *title=lv_label_create(g_wifi_screen);lv_label_set_text(title,"Wi-Fi");lv_obj_align(title,LV_ALIGN_TOP_MID,0,12);
    auto *form=lv_obj_create(g_wifi_screen);lv_obj_set_size(form,460,365);lv_obj_align(form,LV_ALIGN_TOP_MID,0,42);
    lv_obj_set_scroll_dir(form,LV_DIR_VER);lv_obj_set_style_pad_all(form,8,0);
    auto label=[form](const char *text,int x,int y){auto *l=lv_label_create(form);lv_label_set_text(l,text);lv_obj_set_pos(l,x,y);return l;};
    label("Mode",4,12);
    g_wifi_mode=lv_dropdown_create(form);lv_dropdown_set_options(g_wifi_mode,"Station\nAccess Point");lv_obj_set_pos(g_wifi_mode,132,0);lv_obj_set_width(g_wifi_mode,280);lv_obj_add_event_cb(g_wifi_mode,wifi_mode_cb,LV_EVENT_VALUE_CHANGED,nullptr);
    label("Enabled",4,64);g_wifi_enabled=lv_switch_create(form);lv_obj_set_pos(g_wifi_enabled,340,56);
    auto field=[form](int y,int max,bool password){
        auto *ta=lv_textarea_create(form);lv_obj_set_size(ta,280,44);lv_obj_set_pos(ta,132,y);
        lv_textarea_set_one_line(ta,true);lv_textarea_set_max_length(ta,max);lv_textarea_set_password_mode(ta,password);
        lv_textarea_set_password_show_time(ta,0);lv_obj_add_event_cb(ta,wifi_textarea_focus_cb,LV_EVENT_FOCUSED,nullptr);return ta;
    };
    label("SSID",4,112);g_wifi_ssid=field(98,32,false);
    label("Password",4,166);g_wifi_password=field(152,64,true);
    g_wifi_show=lv_checkbox_create(form);lv_checkbox_set_text(g_wifi_show,"Show password");lv_obj_set_pos(g_wifi_show,4,206);lv_obj_add_event_cb(g_wifi_show,wifi_show_cb,LV_EVENT_VALUE_CHANGED,nullptr);
    g_wifi_open=lv_checkbox_create(form);lv_checkbox_set_text(g_wifi_open,"Open network");lv_obj_set_pos(g_wifi_open,232,206);
    auto *b=make_button(form,"SCAN NETWORKS",wifi_scan_cb,196,44);lv_obj_set_pos(b,4,246);
    g_wifi_status=lv_label_create(form);lv_obj_set_width(g_wifi_status,410);lv_obj_set_pos(g_wifi_status,4,300);
    b=make_button(g_wifi_screen,"SAVE / CONNECT",wifi_save_cb,190,48);lv_obj_align(b,LV_ALIGN_BOTTOM_LEFT,24,-8);
    b=make_button(g_wifi_screen,"BACK",settings_screen_cb,120,48);lv_obj_align(b,LV_ALIGN_BOTTOM_RIGHT,-24,-8);
    g_wifi_keyboard=lv_keyboard_create(g_wifi_screen);lv_obj_set_size(g_wifi_keyboard,460,205);lv_obj_align(g_wifi_keyboard,LV_ALIGN_BOTTOM_MID,0,0);
    lv_obj_add_event_cb(g_wifi_keyboard,wifi_keyboard_cb,LV_EVENT_ALL,nullptr);lv_obj_add_flag(g_wifi_keyboard,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_wifi_screen,wifi_mask_cb,LV_EVENT_SCREEN_UNLOADED,nullptr);

    g_wifi_picker=lv_obj_create(nullptr);lv_obj_remove_flag(g_wifi_picker,LV_OBJ_FLAG_SCROLLABLE);
    g_wifi_scan_status=lv_label_create(g_wifi_picker);lv_obj_set_width(g_wifi_scan_status,420);lv_obj_align(g_wifi_scan_status,LV_ALIGN_TOP_MID,0,16);
    g_wifi_scan_list=lv_obj_create(g_wifi_picker);lv_obj_set_size(g_wifi_scan_list,440,320);lv_obj_align(g_wifi_scan_list,LV_ALIGN_TOP_MID,0,66);lv_obj_set_flex_flow(g_wifi_scan_list,LV_FLEX_FLOW_COLUMN);
    b=make_button(g_wifi_picker,"SCAN AGAIN",wifi_scan_again_cb,180,48);lv_obj_align(b,LV_ALIGN_BOTTOM_LEFT,24,-16);
    b=make_button(g_wifi_picker,"BACK",wifi_picker_back_cb,120,48);lv_obj_align(b,LV_ALIGN_BOTTOM_RIGHT,-24,-16);

}
void create_shunts_screen()
{
    g_shunts_screen = require_obj(lv_obj_create(nullptr), "SmartShunts screen");
    lv_obj_remove_flag(g_shunts_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l = require_obj(lv_label_create(g_shunts_screen), "SmartShunts title");
    lv_label_set_text(l, "SMARTSHUNTS");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 22);

    for (size_t i = 0; i < MAX_SMARTSHUNTS; ++i) {
        lv_obj_t *b = make_button(g_shunts_screen, "", shunt_slot_cb, 360, 62, nullptr);
        g_shunt_slot_labels[i] = button_label(b);
        lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 78 + static_cast<int>(i) * 72);
        lv_obj_remove_event_cb(b, shunt_slot_cb);
        lv_obj_add_event_cb(b, shunt_slot_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(i));
    }

    lv_obj_t *b = make_button(g_shunts_screen, "BACK", settings_screen_cb, 120, 46);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -12);
}
void create_shunt_picker_screen()
{
    g_shunt_picker_screen = lv_obj_create(nullptr);
    lv_obj_remove_flag(g_shunt_picker_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = lv_label_create(g_shunt_picker_screen);
    lv_label_set_text(l, "Select SmartShunt");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 16);

    for (size_t i = 0; i < MAX_DISCOVERED_SMARTSHUNTS; ++i) {
        lv_obj_t *b = make_button(g_shunt_picker_screen, "", shunt_picker_select_cb, 400, 42,
                                  reinterpret_cast<void *>(i));
        g_shunt_picker_buttons[i] = b;
        g_shunt_picker_labels[i] = button_label(b);
        lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 52 + static_cast<int>(i) * 45);
    }

    lv_obj_t *b = make_button(g_shunt_picker_screen, "BACK", shunt_picker_back_cb, 120, 44);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -10);
}
void create_shunt_edit_screen()
{
    g_shunt_edit_screen = require_obj(lv_obj_create(nullptr), "SmartShunt edit screen");
    lv_obj_remove_flag(g_shunt_edit_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l = require_obj(lv_label_create(g_shunt_edit_screen), "SmartShunt title");
    lv_label_set_text(l, "SMARTSHUNT");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_t *b = make_button(g_shunt_edit_screen, "SELECT NEARBY", choose_nearby_cb, 200, 44);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 52);

    l = require_obj(lv_label_create(g_shunt_edit_screen), "SmartShunt name label");
    lv_label_set_text(l, "Name");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 30, 112);
    g_shunt_name = require_obj(lv_textarea_create(g_shunt_edit_screen), "SmartShunt name field");
    lv_obj_set_style_bg_color(g_shunt_name, ui_card(), 0);
    lv_obj_set_style_text_color(g_shunt_name, ui_text(), 0);
    lv_obj_set_style_border_color(g_shunt_name, ui_border(), 0);
    lv_obj_set_style_radius(g_shunt_name, 8, 0);
    lv_obj_set_size(g_shunt_name, 300, 42);
    lv_textarea_set_one_line(g_shunt_name, true);
    lv_textarea_set_max_length(g_shunt_name, 24);
    lv_obj_align(g_shunt_name, LV_ALIGN_TOP_RIGHT, -30, 100);
    lv_obj_add_event_cb(g_shunt_name, textarea_focus_cb, LV_EVENT_FOCUSED, nullptr);

    l = require_obj(lv_label_create(g_shunt_edit_screen), "SmartShunt key label");
    lv_label_set_text(l, "Key");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 30, 167);
    g_shunt_key = require_obj(lv_textarea_create(g_shunt_edit_screen), "SmartShunt key field");
    lv_obj_set_style_bg_color(g_shunt_key, ui_card(), 0);
    lv_obj_set_style_text_color(g_shunt_key, ui_text(), 0);
    lv_obj_set_style_border_color(g_shunt_key, ui_border(), 0);
    lv_obj_set_style_radius(g_shunt_key, 8, 0);
    lv_obj_set_size(g_shunt_key, 360, 42);
    lv_textarea_set_one_line(g_shunt_key, true);
    lv_textarea_set_password_mode(g_shunt_key, false);
    lv_textarea_set_max_length(g_shunt_key, 32);
    lv_obj_align(g_shunt_key, LV_ALIGN_TOP_RIGHT, -20, 155);
    lv_obj_add_event_cb(g_shunt_key, textarea_focus_cb, LV_EVENT_FOCUSED, nullptr);

    l = require_obj(lv_label_create(g_shunt_edit_screen), "NMEA bridge label");
    lv_label_set_text(l, "Send to N2K");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 30, 220);
    g_shunt_n2k = require_obj(lv_switch_create(g_shunt_edit_screen), "NMEA bridge switch");
    lv_obj_align(g_shunt_n2k, LV_ALIGN_TOP_RIGHT, -45, 207);

    l = require_obj(lv_label_create(g_shunt_edit_screen), "battery instance label");
    lv_label_set_text(l, "Battery instance");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 30, 272);
    b = make_button(g_shunt_edit_screen, "-", shunt_instance_down_cb, 48, 38);
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -180, 258);
    g_shunt_instance = require_obj(lv_label_create(g_shunt_edit_screen), "battery instance value");
    lv_obj_set_style_text_font(g_shunt_instance, &lv_font_montserrat_20, 0);
    lv_obj_align(g_shunt_instance, LV_ALIGN_TOP_RIGHT, -112, 267);
    b = make_button(g_shunt_edit_screen, "+", shunt_instance_up_cb, 48, 38);
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -40, 258);

    b = make_button(g_shunt_edit_screen, "SAVE", shunt_save_cb, 120, 46);
    lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, 45, -18);
    b = make_button(g_shunt_edit_screen, "BACK", shunts_screen_cb, 120, 46);
    lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, -45, -18);

    g_keyboard = require_obj(lv_keyboard_create(g_shunt_edit_screen), "SmartShunt keyboard");
    lv_obj_set_size(g_keyboard, 460, 205);
    lv_obj_align(g_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(g_keyboard, keyboard_cb, LV_EVENT_ALL, nullptr);
    lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
}

} // namespace

void ui_start(const AppSettings &initial_settings)
{
    g_settings = initial_settings;
    g_boot_brightness_override=true;
    g_active_page = first_enabled_page();

    create_data_screen();
    create_source_depth_screens();
    create_input_screen();
    create_settings_screen();
    create_page_setup_screen();
    create_field_editor_screen();
    create_units_screen();
    create_wifi_screen();
    create_shunts_screen();
    create_shunt_picker_screen();
    create_shunt_edit_screen();

    apply_theme();
    render_active_page();
    update_units();
    update_page_setup();
    update_shunts_list();
    g_boot_screen = require_obj(lv_obj_create(nullptr), "boot screen");
    lv_obj_remove_flag(g_boot_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_boot_screen,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_boot_screen,boot_touch_cb,LV_EVENT_ALL,nullptr);
    apply_theme_to(g_boot_screen);
    lv_obj_t *identity = require_obj(lv_label_create(g_boot_screen), "boot firmware identity");
    const esp_app_desc_t *app = esp_app_get_description();
    lv_label_set_text_fmt(identity, "ESP32 N2K TOUCH\nFirmware: %s\nBuilt: %s %s",
                          app->version, app->date, app->time);
    lv_obj_set_width(identity, 440);
    lv_obj_set_style_text_font(identity, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(identity, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(identity, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_t *hints = lv_label_create(g_boot_screen);
    lv_label_set_text(hints, "Swipe left / right: change page\nSwipe down: edit current page\nSwipe up: open Settings");
    lv_obj_set_style_text_font(hints, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(hints, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hints, LV_ALIGN_TOP_MID, 0, 112);
    lv_obj_t *recovery=require_obj(lv_label_create(g_boot_screen),"brightness recovery hint");
    lv_label_set_text(recovery,"TOUCH SCREEN FOR 3 SECONDS\nTO RESTORE BRIGHTNESS");
    lv_obj_set_width(recovery,440);
    lv_obj_set_style_text_font(recovery,&lv_font_montserrat_20,0);
    lv_obj_set_style_text_align(recovery,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_align(recovery,LV_ALIGN_TOP_MID,0,215);
    g_boot_recovery_status=require_obj(lv_label_create(g_boot_screen),"brightness recovery status");
    lv_label_set_text(g_boot_recovery_status,"");lv_obj_set_width(g_boot_recovery_status,440);
    lv_obj_set_style_text_font(g_boot_recovery_status,&lv_font_montserrat_14,0);
    lv_obj_set_style_text_align(g_boot_recovery_status,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_align(g_boot_recovery_status,LV_ALIGN_TOP_MID,0,275);
    g_boot_status = lv_label_create(g_boot_screen); lv_obj_set_width(g_boot_status, 440);
    lv_obj_set_style_text_align(g_boot_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_boot_status,&lv_font_montserrat_14,0);
    lv_obj_set_height(g_boot_status,100);lv_label_set_long_mode(g_boot_status,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(g_boot_status, LV_ALIGN_TOP_MID, 0, 335);
    // Decorative labels must not intercept a hold anywhere on the boot screen.
    for(uint32_t i=0;i<lv_obj_get_child_count(g_boot_screen);++i)
        lv_obj_remove_flag(lv_obj_get_child(g_boot_screen,i),LV_OBJ_FLAG_CLICKABLE);
    update_input_status();
    lv_screen_load(g_boot_screen);
    // This callback runs on the LVGL task, retaining display/touch ownership.
    g_boot_recovery.begin(lv_tick_get());
    lv_timer_t *boot_timer = lv_timer_create(boot_timer_cb,50,nullptr);
    if (!boot_timer) std::abort();
    g_refresh_timer = lv_timer_create(refresh_cb, 500, nullptr);
    if (g_refresh_timer == nullptr) {
        ESP_LOGE(TAG, "LVGL allocation failed for refresh timer");
        std::abort();
    }
}

bool ui_is_healthy() {
    const uint32_t now=static_cast<uint32_t>(esp_timer_get_time()/1000);
    return g_ui_refreshes.load()>=5 && now-g_ui_last_refresh_ms.load()<1000;
}
