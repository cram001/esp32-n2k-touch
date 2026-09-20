#include "ui.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "bsp/display.h"
#include "esp_err.h"
#include "esp_log.h"
#include "instrument_data.hpp"
#include "lvgl.h"
#include "n2k_bridge.hpp"
#include "smartshunt_ble.hpp"
#include "wifi_service.hpp"

namespace {
constexpr const char *TAG = "ui";

AppSettings g_settings;
size_t g_active_page = 0;
size_t g_edit_page = 0;
size_t g_edit_field = 0;
size_t g_edit_shunt = 0;

lv_obj_t *g_data_screen = nullptr;
lv_obj_t *g_settings_screen = nullptr;
lv_obj_t *g_page_setup_screen = nullptr;
lv_obj_t *g_field_editor_screen = nullptr;
lv_obj_t *g_units_screen = nullptr;
lv_obj_t *g_shunts_screen = nullptr;
lv_obj_t *g_shunt_edit_screen = nullptr;
lv_obj_t *g_wifi_screen = nullptr;
lv_obj_t *g_keyboard = nullptr;
lv_obj_t *g_wifi_keyboard = nullptr;
lv_timer_t *g_refresh_timer = nullptr;

lv_obj_t *g_page_title = nullptr;
std::array<lv_obj_t *, MAX_DATA_FIELDS_PER_PAGE> g_tile_boxes{};
std::array<lv_obj_t *, MAX_DATA_FIELDS_PER_PAGE> g_tile_titles{};
std::array<lv_obj_t *, MAX_DATA_FIELDS_PER_PAGE> g_tile_values{};
std::array<lv_obj_t *, MAX_DATA_FIELDS_PER_PAGE> g_tile_units{};
std::array<lv_obj_t *, MAX_DATA_FIELDS_PER_PAGE> g_tile_sources{};
std::array<lv_obj_t *, MAX_DATA_PAGES> g_page_dots{};

lv_obj_t *g_page_setup_title = nullptr;
lv_obj_t *g_page_enable_switch = nullptr;
lv_obj_t *g_layout_button_label = nullptr;
std::array<lv_obj_t *, MAX_DATA_FIELDS_PER_PAGE> g_field_buttons{};
std::array<lv_obj_t *, MAX_DATA_FIELDS_PER_PAGE> g_field_button_labels{};

lv_obj_t *g_field_source_label = nullptr;
lv_obj_t *g_field_metric_label = nullptr;
lv_obj_t *g_field_device_label = nullptr;
lv_obj_t *g_field_device_button = nullptr;

lv_obj_t *g_units_depth = nullptr;
lv_obj_t *g_units_temp = nullptr;
lv_obj_t *g_units_wind = nullptr;
lv_obj_t *g_units_vessel = nullptr;
lv_obj_t *g_units_distance = nullptr;
lv_obj_t *g_units_short = nullptr;
lv_obj_t *g_units_threshold = nullptr;

std::array<lv_obj_t *, MAX_SMARTSHUNTS> g_shunt_slot_labels{};
lv_obj_t *g_shunt_name = nullptr;
lv_obj_t *g_shunt_key = nullptr;
lv_obj_t *g_shunt_n2k = nullptr;
lv_obj_t *g_shunt_instance = nullptr;

lv_obj_t *g_wifi_enabled = nullptr;
lv_obj_t *g_wifi_ssid = nullptr;
lv_obj_t *g_wifi_password = nullptr;
lv_obj_t *g_wifi_status = nullptr;

const std::array<DataMetric, 14> NMEA_METRICS = {
    DataMetric::None, DataMetric::Depth, DataMetric::BoatSpeed, DataMetric::SpeedOverGround,
    DataMetric::CourseOverGround, DataMetric::Heading, DataMetric::ApparentWindSpeed,
    DataMetric::ApparentWindAngle, DataMetric::TrueWindSpeed, DataMetric::TrueWindAngle,
    DataMetric::WaterTemperature, DataMetric::AirTemperature, DataMetric::DistanceToWaypoint,
    DataMetric::TripDistance
};
const std::array<DataMetric, 6> SHUNT_METRICS = {
    DataMetric::BatteryVoltage, DataMetric::BatteryCurrent, DataMetric::BatterySoc,
    DataMetric::BatteryConsumedAh, DataMetric::BatteryTimeToGo, DataMetric::BatteryTemperature
};

uint8_t active_brightness()
{
    return g_settings.theme == DisplayTheme::Day ? g_settings.day_brightness : g_settings.night_brightness;
}

size_t layout_count(PageLayout layout) { return static_cast<size_t>(layout); }

lv_color_t ui_bg() { return lv_color_hex(0x081014); }
lv_color_t ui_card() { return lv_color_hex(0x0D171C); }
lv_color_t ui_card_alt() { return lv_color_hex(0x101D23); }
lv_color_t ui_border() { return lv_color_hex(0x29414A); }
lv_color_t ui_text() { return lv_color_hex(0xF4F7F8); }
lv_color_t ui_muted() { return lv_color_hex(0x93A4AA); }
lv_color_t ui_accent() { return lv_color_hex(0x2EB7F3); }
lv_color_t ui_good() { return lv_color_hex(0x43DB75); }

void style_button(lv_obj_t *button)
{
    lv_obj_set_style_bg_color(button, ui_card_alt(), 0);
    lv_obj_set_style_border_color(button, ui_border(), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_text_color(button, ui_text(), 0);
}

void style_card(lv_obj_t *card)
{
    lv_obj_set_style_bg_color(card, ui_card(), 0);
    lv_obj_set_style_border_color(card, ui_border(), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_text_color(card, ui_text(), 0);
}

void apply_backlight()
{
    const esp_err_t err = bsp_display_brightness_set(active_brightness());
    if (err != ESP_OK) ESP_LOGW(TAG, "Unable to set brightness: %s", esp_err_to_name(err));
}

void apply_theme_to(lv_obj_t *screen)
{
    if (!screen) return;
    if (g_settings.theme == DisplayTheme::Night) {
        lv_obj_set_style_bg_color(screen, ui_bg(), 0);
        lv_obj_set_style_text_color(screen, ui_text(), 0);
    } else {
        lv_obj_set_style_bg_color(screen, lv_color_hex(0xF4F7F8), 0);
        lv_obj_set_style_text_color(screen, lv_color_hex(0x102027), 0);
    }
}

void apply_theme()
{
    apply_theme_to(g_data_screen); apply_theme_to(g_settings_screen); apply_theme_to(g_page_setup_screen);
    apply_theme_to(g_field_editor_screen); apply_theme_to(g_units_screen); apply_theme_to(g_shunts_screen);
    apply_theme_to(g_shunt_edit_screen); apply_theme_to(g_wifi_screen); apply_backlight();
}

void apply_runtime_settings()
{
    wifi_service_apply_config(g_settings.wifi);
    smartshunt_ble_apply_settings(g_settings);
    n2k_bridge_apply_settings(g_settings);
}

void persist()
{
    if (!settings_save(g_settings)) ESP_LOGW(TAG, "Could not persist settings");
    apply_runtime_settings(); apply_theme();
}

lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, int w=120, int h=48, void *user=nullptr)
{
    lv_obj_t *b = lv_button_create(parent); lv_obj_set_size(b,w,h); lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,user); style_button(b);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l,text); lv_obj_set_style_text_font(l,&lv_font_montserrat_14,0); lv_obj_center(l); return b;
}

lv_obj_t *button_label(lv_obj_t *button) { return lv_obj_get_child(button,0); }

size_t first_enabled_page()
{
    for(size_t i=0;i<g_settings.pages.size();++i) if(g_settings.pages[i].enabled) return i; return 0;
}

size_t next_enabled_page(size_t from,int direction)
{
    for(size_t step=1;step<=MAX_DATA_PAGES;++step){
        const int raw=static_cast<int>(from)+direction*static_cast<int>(step);
        const size_t idx=static_cast<size_t>((raw%static_cast<int>(MAX_DATA_PAGES)+static_cast<int>(MAX_DATA_PAGES))%static_cast<int>(MAX_DATA_PAGES));
        if(g_settings.pages[idx].enabled) return idx;
    }
    return from;
}

void position_tile(size_t index, PageLayout layout)
{
    lv_obj_t *box=g_tile_boxes[index]; if(!box) return;
    int x=10,y=48,w=460,h=326; const size_t count=layout_count(layout);
    if(count==2){x=10;w=460;h=155;y=48+static_cast<int>(index)*161;}
    else if(count==4){w=226;h=155;x=10+static_cast<int>(index%2)*234;y=48+static_cast<int>(index/2)*161;}
    else if(count==6){w=226;h=101;x=10+static_cast<int>(index%2)*234;y=48+static_cast<int>(index/2)*107;}
    lv_obj_set_pos(box,x,y);lv_obj_set_size(box,w,h);lv_obj_set_style_pad_all(box,8,0);style_card(box);
    const lv_font_t *vf=count==1?&lv_font_montserrat_48:(count<=4?&lv_font_montserrat_32:&lv_font_montserrat_24);
    lv_obj_set_style_text_font(g_tile_values[index],vf,0);
    lv_obj_set_style_text_color(g_tile_titles[index], ui_muted(), 0);
    lv_obj_set_style_text_color(g_tile_values[index], ui_text(), 0);
    lv_obj_set_style_text_color(g_tile_units[index], ui_muted(), 0);
    lv_obj_set_style_text_color(g_tile_sources[index], ui_muted(), 0);
    lv_obj_align(g_tile_titles[index],LV_ALIGN_TOP_LEFT,2,0);
    lv_obj_align(g_tile_values[index],LV_ALIGN_CENTER,-10,count==6?2:5);
    lv_obj_align_to(g_tile_units[index],g_tile_values[index],LV_ALIGN_OUT_RIGHT_MID,6,0);
    lv_obj_align(g_tile_sources[index],LV_ALIGN_BOTTOM_RIGHT,-2,0);
}

void render_active_page()
{
    if(!g_settings.pages[g_active_page].enabled) g_active_page=first_enabled_page();
    auto &active=g_settings.pages[g_active_page];
    char title[40];std::snprintf(title,sizeof(title),"%s   %u/%u",active.name.data(),static_cast<unsigned>(g_active_page+1),static_cast<unsigned>(MAX_DATA_PAGES));
    lv_label_set_text(g_page_title,title);
    lv_obj_set_style_text_color(g_page_title, ui_text(), 0);
    for(size_t i=0;i<MAX_DATA_PAGES;++i){
        if(!g_page_dots[i]) continue;
        lv_obj_set_style_bg_color(g_page_dots[i], i==g_active_page ? ui_accent() : ui_muted(), 0);
        lv_obj_set_style_bg_opa(g_page_dots[i], g_settings.pages[i].enabled ? LV_OPA_COVER : LV_OPA_30, 0);
    }
    const size_t count=layout_count(active.layout);
    for(size_t i=0;i<MAX_DATA_FIELDS_PER_PAGE;++i){
        if(i>=count){lv_obj_add_flag(g_tile_boxes[i],LV_OBJ_FLAG_HIDDEN);continue;}
        lv_obj_remove_flag(g_tile_boxes[i],LV_OBJ_FLAG_HIDDEN);position_tile(i,active.layout);
        const auto &sel=active.fields[i];lv_label_set_text(g_tile_titles[i],instrument_metric_name(sel.metric));lv_label_set_text(g_tile_sources[i],instrument_source_name(sel,g_settings));
        const InstrumentValue v=instrument_data_get(sel);char value[32],unit[12];instrument_format_value(sel,g_settings.units,v,value,sizeof(value),unit,sizeof(unit));
        lv_label_set_text(g_tile_values[i],value);lv_label_set_text(g_tile_units[i],unit);
    }
}

void update_wifi_status()
{
    if(!g_wifi_status) return;
    const WifiStatus st=wifi_service_get_status();
    char b[96]{};
    switch(st.state){
    case WifiState::Disabled: std::snprintf(b,sizeof(b),"Disabled"); break;
    case WifiState::Connecting: std::snprintf(b,sizeof(b),"Connecting to %s...",st.ssid.data()); break;
    case WifiState::Connected: std::snprintf(b,sizeof(b),"%s\n%s   %d dBm",st.ssid.data(),st.ip.data(),st.rssi); break;
    case WifiState::Disconnected: std::snprintf(b,sizeof(b),"Disconnected - reconnecting"); break;
    case WifiState::Error: std::snprintf(b,sizeof(b),"Wi-Fi error"); break;
    }
    lv_label_set_text(g_wifi_status,b);
}
void refresh_cb(lv_timer_t *){render_active_page();update_wifi_status();}
void previous_page_cb(lv_event_t *){g_active_page=next_enabled_page(g_active_page,-1);render_active_page();}
void next_page_cb(lv_event_t *){g_active_page=next_enabled_page(g_active_page,+1);render_active_page();}
void data_screen_cb(lv_event_t *){render_active_page();lv_screen_load(g_data_screen);}
void settings_screen_cb(lv_event_t *){lv_screen_load(g_settings_screen);}
void theme_toggle_cb(lv_event_t *){g_settings.theme=g_settings.theme==DisplayTheme::Day?DisplayTheme::Night:DisplayTheme::Day;persist();}
void brightness_down_cb(lv_event_t *){uint8_t &v=g_settings.theme==DisplayTheme::Day?g_settings.day_brightness:g_settings.night_brightness;v=v>10?static_cast<uint8_t>(v-10):1;persist();}
void brightness_up_cb(lv_event_t *){uint8_t &v=g_settings.theme==DisplayTheme::Day?g_settings.day_brightness:g_settings.night_brightness;v=v<91?static_cast<uint8_t>(v+10):100;persist();}

void update_page_setup()
{
    auto &p=g_settings.pages[g_edit_page];char b[48];std::snprintf(b,sizeof(b),"PAGE %u - %s",static_cast<unsigned>(g_edit_page+1),p.name.data());lv_label_set_text(g_page_setup_title,b);
    if(p.enabled)lv_obj_add_state(g_page_enable_switch,LV_STATE_CHECKED);else lv_obj_remove_state(g_page_enable_switch,LV_STATE_CHECKED);
    std::snprintf(b,sizeof(b),"LAYOUT: %u",static_cast<unsigned>(layout_count(p.layout)));lv_label_set_text(g_layout_button_label,b);
    const size_t count=layout_count(p.layout);
    for(size_t i=0;i<MAX_DATA_FIELDS_PER_PAGE;++i){if(i>=count){lv_obj_add_flag(g_field_buttons[i],LV_OBJ_FLAG_HIDDEN);continue;}lv_obj_remove_flag(g_field_buttons[i],LV_OBJ_FLAG_HIDDEN);const auto &f=p.fields[i];std::snprintf(b,sizeof(b),"%u. %s\n%s",static_cast<unsigned>(i+1),instrument_metric_name(f.metric),instrument_source_name(f,g_settings));lv_label_set_text(g_field_button_labels[i],b);}
}
void page_setup_screen_cb(lv_event_t *){update_page_setup();lv_screen_load(g_page_setup_screen);}
void edit_prev_page_cb(lv_event_t *){g_edit_page=(g_edit_page+MAX_DATA_PAGES-1)%MAX_DATA_PAGES;update_page_setup();}
void edit_next_page_cb(lv_event_t *){g_edit_page=(g_edit_page+1)%MAX_DATA_PAGES;update_page_setup();}
void page_enabled_cb(lv_event_t *e){g_settings.pages[g_edit_page].enabled=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED);bool any=false;for(const auto&p:g_settings.pages)any|=p.enabled;if(!any){g_settings.pages[g_edit_page].enabled=true;lv_obj_add_state(g_page_enable_switch,LV_STATE_CHECKED);}persist();update_page_setup();}
void layout_cb(lv_event_t *){auto &l=g_settings.pages[g_edit_page].layout;if(l==PageLayout::One)l=PageLayout::Two;else if(l==PageLayout::Two)l=PageLayout::Four;else if(l==PageLayout::Four)l=PageLayout::Six;else l=PageLayout::One;persist();update_page_setup();}

void update_field_editor()
{
    auto &f=g_settings.pages[g_edit_page].fields[g_edit_field];lv_label_set_text(g_field_source_label,f.source==DataSourceType::Nmea2000?"NMEA 2000":"SMARTSHUNT");lv_label_set_text(g_field_metric_label,instrument_metric_name(f.metric));
    if(f.source==DataSourceType::SmartShunt){lv_obj_remove_flag(g_field_device_button,LV_OBJ_FLAG_HIDDEN);lv_label_set_text(g_field_device_label,instrument_source_name(f,g_settings));}else lv_obj_add_flag(g_field_device_button,LV_OBJ_FLAG_HIDDEN);
}
void open_field_editor_cb(lv_event_t *e){g_edit_field=static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));update_field_editor();lv_screen_load(g_field_editor_screen);}
void field_source_cb(lv_event_t *){auto &f=g_settings.pages[g_edit_page].fields[g_edit_field];f.source=f.source==DataSourceType::Nmea2000?DataSourceType::SmartShunt:DataSourceType::Nmea2000;f.metric=f.source==DataSourceType::Nmea2000?DataMetric::Depth:DataMetric::BatteryVoltage;persist();update_field_editor();}
void field_metric_step(int direction){auto &f=g_settings.pages[g_edit_page].fields[g_edit_field];if(f.source==DataSourceType::Nmea2000){int pos=0;for(size_t i=0;i<NMEA_METRICS.size();++i)if(NMEA_METRICS[i]==f.metric){pos=static_cast<int>(i);break;}const int n=static_cast<int>(NMEA_METRICS.size());pos=(pos+direction+n)%n;f.metric=NMEA_METRICS[static_cast<size_t>(pos)];}else{int pos=0;for(size_t i=0;i<SHUNT_METRICS.size();++i)if(SHUNT_METRICS[i]==f.metric){pos=static_cast<int>(i);break;}const int n=static_cast<int>(SHUNT_METRICS.size());pos=(pos+direction+n)%n;f.metric=SHUNT_METRICS[static_cast<size_t>(pos)];}persist();update_field_editor();}
void field_metric_prev_cb(lv_event_t *){field_metric_step(-1);}void field_metric_next_cb(lv_event_t *){field_metric_step(1);}
void field_device_cb(lv_event_t *){auto &f=g_settings.pages[g_edit_page].fields[g_edit_field];for(size_t step=1;step<=MAX_SMARTSHUNTS;++step){size_t idx=(f.source_index+step)%MAX_SMARTSHUNTS;if(g_settings.smartshunts[idx].configured){f.source_index=idx;break;}}persist();update_field_editor();}
void field_done_cb(lv_event_t *){update_page_setup();lv_screen_load(g_page_setup_screen);}

const char *depth_name(){return g_settings.units.depth==DepthUnit::Metres?"METRES":"FEET";}const char *temp_name(){return g_settings.units.temperature==TemperatureUnit::Celsius?"CELSIUS":"FAHRENHEIT";}const char *speed_name(SpeedUnit u){return u==SpeedUnit::Knots?"KNOTS":(u==SpeedUnit::KilometresPerHour?"KM/H":"M/S");}const char *distance_name(){return g_settings.units.distance==DistanceUnit::NauticalMiles?"NM":"KM";}const char *short_name(){return g_settings.units.short_distance==ShortDistanceUnit::Metres?"METRES":(g_settings.units.short_distance==ShortDistanceUnit::Feet?"FEET":"YARDS");}
void update_units(){lv_label_set_text(g_units_depth,depth_name());lv_label_set_text(g_units_temp,temp_name());lv_label_set_text(g_units_wind,speed_name(g_settings.units.wind_speed));lv_label_set_text(g_units_vessel,speed_name(g_settings.units.vessel_speed));lv_label_set_text(g_units_distance,distance_name());lv_label_set_text(g_units_short,short_name());char b[24];std::snprintf(b,sizeof(b),"< %.2f NM",g_settings.units.short_distance_threshold_nm);lv_label_set_text(g_units_threshold,b);}
void units_screen_cb(lv_event_t *){update_units();lv_screen_load(g_units_screen);}void unit_depth_cb(lv_event_t *){g_settings.units.depth=g_settings.units.depth==DepthUnit::Metres?DepthUnit::Feet:DepthUnit::Metres;persist();update_units();}void unit_temp_cb(lv_event_t *){g_settings.units.temperature=g_settings.units.temperature==TemperatureUnit::Celsius?TemperatureUnit::Fahrenheit:TemperatureUnit::Celsius;persist();update_units();}
SpeedUnit next_speed(SpeedUnit u){return u==SpeedUnit::Knots?SpeedUnit::KilometresPerHour:(u==SpeedUnit::KilometresPerHour?SpeedUnit::MetresPerSecond:SpeedUnit::Knots);}void unit_wind_cb(lv_event_t *){g_settings.units.wind_speed=next_speed(g_settings.units.wind_speed);persist();update_units();}void unit_vessel_cb(lv_event_t *){g_settings.units.vessel_speed=next_speed(g_settings.units.vessel_speed);persist();update_units();}void unit_distance_cb(lv_event_t *){g_settings.units.distance=g_settings.units.distance==DistanceUnit::NauticalMiles?DistanceUnit::Kilometres:DistanceUnit::NauticalMiles;persist();update_units();}void unit_short_cb(lv_event_t *){auto&u=g_settings.units.short_distance;u=u==ShortDistanceUnit::Metres?ShortDistanceUnit::Feet:(u==ShortDistanceUnit::Feet?ShortDistanceUnit::Yards:ShortDistanceUnit::Metres);persist();update_units();}void unit_threshold_down_cb(lv_event_t *){auto&t=g_settings.units.short_distance_threshold_nm;if(t>0.05f)t-=0.05f;persist();update_units();}void unit_threshold_up_cb(lv_event_t *){auto&t=g_settings.units.short_distance_threshold_nm;if(t<1.0f)t+=0.05f;persist();update_units();}

void wifi_keyboard_cb(lv_event_t *e){
    const auto code=lv_event_get_code(e);
    if(code==LV_EVENT_READY||code==LV_EVENT_CANCEL){
        lv_obj_add_flag(g_wifi_keyboard,LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(g_wifi_keyboard,nullptr);
    }
}
void wifi_textarea_focus_cb(lv_event_t *e){
    lv_keyboard_set_textarea(g_wifi_keyboard,lv_event_get_target_obj(e));
    lv_obj_remove_flag(g_wifi_keyboard,LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_wifi_keyboard);
}
void wifi_screen_cb(lv_event_t *){
    if(g_settings.wifi.enabled) lv_obj_add_state(g_wifi_enabled,LV_STATE_CHECKED); else lv_obj_remove_state(g_wifi_enabled,LV_STATE_CHECKED);
    lv_textarea_set_text(g_wifi_ssid,g_settings.wifi.ssid.data());
    lv_textarea_set_text(g_wifi_password,g_settings.wifi.password.data());
    update_wifi_status();
    lv_screen_load(g_wifi_screen);
}
void wifi_save_cb(lv_event_t *){
    g_settings.wifi.enabled=lv_obj_has_state(g_wifi_enabled,LV_STATE_CHECKED);
    std::snprintf(g_settings.wifi.ssid.data(),g_settings.wifi.ssid.size(),"%s",lv_textarea_get_text(g_wifi_ssid));
    std::snprintf(g_settings.wifi.password.data(),g_settings.wifi.password.size(),"%s",lv_textarea_get_text(g_wifi_password));
    persist();
    update_wifi_status();
}
void update_shunts_list(){for(size_t i=0;i<MAX_SMARTSHUNTS;++i){char b[48];const auto&c=g_settings.smartshunts[i];std::snprintf(b,sizeof(b),"%u. %s",static_cast<unsigned>(i+1),c.configured?(c.name[0]?c.name.data():"SmartShunt"):"ADD SMARTSHUNT");lv_label_set_text(g_shunt_slot_labels[i],b);}}
void shunts_screen_cb(lv_event_t *){update_shunts_list();lv_screen_load(g_shunts_screen);}void shunt_slot_cb(lv_event_t *e){g_edit_shunt=static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));auto &c=g_settings.smartshunts[g_edit_shunt];lv_textarea_set_text(g_shunt_name,c.name.data());lv_textarea_set_text(g_shunt_key,c.bindkey.data());if(c.n2k_enabled)lv_obj_add_state(g_shunt_n2k,LV_STATE_CHECKED);else lv_obj_remove_state(g_shunt_n2k,LV_STATE_CHECKED);char b[12];std::snprintf(b,sizeof(b),"%u",c.battery_instance);lv_label_set_text(g_shunt_instance,b);lv_screen_load(g_shunt_edit_screen);}
void choose_nearby_cb(lv_event_t *){std::array<DiscoveredSmartShunt,MAX_DISCOVERED_SMARTSHUNTS> found{};const size_t n=smartshunt_ble_get_discovered(found);if(n==0)return;auto&c=g_settings.smartshunts[g_edit_shunt];size_t pick=0;if(c.configured){for(size_t i=0;i<n;++i)if(std::strcmp(found[i].mac.data(),c.mac.data())==0){pick=(i+1)%n;break;}}c.configured=true;c.enabled=true;std::snprintf(c.mac.data(),c.mac.size(),"%s",found[pick].mac.data());std::snprintf(c.name.data(),c.name.size(),"%s",found[pick].name.data());lv_textarea_set_text(g_shunt_name,c.name.data());persist();}
void shunt_instance_down_cb(lv_event_t *){auto&c=g_settings.smartshunts[g_edit_shunt];if(c.battery_instance>0)--c.battery_instance;char b[12];std::snprintf(b,sizeof(b),"%u",c.battery_instance);lv_label_set_text(g_shunt_instance,b);persist();}void shunt_instance_up_cb(lv_event_t *){auto&c=g_settings.smartshunts[g_edit_shunt];if(c.battery_instance<252)++c.battery_instance;char b[12];std::snprintf(b,sizeof(b),"%u",c.battery_instance);lv_label_set_text(g_shunt_instance,b);persist();}
void shunt_save_cb(lv_event_t *){auto&c=g_settings.smartshunts[g_edit_shunt];std::snprintf(c.name.data(),c.name.size(),"%s",lv_textarea_get_text(g_shunt_name));std::snprintf(c.bindkey.data(),c.bindkey.size(),"%s",lv_textarea_get_text(g_shunt_key));c.n2k_enabled=lv_obj_has_state(g_shunt_n2k,LV_STATE_CHECKED);persist();update_shunts_list();lv_screen_load(g_shunts_screen);}void keyboard_cb(lv_event_t *e){const auto code=lv_event_get_code(e);if(code==LV_EVENT_READY||code==LV_EVENT_CANCEL){lv_obj_add_flag(g_keyboard,LV_OBJ_FLAG_HIDDEN);lv_keyboard_set_textarea(g_keyboard,nullptr);}}void textarea_focus_cb(lv_event_t *e){lv_keyboard_set_textarea(g_keyboard,lv_event_get_target_obj(e));lv_obj_remove_flag(g_keyboard,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(g_keyboard);}

void create_data_screen()
{
    g_data_screen=lv_obj_create(nullptr);
    lv_obj_remove_flag(g_data_screen,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(g_data_screen,ui_bg(),0);
    lv_obj_set_style_pad_all(g_data_screen,0,0);

    g_page_title=lv_label_create(g_data_screen);
    lv_obj_set_style_text_font(g_page_title,&lv_font_montserrat_20,0);
    lv_obj_set_style_text_color(g_page_title,ui_text(),0);
    lv_obj_align(g_page_title,LV_ALIGN_TOP_MID,0,12);

    for(size_t i=0;i<MAX_DATA_FIELDS_PER_PAGE;++i){
        g_tile_boxes[i]=lv_obj_create(g_data_screen);
        lv_obj_remove_flag(g_tile_boxes[i],LV_OBJ_FLAG_SCROLLABLE);
        style_card(g_tile_boxes[i]);

        g_tile_titles[i]=lv_label_create(g_tile_boxes[i]);
        lv_obj_set_style_text_font(g_tile_titles[i],&lv_font_montserrat_14,0);
        g_tile_values[i]=lv_label_create(g_tile_boxes[i]);
        g_tile_units[i]=lv_label_create(g_tile_boxes[i]);
        lv_obj_set_style_text_font(g_tile_units[i],&lv_font_montserrat_14,0);
        g_tile_sources[i]=lv_label_create(g_tile_boxes[i]);
        lv_obj_set_style_text_font(g_tile_sources[i],&lv_font_montserrat_14,0);
    }

    lv_obj_t *b=make_button(g_data_screen,"<",previous_page_cb,58,40);
    lv_obj_align(b,LV_ALIGN_BOTTOM_LEFT,12,-9);
    b=make_button(g_data_screen,"SETUP",settings_screen_cb,96,40);
    lv_obj_align(b,LV_ALIGN_BOTTOM_MID,0,-9);
    b=make_button(g_data_screen,">",next_page_cb,58,40);
    lv_obj_align(b,LV_ALIGN_BOTTOM_RIGHT,-12,-9);

    for(size_t i=0;i<MAX_DATA_PAGES;++i){
        g_page_dots[i]=lv_obj_create(g_data_screen);
        lv_obj_remove_flag(g_page_dots[i],LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(g_page_dots[i],8,8);
        lv_obj_set_style_radius(g_page_dots[i],LV_RADIUS_CIRCLE,0);
        lv_obj_set_style_border_width(g_page_dots[i],0,0);
        lv_obj_set_style_pad_all(g_page_dots[i],0,0);
        lv_obj_set_pos(g_page_dots[i],216+static_cast<int>(i)*14,421);
    }
}
void create_settings_screen()
{
    g_settings_screen=lv_obj_create(nullptr);
    lv_obj_remove_flag(g_settings_screen,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t*l=lv_label_create(g_settings_screen);
    lv_label_set_text(l,"Settings");
    lv_obj_set_style_text_font(l,&lv_font_montserrat_24,0);
    lv_obj_align(l,LV_ALIGN_TOP_MID,0,18);

    lv_obj_t*b=make_button(g_settings_screen,"Pages",page_setup_screen_cb,260,48);
    lv_obj_align(b,LV_ALIGN_TOP_MID,0,62);
    b=make_button(g_settings_screen,"UNITS",units_screen_cb,260,48);
    lv_obj_align(b,LV_ALIGN_TOP_MID,0,116);
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

    b=make_button(g_settings_screen,"BACK",data_screen_cb,120,46);
    lv_obj_align(b,LV_ALIGN_BOTTOM_MID,0,-18);
}
void create_page_setup_screen(){g_page_setup_screen=lv_obj_create(nullptr);lv_obj_remove_flag(g_page_setup_screen,LV_OBJ_FLAG_SCROLLABLE);g_page_setup_title=lv_label_create(g_page_setup_screen);lv_obj_set_style_text_font(g_page_setup_title,&lv_font_montserrat_20,0);lv_obj_align(g_page_setup_title,LV_ALIGN_TOP_MID,0,18);lv_obj_t*b=make_button(g_page_setup_screen,"< PAGE",edit_prev_page_cb,90,42);lv_obj_align(b,LV_ALIGN_TOP_LEFT,20,52);b=make_button(g_page_setup_screen,"PAGE >",edit_next_page_cb,90,42);lv_obj_align(b,LV_ALIGN_TOP_RIGHT,-20,52);lv_obj_t*l=lv_label_create(g_page_setup_screen);lv_label_set_text(l,"Enabled");lv_obj_align(l,LV_ALIGN_TOP_LEFT,140,64);g_page_enable_switch=lv_switch_create(g_page_setup_screen);lv_obj_align(g_page_enable_switch,LV_ALIGN_TOP_RIGHT,-120,54);lv_obj_add_event_cb(g_page_enable_switch,page_enabled_cb,LV_EVENT_VALUE_CHANGED,nullptr);b=make_button(g_page_setup_screen,"",layout_cb,180,44);g_layout_button_label=button_label(b);lv_obj_align(b,LV_ALIGN_TOP_MID,0,106);for(size_t i=0;i<MAX_DATA_FIELDS_PER_PAGE;++i){b=make_button(g_page_setup_screen,"",open_field_editor_cb,216,74,nullptr);g_field_buttons[i]=b;g_field_button_labels[i]=button_label(b);lv_label_set_long_mode(g_field_button_labels[i],LV_LABEL_LONG_WRAP);lv_obj_set_width(g_field_button_labels[i],196);lv_obj_set_style_text_align(g_field_button_labels[i],LV_TEXT_ALIGN_CENTER,0);lv_obj_set_pos(b,16+static_cast<int>(i%2)*232,164+static_cast<int>(i/2)*82);lv_obj_remove_event_cb(b,open_field_editor_cb);lv_obj_add_event_cb(b,open_field_editor_cb,LV_EVENT_CLICKED,reinterpret_cast<void*>(i));}b=make_button(g_page_setup_screen,"BACK",settings_screen_cb,120,46);lv_obj_align(b,LV_ALIGN_BOTTOM_MID,0,-12);}
void create_field_editor_screen(){g_field_editor_screen=lv_obj_create(nullptr);lv_obj_remove_flag(g_field_editor_screen,LV_OBJ_FLAG_SCROLLABLE);lv_obj_t*l=lv_label_create(g_field_editor_screen);lv_label_set_text(l,"DATA FIELD");lv_obj_set_style_text_font(l,&lv_font_montserrat_24,0);lv_obj_align(l,LV_ALIGN_TOP_MID,0,24);l=lv_label_create(g_field_editor_screen);lv_label_set_text(l,"Source");lv_obj_align(l,LV_ALIGN_TOP_LEFT,40,95);lv_obj_t*b=make_button(g_field_editor_screen,"SOURCE",field_source_cb,190,52);g_field_source_label=button_label(b);lv_obj_align(b,LV_ALIGN_TOP_RIGHT,-40,80);l=lv_label_create(g_field_editor_screen);lv_label_set_text(l,"Data");lv_obj_align(l,LV_ALIGN_TOP_LEFT,40,175);b=make_button(g_field_editor_screen,"<",field_metric_prev_cb,55,50);lv_obj_align(b,LV_ALIGN_TOP_LEFT,130,157);b=make_button(g_field_editor_screen,"",field_metric_next_cb,190,50);g_field_metric_label=button_label(b);lv_obj_align(b,LV_ALIGN_TOP_MID,55,157);b=make_button(g_field_editor_screen,">",field_metric_next_cb,55,50);lv_obj_align(b,LV_ALIGN_TOP_RIGHT,-25,157);l=lv_label_create(g_field_editor_screen);lv_label_set_text(l,"Device");lv_obj_align(l,LV_ALIGN_TOP_LEFT,40,250);g_field_device_button=make_button(g_field_editor_screen,"",field_device_cb,250,52);g_field_device_label=button_label(g_field_device_button);lv_obj_align(g_field_device_button,LV_ALIGN_TOP_RIGHT,-40,232);b=make_button(g_field_editor_screen,"DONE",field_done_cb,150,54);lv_obj_align(b,LV_ALIGN_BOTTOM_MID,0,-40);}
void create_units_screen(){g_units_screen=lv_obj_create(nullptr);lv_obj_remove_flag(g_units_screen,LV_OBJ_FLAG_SCROLLABLE);lv_obj_t*l=lv_label_create(g_units_screen);lv_label_set_text(l,"UNITS");lv_obj_set_style_text_font(l,&lv_font_montserrat_24,0);lv_obj_align(l,LV_ALIGN_TOP_MID,0,18);const char*names[]={"Depth","Temperature","Wind speed","Boat speed","Distance","Short distance"};lv_obj_t**vals[]={&g_units_depth,&g_units_temp,&g_units_wind,&g_units_vessel,&g_units_distance,&g_units_short};lv_event_cb_t cbs[]={unit_depth_cb,unit_temp_cb,unit_wind_cb,unit_vessel_cb,unit_distance_cb,unit_short_cb};for(int i=0;i<6;++i){l=lv_label_create(g_units_screen);lv_label_set_text(l,names[i]);lv_obj_align(l,LV_ALIGN_TOP_LEFT,32,64+i*48);lv_obj_t*b=make_button(g_units_screen,"",cbs[i],170,40);*vals[i]=button_label(b);lv_obj_align(b,LV_ALIGN_TOP_RIGHT,-32,54+i*48);}l=lv_label_create(g_units_screen);lv_label_set_text(l,"Short if");lv_obj_align(l,LV_ALIGN_TOP_LEFT,32,355);lv_obj_t*b=make_button(g_units_screen,"-",unit_threshold_down_cb,48,38);lv_obj_align(b,LV_ALIGN_TOP_LEFT,145,344);b=make_button(g_units_screen,"",unit_threshold_up_cb,125,38);g_units_threshold=button_label(b);lv_obj_align(b,LV_ALIGN_TOP_MID,60,344);b=make_button(g_units_screen,"+",unit_threshold_up_cb,48,38);lv_obj_align(b,LV_ALIGN_TOP_RIGHT,-32,344);b=make_button(g_units_screen,"BACK",settings_screen_cb,120,46);lv_obj_align(b,LV_ALIGN_BOTTOM_MID,0,-12);}
void create_wifi_screen()
{
    g_wifi_screen=lv_obj_create(nullptr);
    lv_obj_remove_flag(g_wifi_screen,LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t*l=lv_label_create(g_wifi_screen);
    lv_label_set_text(l,"Wi-Fi");
    lv_obj_set_style_text_font(l,&lv_font_montserrat_24,0);
    lv_obj_align(l,LV_ALIGN_TOP_MID,0,18);

    l=lv_label_create(g_wifi_screen);
    lv_label_set_text(l,"Enabled");
    lv_obj_align(l,LV_ALIGN_TOP_LEFT,34,70);
    g_wifi_enabled=lv_switch_create(g_wifi_screen);
    lv_obj_align(g_wifi_enabled,LV_ALIGN_TOP_RIGHT,-40,58);

    l=lv_label_create(g_wifi_screen);
    lv_label_set_text(l,"SSID");
    lv_obj_align(l,LV_ALIGN_TOP_LEFT,34,128);
    g_wifi_ssid=lv_textarea_create(g_wifi_screen);
    lv_obj_set_size(g_wifi_ssid,300,44);
    lv_textarea_set_one_line(g_wifi_ssid,true);
    lv_textarea_set_max_length(g_wifi_ssid,32);
    lv_obj_align(g_wifi_ssid,LV_ALIGN_TOP_RIGHT,-34,112);
    lv_obj_set_style_bg_color(g_wifi_ssid,ui_card(),0);
    lv_obj_set_style_text_color(g_wifi_ssid,ui_text(),0);
    lv_obj_set_style_border_color(g_wifi_ssid,ui_border(),0);
    lv_obj_set_style_radius(g_wifi_ssid,8,0);
    lv_obj_add_event_cb(g_wifi_ssid,wifi_textarea_focus_cb,LV_EVENT_FOCUSED,nullptr);

    l=lv_label_create(g_wifi_screen);
    lv_label_set_text(l,"Password");
    lv_obj_align(l,LV_ALIGN_TOP_LEFT,34,188);
    g_wifi_password=lv_textarea_create(g_wifi_screen);
    lv_obj_set_size(g_wifi_password,300,44);
    lv_textarea_set_one_line(g_wifi_password,true);
    lv_textarea_set_password_mode(g_wifi_password,true);
    lv_textarea_set_max_length(g_wifi_password,64);
    lv_obj_align(g_wifi_password,LV_ALIGN_TOP_RIGHT,-34,172);
    lv_obj_set_style_bg_color(g_wifi_password,ui_card(),0);
    lv_obj_set_style_text_color(g_wifi_password,ui_text(),0);
    lv_obj_set_style_border_color(g_wifi_password,ui_border(),0);
    lv_obj_set_style_radius(g_wifi_password,8,0);
    lv_obj_add_event_cb(g_wifi_password,wifi_textarea_focus_cb,LV_EVENT_FOCUSED,nullptr);

    g_wifi_status=lv_label_create(g_wifi_screen);
    lv_label_set_text(g_wifi_status,"Disabled");
    lv_obj_set_style_text_align(g_wifi_status,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_width(g_wifi_status,400);
    lv_obj_align(g_wifi_status,LV_ALIGN_TOP_MID,0,245);

    lv_obj_t*b=make_button(g_wifi_screen,"SAVE / CONNECT",wifi_save_cb,180,48);
    lv_obj_align(b,LV_ALIGN_BOTTOM_LEFT,36,-22);
    b=make_button(g_wifi_screen,"BACK",settings_screen_cb,120,48);
    lv_obj_align(b,LV_ALIGN_BOTTOM_RIGHT,-36,-22);

    g_wifi_keyboard=lv_keyboard_create(g_wifi_screen);
    lv_obj_set_size(g_wifi_keyboard,460,205);
    lv_obj_align(g_wifi_keyboard,LV_ALIGN_BOTTOM_MID,0,0);
    lv_obj_add_event_cb(g_wifi_keyboard,wifi_keyboard_cb,LV_EVENT_ALL,nullptr);
    lv_obj_add_flag(g_wifi_keyboard,LV_OBJ_FLAG_HIDDEN);
}
void create_shunts_screen(){g_shunts_screen=lv_obj_create(nullptr);lv_obj_remove_flag(g_shunts_screen,LV_OBJ_FLAG_SCROLLABLE);lv_obj_t*l=lv_label_create(g_shunts_screen);lv_label_set_text(l,"SMARTSHUNTS");lv_obj_set_style_text_font(l,&lv_font_montserrat_24,0);lv_obj_align(l,LV_ALIGN_TOP_MID,0,22);for(size_t i=0;i<MAX_SMARTSHUNTS;++i){lv_obj_t*b=make_button(g_shunts_screen,"",shunt_slot_cb,360,62,nullptr);g_shunt_slot_labels[i]=button_label(b);lv_obj_align(b,LV_ALIGN_TOP_MID,0,78+static_cast<int>(i)*72);lv_obj_remove_event_cb(b,shunt_slot_cb);lv_obj_add_event_cb(b,shunt_slot_cb,LV_EVENT_CLICKED,reinterpret_cast<void*>(i));}lv_obj_t*b=make_button(g_shunts_screen,"BACK",settings_screen_cb,120,46);lv_obj_align(b,LV_ALIGN_BOTTOM_MID,0,-12);}
void create_shunt_edit_screen(){g_shunt_edit_screen=lv_obj_create(nullptr);lv_obj_remove_flag(g_shunt_edit_screen,LV_OBJ_FLAG_SCROLLABLE);lv_obj_t*l=lv_label_create(g_shunt_edit_screen);lv_label_set_text(l,"SMARTSHUNT");lv_obj_set_style_text_font(l,&lv_font_montserrat_24,0);lv_obj_align(l,LV_ALIGN_TOP_MID,0,14);lv_obj_t*b=make_button(g_shunt_edit_screen,"SELECT NEARBY",choose_nearby_cb,200,44);lv_obj_align(b,LV_ALIGN_TOP_MID,0,52);l=lv_label_create(g_shunt_edit_screen);lv_label_set_text(l,"Name");lv_obj_align(l,LV_ALIGN_TOP_LEFT,30,112);g_shunt_name=lv_textarea_create(g_shunt_edit_screen);lv_obj_set_style_bg_color(g_shunt_name,ui_card(),0);lv_obj_set_style_text_color(g_shunt_name,ui_text(),0);lv_obj_set_style_border_color(g_shunt_name,ui_border(),0);lv_obj_set_style_radius(g_shunt_name,8,0);lv_obj_set_size(g_shunt_name,300,42);lv_textarea_set_one_line(g_shunt_name,true);lv_textarea_set_max_length(g_shunt_name,24);lv_obj_align(g_shunt_name,LV_ALIGN_TOP_RIGHT,-30,100);lv_obj_add_event_cb(g_shunt_name,textarea_focus_cb,LV_EVENT_FOCUSED,nullptr);l=lv_label_create(g_shunt_edit_screen);lv_label_set_text(l,"Key");lv_obj_align(l,LV_ALIGN_TOP_LEFT,30,167);g_shunt_key=lv_textarea_create(g_shunt_edit_screen);lv_obj_set_style_bg_color(g_shunt_key,ui_card(),0);lv_obj_set_style_text_color(g_shunt_key,ui_text(),0);lv_obj_set_style_border_color(g_shunt_key,ui_border(),0);lv_obj_set_style_radius(g_shunt_key,8,0);lv_obj_set_size(g_shunt_key,300,42);lv_textarea_set_one_line(g_shunt_key,true);lv_textarea_set_password_mode(g_shunt_key,true);lv_textarea_set_max_length(g_shunt_key,32);lv_obj_align(g_shunt_key,LV_ALIGN_TOP_RIGHT,-30,155);lv_obj_add_event_cb(g_shunt_key,textarea_focus_cb,LV_EVENT_FOCUSED,nullptr);l=lv_label_create(g_shunt_edit_screen);lv_label_set_text(l,"Send to N2K");lv_obj_align(l,LV_ALIGN_TOP_LEFT,30,220);g_shunt_n2k=lv_switch_create(g_shunt_edit_screen);lv_obj_align(g_shunt_n2k,LV_ALIGN_TOP_RIGHT,-45,207);l=lv_label_create(g_shunt_edit_screen);lv_label_set_text(l,"Battery instance");lv_obj_align(l,LV_ALIGN_TOP_LEFT,30,272);b=make_button(g_shunt_edit_screen,"-",shunt_instance_down_cb,48,38);lv_obj_align(b,LV_ALIGN_TOP_RIGHT,-180,258);g_shunt_instance=lv_label_create(g_shunt_edit_screen);lv_obj_set_style_text_font(g_shunt_instance,&lv_font_montserrat_20,0);lv_obj_align(g_shunt_instance,LV_ALIGN_TOP_RIGHT,-112,267);b=make_button(g_shunt_edit_screen,"+",shunt_instance_up_cb,48,38);lv_obj_align(b,LV_ALIGN_TOP_RIGHT,-40,258);b=make_button(g_shunt_edit_screen,"SAVE",shunt_save_cb,120,46);lv_obj_align(b,LV_ALIGN_BOTTOM_LEFT,45,-18);b=make_button(g_shunt_edit_screen,"BACK",shunts_screen_cb,120,46);lv_obj_align(b,LV_ALIGN_BOTTOM_RIGHT,-45,-18);g_keyboard=lv_keyboard_create(g_shunt_edit_screen);lv_obj_set_size(g_keyboard,460,205);lv_obj_align(g_keyboard,LV_ALIGN_BOTTOM_MID,0,0);lv_obj_add_event_cb(g_keyboard,keyboard_cb,LV_EVENT_ALL,nullptr);lv_obj_add_flag(g_keyboard,LV_OBJ_FLAG_HIDDEN);}
}

void ui_start(AppSettings initial_settings)
{
    g_settings=initial_settings;g_active_page=first_enabled_page();create_data_screen();create_settings_screen();create_page_setup_screen();create_field_editor_screen();create_units_screen();create_wifi_screen();create_shunts_screen();create_shunt_edit_screen();apply_theme();render_active_page();update_units();update_page_setup();update_shunts_list();lv_screen_load(g_data_screen);g_refresh_timer=lv_timer_create(refresh_cb,500,nullptr);
}
