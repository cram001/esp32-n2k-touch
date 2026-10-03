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
OperatingMode g_mode_draft = OperatingMode::CanN2kBluetooth;
bool g_input_feedback = false;
void update_input_status();
lv_obj_t *g_settings_screen = nullptr;
lv_obj_t *g_page_setup_screen = nullptr;
lv_obj_t *g_field_editor_screen = nullptr;
lv_obj_t *g_units_screen = nullptr;
lv_obj_t *g_shunts_screen = nullptr;
lv_obj_t *g_shunt_edit_screen = nullptr;
lv_obj_t *g_wifi_screen = nullptr;
lv_obj_t *g_shunt_picker_screen = nullptr;
lv_obj_t *g_keyboard = nullptr;
lv_obj_t *g_wifi_keyboard = nullptr;
lv_timer_t *g_refresh_timer = nullptr;
std::atomic<uint32_t> g_ui_refreshes{0};
std::atomic<uint32_t> g_ui_last_refresh_ms{0};

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

lv_obj_t *g_units_heading = nullptr;
lv_obj_t *g_units_depth = nullptr;
lv_obj_t *g_units_temp = nullptr;
lv_obj_t *g_units_wind = nullptr;
lv_obj_t *g_units_vessel = nullptr;
lv_obj_t *g_units_distance = nullptr;
lv_obj_t *g_units_short = nullptr;
lv_obj_t *g_units_latlon = nullptr;
lv_obj_t *g_units_threshold = nullptr;

std::array<lv_obj_t *, MAX_SMARTSHUNTS> g_shunt_slot_labels{};
lv_obj_t *g_shunt_name = nullptr;
lv_obj_t *g_shunt_key = nullptr;
lv_obj_t *g_shunt_n2k = nullptr;
lv_obj_t *g_shunt_instance = nullptr;
std::array<lv_obj_t *, MAX_DISCOVERED_SMARTSHUNTS> g_shunt_picker_buttons{};
std::array<lv_obj_t *, MAX_DISCOVERED_SMARTSHUNTS> g_shunt_picker_labels{};
std::array<DiscoveredSmartShunt, MAX_DISCOVERED_SMARTSHUNTS> g_picker_devices{};
size_t g_picker_count = 0;

lv_obj_t *g_wifi_enabled = nullptr;
lv_obj_t *g_wifi_ssid = nullptr;
lv_obj_t *g_wifi_password = nullptr;
lv_obj_t *g_wifi_status = nullptr;
lv_obj_t *g_wifi_mode = nullptr;
lv_obj_t *g_wifi_show = nullptr;
lv_obj_t *g_wifi_open = nullptr;
lv_obj_t *g_wifi_picker = nullptr;
lv_obj_t *g_wifi_scan_status = nullptr;
lv_obj_t *g_wifi_scan_list = nullptr;
WifiConfig g_wifi_draft{};
char g_wifi_feedback[128]{};
uint32_t g_wifi_feedback_time = 0;
void wifi_feedback(const char *text) {
    std::snprintf(g_wifi_feedback,sizeof(g_wifi_feedback),"%s",text);
    g_wifi_feedback_time=lv_tick_get();
    lv_label_set_text(g_wifi_status,text);
}
WifiScanResults g_wifi_scan_snapshot{};
uint32_t g_wifi_scan_generation = UINT32_MAX;
lv_obj_t *g_ota_screen = nullptr;
lv_obj_t *g_ota_url = nullptr;
lv_obj_t *g_ota_status = nullptr;
lv_obj_t *g_ota_web_button = nullptr;
lv_obj_t *g_ota_keyboard = nullptr;
char g_ota_feedback[128]{};
uint32_t g_ota_feedback_time=0;
void ota_feedback(const char *text) {
    std::snprintf(g_ota_feedback,sizeof(g_ota_feedback),"%s",text);g_ota_feedback_time=lv_tick_get();
    lv_label_set_text(g_ota_status,text);
}
void update_wifi_scan();
void update_ota_status();
void update_shunt_picker();

const std::array<DataMetric, 47> NMEA_METRICS = {
    DataMetric::None,
    DataMetric::Depth,
    DataMetric::BoatSpeed,
    DataMetric::SpeedOverGround,
    DataMetric::CourseOverGround,
    DataMetric::Heading,
    DataMetric::ApparentWindSpeed,
    DataMetric::ApparentWindAngle,
    DataMetric::TrueWindSpeed,
    DataMetric::TrueWindAngle,
    DataMetric::WaterTemperature,
    DataMetric::AirTemperature,
    DataMetric::DistanceToWaypoint,
    DataMetric::TripDistance,
    DataMetric::EngineRpm,
    DataMetric::EngineCoolantTemperature,
    DataMetric::EngineOilPressure,
    DataMetric::EngineOilTemperature,
    DataMetric::EngineBoostPressure,
    DataMetric::EngineAlternatorVoltage,
    DataMetric::EngineFuelRate,
    DataMetric::EngineHours,
    DataMetric::EngineCoolantPressure,
    DataMetric::EngineFuelPressure,
    DataMetric::EngineLoad,
    DataMetric::EngineTorque,
    DataMetric::EngineExhaustTemperature,
    DataMetric::TankLevel,
    DataMetric::TankCapacity,
    DataMetric::Latitude,
    DataMetric::Longitude,
    DataMetric::Position,
    DataMetric::Altitude,
    DataMetric::WaypointBearing,
    DataMetric::WaypointVmg,
    DataMetric::CrossTrackError,
    DataMetric::WaypointName,
    DataMetric::CabinTemperature,
    DataMetric::EngineRoomTemperature,
    DataMetric::EnvironmentalTemperature,
    DataMetric::AtmosphericPressure,
    DataMetric::Humidity,
    DataMetric::MagneticVariation,
    DataMetric::DepthTransducer,
    DataMetric::DepthBelowKeel,
    DataMetric::DepthWaterline,
    DataMetric::DepthSensorOffset
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
    const esp_err_t err = bsp_display_brightness_set(g_boot_brightness_override?DEFAULT_DAY_BRIGHTNESS:active_brightness());
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
    apply_theme_to(g_source_picker);apply_theme_to(g_metric_picker);apply_theme_to(g_depth_screen);
    apply_theme_to(g_input_screen);
    apply_theme_to(g_data_screen); apply_theme_to(g_settings_screen); apply_theme_to(g_page_setup_screen);
    apply_theme_to(g_field_editor_screen); apply_theme_to(g_units_screen); apply_theme_to(g_shunts_screen);
    apply_theme_to(g_shunt_edit_screen); apply_theme_to(g_shunt_picker_screen); apply_theme_to(g_wifi_screen); apply_theme_to(g_wifi_picker); apply_theme_to(g_ota_screen); apply_backlight();
}

void apply_runtime_settings()
{
    if(g_settings.operating_mode==OperatingMode::CanN2kBluetooth) {
        smartshunt_ble_apply_settings(g_settings);
        n2k_bridge_apply_settings(g_settings);
    } else if(g_settings.operating_mode==OperatingMode::WifiN2k) {
        wifi_service_apply_config(g_settings.wifi);
        n2k_bridge_apply_settings(g_settings);
    } else {
        wifi_service_apply_config(g_settings.wifi);
    }
}

void persist()
{
    if (!settings_save(g_settings)) ESP_LOGW(TAG, "Could not persist settings");
    apply_runtime_settings(); apply_theme();
}

lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, int w=120, int h=48, void *user=nullptr)
{
    lv_obj_t *b = require_obj(lv_button_create(parent), "button");
    lv_obj_set_size(b,w,h); lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,user); style_button(b);
    lv_obj_t *l = require_obj(lv_label_create(b), "button label"); lv_label_set_text(l,text); lv_obj_set_style_text_font(l,&lv_font_montserrat_14,0); lv_obj_center(l); return b;
}

lv_obj_t *button_label(lv_obj_t *button) { return lv_obj_get_child(button,0); }

size_t first_enabled_page()
{
    for (size_t i = 0; i < g_settings.pages.size(); ++i) {
        if (g_settings.pages[i].enabled) return i;
    }
    return 0;
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
    // Gestures replace the bottom toolbar; reserve only the page indicator.
    constexpr int top=48, area_height=410, gap=8;
    const size_t count=layout_count(layout);
    const int columns=count>=4?2:1;
    const int rows=static_cast<int>(count)/columns;
    const int w=columns==2?226:460;
    const int h=(area_height-(rows-1)*gap)/rows;
    const int x=10+static_cast<int>(index%columns)*(w+gap);
    const int y=top+static_cast<int>(index/columns)*(h+gap);
    lv_obj_set_pos(box,x,y);lv_obj_set_size(box,w,h);lv_obj_set_style_pad_all(box,8,0);style_card(box);
    lv_obj_set_style_text_color(g_tile_titles[index], ui_muted(), 0);
    lv_obj_set_style_text_color(g_tile_values[index], ui_text(), 0);
    lv_obj_set_style_text_color(g_tile_units[index], ui_muted(), 0);
    lv_obj_set_style_text_color(g_tile_sources[index], ui_muted(), 0);
    lv_obj_align(g_tile_titles[index],LV_ALIGN_TOP_LEFT,2,0);
    lv_obj_align(g_tile_values[index],LV_ALIGN_CENTER,-10,count==6?2:5);
    lv_obj_align_to(g_tile_units[index],g_tile_values[index],LV_ALIGN_OUT_RIGHT_MID,6,0);
    lv_obj_set_width(g_tile_sources[index],w-20);
    lv_label_set_long_mode(g_tile_sources[index],LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(g_tile_sources[index],LV_TEXT_ALIGN_RIGHT,0);
    lv_obj_align(g_tile_sources[index],LV_ALIGN_BOTTOM_RIGHT,-2,0);
}

void fit_numeric_tile(size_t index,size_t count,const char *text,const char *unit)
{
    auto *value=g_tile_values[index];
    const int columns=count>=4?2:1,rows=static_cast<int>(count)/columns;
    // Card padding/border plus a small guard on both sides.
    const int width=(columns==2?226:460)-20;
    const int height=(410-(rows-1)*8)/rows-20;
    lv_obj_set_style_text_font(value,&lv_font_montserrat_48,0);
    lv_obj_set_style_text_letter_space(value,0,0);
    lv_obj_set_style_text_line_space(value,0,0);
    lv_point_t measured{};
    lv_text_get_size(&measured,text,&lv_font_montserrat_48,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    const int footer=lv_obj_has_flag(g_tile_sources[index],LV_OBJ_FLAG_HIDDEN)?0:
        lv_font_get_line_height(&lv_font_montserrat_14);
    const auto fit=tile_number_fit(width,height,lv_font_get_line_height(&lv_font_montserrat_20),
        unit[0]?lv_font_get_line_height(&lv_font_montserrat_20):0,footer,measured.x,measured.y);
    // LVGL scales the label uniformly from its top-left pivot. Explicitly
    // position the rendered bounds; raw label coordinates remain unscaled.
    lv_obj_set_style_transform_pivot_x(value,0,0);
    lv_obj_set_style_transform_pivot_y(value,0,0);
    lv_obj_set_style_transform_scale_x(value,fit.scale,0);
    lv_obj_set_style_transform_scale_y(value,fit.scale,0);
    lv_obj_set_align(value,LV_ALIGN_TOP_LEFT);
    lv_obj_set_size(value,measured.x,measured.y);
    lv_obj_set_pos(value,fit.x,fit.y);
    lv_obj_set_width(g_tile_titles[index],width);
    lv_label_set_long_mode(g_tile_titles[index],LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(g_tile_units[index],width);
    lv_obj_set_style_text_align(g_tile_units[index],LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_align(g_tile_units[index],LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(g_tile_units[index],0,fit.unit_y);
}

void render_active_page()
{
    static std::array<DepthDisplay,MAX_DATA_PAGES*MAX_DATA_FIELDS_PER_PAGE> depth_display{};
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
        const auto &sel=active.fields[i];lv_label_set_text(g_tile_titles[i],instrument_metric_name(sel.metric));
        const auto field_id=static_cast<uint8_t>(g_active_page*MAX_DATA_FIELDS_PER_PAGE+i);
        const InstrumentValue v=depth_display[field_id].apply(sel,g_settings.units.depth,instrument_data_get(sel,field_id));
        if(sel.source==DataSourceType::SmartShunt){
            lv_label_set_text(g_tile_sources[i],instrument_source_name(sel,g_settings,static_cast<uint8_t>(g_active_page*MAX_DATA_FIELDS_PER_PAGE+i)));
            lv_obj_remove_flag(g_tile_sources[i],LV_OBJ_FLAG_HIDDEN);
        }else if(sel.metric==DataMetric::TankLevel || sel.metric==DataMetric::TankCapacity){
            const char *fluid[]={"Fuel","Water","Grey water","Live well","Oil","Sewage","Gasoline"};
            lv_label_set_text(g_tile_sources[i],v.source_kind<7?fluid[v.source_kind]:"Tank");
            lv_obj_remove_flag(g_tile_sources[i],LV_OBJ_FLAG_HIDDEN);
        }else{
            lv_obj_add_flag(g_tile_sources[i],LV_OBJ_FLAG_HIDDEN);
        }
        if(sel.metric==DataMetric::Depth){
            const char *depth_titles[]={"Depth (transducer)","Depth (sensor)","Depth (keel)","Depth (surface)"};
            lv_label_set_text(g_tile_titles[i],depth_titles[v.depth_reference<4?v.depth_reference:0]);
        }
        if(sel.metric==DataMetric::WaypointName || sel.metric==DataMetric::Latitude || sel.metric==DataMetric::Longitude || sel.metric==DataMetric::Position){
            lv_obj_set_style_transform_scale_x(g_tile_values[i],256,0);
            lv_obj_set_style_transform_scale_y(g_tile_values[i],256,0);
            lv_obj_set_style_text_font(g_tile_values[i],count==1?&lv_font_montserrat_48:&lv_font_montserrat_24,0);
            lv_obj_set_height(g_tile_values[i],LV_SIZE_CONTENT);
            lv_obj_set_width(g_tile_values[i],lv_obj_get_width(g_tile_boxes[i])-90);
            lv_label_set_long_mode(g_tile_values[i],LV_LABEL_LONG_MODE_DOTS);
        }else{lv_obj_set_width(g_tile_values[i],LV_SIZE_CONTENT);lv_label_set_long_mode(g_tile_values[i],LV_LABEL_LONG_MODE_WRAP);}
        char value[80],unit[16];instrument_format_value(sel,g_settings.units,v,value,sizeof(value),unit,sizeof(unit));
        lv_label_set_text(g_tile_values[i],value);lv_label_set_text(g_tile_units[i],unit);
        const bool text_field=sel.metric==DataMetric::WaypointName || sel.metric==DataMetric::Latitude ||
            sel.metric==DataMetric::Longitude || sel.metric==DataMetric::Position;
        if(!text_field)fit_numeric_tile(i,count,value,unit);
        else {
            lv_obj_align(g_tile_values[i],LV_ALIGN_CENTER,-10,count==6?2:5);
            lv_obj_set_width(g_tile_units[i],LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(g_tile_units[i],LV_TEXT_ALIGN_LEFT,0);
            lv_obj_align_to(g_tile_units[i],g_tile_values[i],LV_ALIGN_OUT_RIGHT_MID,6,0);
        }
    }
}

void update_wifi_status()
{
    if(!g_wifi_status) return;
    if(g_wifi_feedback[0] && lv_tick_elaps(g_wifi_feedback_time)<6000) return;
    g_wifi_feedback[0]=0;
    const WifiStatus st=wifi_service_get_status();
    char b[240]{};
    switch(st.state){
    case WifiState::Disabled: std::snprintf(b,sizeof(b),"Disabled"); break;
    case WifiState::Connecting: std::snprintf(b,sizeof(b),"Connecting to %s...\n%s (reason %d)",st.ssid.data(),st.message.data(),st.disconnect_reason); break;
    case WifiState::Connected: std::snprintf(b,sizeof(b),"%s\n%s   %d dBm",st.ssid.data(),st.ip.data(),st.rssi); break;
    case WifiState::CredentialsRequired: std::snprintf(b,sizeof(b),"%s\n%s",st.ssid.data(),st.message.data()); break;
    case WifiState::Disconnected: std::snprintf(b,sizeof(b),"%s\nReason %d; retrying",st.message.data(),st.disconnect_reason); break;
    case WifiState::AccessPoint: {
        const auto server=ota_local_server_status();
        if(server.stage==LocalServerStage::Listening)std::snprintf(b,sizeof(b),"AP: %s\nUpdate page: http://%s\n%us remaining",st.ssid.data(),st.ip.data(),static_cast<unsigned>(ota_local_server_seconds_remaining()));
        else if(!ota_local_server_window_active())std::snprintf(b,sizeof(b),"AP: %s\nWeb update: disabled",st.ssid.data());
        else std::snprintf(b,sizeof(b),"AP: %s\nWeb: %s (0x%x)\nSocket errno: %d",st.ssid.data(),ota_local_server_stage_name(server.stage),server.error,server.socket_error);
        break;
    }
    case WifiState::Error: std::snprintf(b,sizeof(b),"Wi-Fi error 0x%x\n%s",st.last_error,st.message.data()); break;
    }
    lv_label_set_text(g_wifi_status,b);
}
void refresh_cb(lv_timer_t *){++g_ui_refreshes;g_ui_last_refresh_ms=static_cast<uint32_t>(esp_timer_get_time()/1000);render_active_page();update_wifi_status();update_wifi_scan();update_ota_status();update_input_status();if(lv_screen_active()==g_shunt_picker_screen)update_shunt_picker();}
void previous_page_cb(lv_event_t *){g_active_page=next_enabled_page(g_active_page,-1);render_active_page();}
void next_page_cb(lv_event_t *){g_active_page=next_enabled_page(g_active_page,+1);render_active_page();}
void data_screen_cb(lv_event_t *){render_active_page();lv_screen_load(g_data_screen);}
void settings_screen_cb(lv_event_t *){lv_screen_load(g_settings_screen);}
void rotation_label(){
    lv_label_set_text(button_label(g_rotation_button),g_settings.rotation==DisplayRotation::Normal?"Rotation: Normal (tap)":"Rotation: 180° (tap)");
}
void rotation_cb(lv_event_t *){
    AppSettings next=g_settings;
    next.rotation=next.rotation==DisplayRotation::Normal?DisplayRotation::Rotated180:DisplayRotation::Normal;
    if(!settings_save(next)){
        lv_label_set_text(button_label(g_rotation_button),"Rotation save failed; tap to retry");return;
    }
    g_settings.rotation=next.rotation;
    // Runs in LVGL's callback, under its existing ownership/lock. Display
    // rotation also transforms the associated touch coordinates in LVGL.
    if(auto *input=lv_indev_active())lv_indev_wait_release(input);
    lv_display_set_rotation(lv_obj_get_display(g_settings_screen),next.rotation==DisplayRotation::Normal?LV_DISPLAY_ROTATION_0:LV_DISPLAY_ROTATION_180);
    rotation_label();
}
void theme_toggle_cb(lv_event_t *){g_settings.theme=g_settings.theme==DisplayTheme::Day?DisplayTheme::Night:DisplayTheme::Day;persist();}
void brightness_down_cb(lv_event_t *){uint8_t &v=g_settings.theme==DisplayTheme::Day?g_settings.day_brightness:g_settings.night_brightness;v=v>10?static_cast<uint8_t>(v-10):1;persist();}
void brightness_up_cb(lv_event_t *){uint8_t &v=g_settings.theme==DisplayTheme::Day?g_settings.day_brightness:g_settings.night_brightness;v=v<91?static_cast<uint8_t>(v+10):100;persist();}

void update_page_setup()
{
    auto &p=g_settings.pages[g_edit_page];char b[120];std::snprintf(b,sizeof(b),"PAGE %u - %s",static_cast<unsigned>(g_edit_page+1),p.name.data());lv_label_set_text(g_page_setup_title,b);
    if(p.enabled)lv_obj_add_state(g_page_enable_switch,LV_STATE_CHECKED);else lv_obj_remove_state(g_page_enable_switch,LV_STATE_CHECKED);
    std::snprintf(b,sizeof(b),"LAYOUT: %u",static_cast<unsigned>(layout_count(p.layout)));lv_label_set_text(g_layout_button_label,b);
    const size_t count=layout_count(p.layout);
    for(size_t i=0;i<MAX_DATA_FIELDS_PER_PAGE;++i){if(i>=count){lv_obj_add_flag(g_field_buttons[i],LV_OBJ_FLAG_HIDDEN);continue;}lv_obj_remove_flag(g_field_buttons[i],LV_OBJ_FLAG_HIDDEN);const auto &f=p.fields[i];std::snprintf(b,sizeof(b),"%u. %s\n%s",static_cast<unsigned>(i+1),instrument_metric_name(f.metric),instrument_source_name(f,g_settings,static_cast<uint8_t>(g_edit_page*MAX_DATA_FIELDS_PER_PAGE+i)));lv_label_set_text(g_field_button_labels[i],b);}
}
void page_setup_screen_cb(lv_event_t *){update_page_setup();lv_screen_load(g_page_setup_screen);}
void edit_prev_page_cb(lv_event_t *){g_edit_page=(g_edit_page+MAX_DATA_PAGES-1)%MAX_DATA_PAGES;update_page_setup();}
void edit_next_page_cb(lv_event_t *){g_edit_page=(g_edit_page+1)%MAX_DATA_PAGES;update_page_setup();}
void page_enabled_cb(lv_event_t *e){g_settings.pages[g_edit_page].enabled=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED);bool any=false;for(const auto&p:g_settings.pages)any|=p.enabled;if(!any){g_settings.pages[g_edit_page].enabled=true;lv_obj_add_state(g_page_enable_switch,LV_STATE_CHECKED);}persist();update_page_setup();}
void layout_cb(lv_event_t *){auto &l=g_settings.pages[g_edit_page].layout;if(l==PageLayout::One)l=PageLayout::Two;else if(l==PageLayout::Two)l=PageLayout::Four;else if(l==PageLayout::Four)l=PageLayout::Six;else l=PageLayout::One;persist();update_page_setup();}

void update_field_editor()
{
    auto &f=g_settings.pages[g_edit_page].fields[g_edit_field];lv_label_set_text(g_field_source_label,f.source==DataSourceType::Nmea2000?"NMEA 2000":"SMARTSHUNT");lv_label_set_text(g_field_metric_label,instrument_metric_name(f.metric));
    lv_obj_remove_flag(g_field_device_button,LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(g_field_device_label,instrument_source_name(f,g_settings,editing_field_id()));
}
void open_field_editor_cb(lv_event_t *e){g_edit_field=static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));update_field_editor();lv_screen_load(g_field_editor_screen);}
void field_source_cb(lv_event_t *){if(!n2k_sources_save_choice(editing_field_id(),N2kSourceChoice{}))return;auto &f=g_settings.pages[g_edit_page].fields[g_edit_field];f.source=f.source==DataSourceType::Nmea2000?DataSourceType::SmartShunt:DataSourceType::Nmea2000;f.metric=f.source==DataSourceType::Nmea2000?DataMetric::Depth:DataMetric::BatteryVoltage;persist();update_field_editor();}
void field_metric_step(int direction){auto &f=g_settings.pages[g_edit_page].fields[g_edit_field];if(f.source==DataSourceType::Nmea2000){if(!n2k_sources_save_choice(editing_field_id(),N2kSourceChoice{}))return;int pos=0;for(size_t i=0;i<NMEA_METRICS.size();++i)if(NMEA_METRICS[i]==f.metric){pos=static_cast<int>(i);break;}const int n=static_cast<int>(NMEA_METRICS.size());pos=(pos+direction+n)%n;f.metric=NMEA_METRICS[static_cast<size_t>(pos)];}else{int pos=0;for(size_t i=0;i<SHUNT_METRICS.size();++i)if(SHUNT_METRICS[i]==f.metric){pos=static_cast<int>(i);break;}const int n=static_cast<int>(SHUNT_METRICS.size());pos=(pos+direction+n)%n;f.metric=SHUNT_METRICS[static_cast<size_t>(pos)];}persist();update_field_editor();}
void field_metric_prev_cb(lv_event_t *){field_metric_step(-1);}void field_metric_next_cb(lv_event_t *){field_metric_step(1);}
void field_device_cb(lv_event_t *){auto &f=g_settings.pages[g_edit_page].fields[g_edit_field];if(f.source==DataSourceType::Nmea2000){g_source_depth_picker=false;n2k_bridge_request_sources();source_picker_cb(nullptr);return;}for(size_t step=1;step<=MAX_SMARTSHUNTS;++step){size_t idx=(f.source_index+step)%MAX_SMARTSHUNTS;if(g_settings.smartshunts[idx].configured){f.source_index=idx;break;}}persist();update_field_editor();}
void field_done_cb(lv_event_t *){update_page_setup();lv_screen_load(g_page_setup_screen);}

const char *depth_name(){return g_settings.units.depth==DepthUnit::Metres?"METRES":"FEET";}const char *temp_name(){return g_settings.units.temperature==TemperatureUnit::Celsius?"CELSIUS":"FAHRENHEIT";}const char *speed_name(SpeedUnit u){return u==SpeedUnit::Knots?"KNOTS":(u==SpeedUnit::KilometresPerHour?"KM/H":"M/S");}const char *distance_name(){return g_settings.units.distance==DistanceUnit::NauticalMiles?"NM":"KM";}const char *short_name(){return g_settings.units.short_distance==ShortDistanceUnit::Metres?"METRES":(g_settings.units.short_distance==ShortDistanceUnit::Feet?"FEET":"YARDS");}
const char *latlon_name(){switch(g_settings.units.lat_lon_format){case LatLonFormat::DecimalDegrees:return "DECIMAL";case LatLonFormat::DegreesMinutesSeconds:return "DMS";default:return "DEG + MIN";}}
void update_units(){lv_label_set_text(g_units_heading,g_settings.units.heading_reference==HeadingReference::True?"TRUE":"MAGNETIC");lv_label_set_text(g_units_depth,depth_name());lv_label_set_text(g_units_temp,temp_name());lv_label_set_text(g_units_wind,speed_name(g_settings.units.wind_speed));lv_label_set_text(g_units_vessel,speed_name(g_settings.units.vessel_speed));lv_label_set_text(g_units_distance,distance_name());lv_label_set_text(g_units_short,short_name());lv_label_set_text(g_units_latlon,latlon_name());char b[24];std::snprintf(b,sizeof(b),"< %.2f NM",g_settings.units.short_distance_threshold_nm);lv_label_set_text(g_units_threshold,b);}
void unit_heading_cb(lv_event_t *){g_settings.units.heading_reference=g_settings.units.heading_reference==HeadingReference::True?HeadingReference::Magnetic:HeadingReference::True;persist();update_units();}
void unit_latlon_cb(lv_event_t *){auto &f=g_settings.units.lat_lon_format;f=f==LatLonFormat::DecimalDegrees?LatLonFormat::DegreesMinutes:(f==LatLonFormat::DegreesMinutes?LatLonFormat::DegreesMinutesSeconds:LatLonFormat::DecimalDegrees);persist();update_units();}
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
void hide_wifi_keyboard() {
    lv_obj_add_flag(g_wifi_keyboard,LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(g_wifi_keyboard,nullptr);
}
void wifi_mask_cb(lv_event_t *) {
    lv_textarea_set_password_mode(g_wifi_password,true);
    lv_obj_remove_state(g_wifi_show,LV_STATE_CHECKED);
    hide_wifi_keyboard();
}
void wifi_show_cb(lv_event_t *e) {
    lv_textarea_set_password_mode(g_wifi_password,!lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED));
}
bool read_wifi_draft() {
    if(std::strlen(lv_textarea_get_text(g_wifi_ssid))>32 || std::strlen(lv_textarea_get_text(g_wifi_password))>64) {
        wifi_feedback("SSID: max 32 bytes; password: max 64 bytes");return false;
    }
    g_wifi_draft.enabled=lv_obj_has_state(g_wifi_enabled,LV_STATE_CHECKED);
    const bool ap=g_wifi_draft.mode==WifiMode::AccessPoint;
    auto &ssid=ap?g_wifi_draft.ap_ssid:g_wifi_draft.ssid;
    auto &password=ap?g_wifi_draft.ap_password:g_wifi_draft.password;
    std::snprintf(ssid.data(),ssid.size(),"%s",lv_textarea_get_text(g_wifi_ssid));
    std::snprintf(password.data(),password.size(),"%s",lv_textarea_get_text(g_wifi_password));
    if(!ap) g_wifi_draft.open_network=lv_obj_has_state(g_wifi_open,LV_STATE_CHECKED);
    return true;
}
void show_wifi_draft() {
    const bool ap=g_wifi_draft.mode==WifiMode::AccessPoint;
    lv_dropdown_set_selected(g_wifi_mode,ap?1:0);
    if(g_wifi_draft.enabled) lv_obj_add_state(g_wifi_enabled,LV_STATE_CHECKED); else lv_obj_remove_state(g_wifi_enabled,LV_STATE_CHECKED);
    lv_textarea_set_text(g_wifi_ssid,(ap?g_wifi_draft.ap_ssid:g_wifi_draft.ssid).data());
    lv_textarea_set_text(g_wifi_password,(ap?g_wifi_draft.ap_password:g_wifi_draft.password).data());
    if(g_wifi_draft.open_network) lv_obj_add_state(g_wifi_open,LV_STATE_CHECKED); else lv_obj_remove_state(g_wifi_open,LV_STATE_CHECKED);
    if(ap) lv_obj_add_flag(g_wifi_open,LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(g_wifi_open,LV_OBJ_FLAG_HIDDEN);
    wifi_mask_cb(nullptr);
}
void wifi_mode_cb(lv_event_t *) {
    if(!read_wifi_draft()) {lv_dropdown_set_selected(g_wifi_mode,g_wifi_draft.mode==WifiMode::AccessPoint?1:0);return;}
    g_wifi_draft.mode=lv_dropdown_get_selected(g_wifi_mode)==1?WifiMode::AccessPoint:WifiMode::Station;
    show_wifi_draft();
}
void wifi_screen_cb(lv_event_t *) {
    g_wifi_feedback[0]=0;
    g_wifi_draft=g_settings.wifi;
    show_wifi_draft();update_wifi_status();lv_screen_load(g_wifi_screen);
}
void wifi_save_cb(lv_event_t *) {
    const auto ota=ota_get_status();
    if(ota.state!=OtaState::Idle && ota.state!=OtaState::Failed) { wifi_feedback("Finish firmware update before changing Wi-Fi");return; }
    if(!read_wifi_draft()) return;
    const char *reason=nullptr;
    if(!wifi_config_valid(g_wifi_draft,&reason)) { wifi_feedback(reason);return; }
    AppSettings candidate=g_settings;candidate.wifi=g_wifi_draft;
    if(!settings_save(candidate)) { wifi_feedback("Save failed; settings were not applied");return; }
    g_settings=candidate;
    if(!wifi_service_apply_config(candidate.wifi)) { wifi_feedback("Saved; Wi-Fi busy. Tap Save again to apply.");return; }
    wifi_mask_cb(nullptr);
    wifi_feedback("Saved; applying Wi-Fi settings");
}
void wifi_select_cb(lv_event_t *e) {
    const size_t index=reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    if(index>=g_wifi_scan_snapshot.count) return;
    const auto &network=g_wifi_scan_snapshot.networks[index];
    if(std::strcmp(lv_textarea_get_text(g_wifi_ssid),network.ssid.data())!=0) lv_textarea_set_text(g_wifi_password,"");
    lv_textarea_set_text(g_wifi_ssid,network.ssid.data());
    if(network.open) { lv_obj_add_state(g_wifi_open,LV_STATE_CHECKED);lv_textarea_set_text(g_wifi_password,""); }
    else lv_obj_remove_state(g_wifi_open,LV_STATE_CHECKED);
    lv_screen_load(g_wifi_screen);
}
void wifi_picker_back_cb(lv_event_t *) { lv_screen_load(g_wifi_screen); }
void wifi_scan_again_cb(lv_event_t *) {
    if(!wifi_service_request_scan()) lv_label_set_text(g_wifi_scan_status,"Scan busy; try again shortly");
}
void wifi_scan_cb(lv_event_t *) {
    if(g_wifi_draft.mode==WifiMode::AccessPoint) {wifi_feedback("Save Station mode before scanning");return;}
    hide_wifi_keyboard();
    lv_screen_load(g_wifi_picker);wifi_scan_again_cb(nullptr);
}
void update_wifi_scan() {
    if(!g_wifi_picker || lv_screen_active()!=g_wifi_picker) return;
    const auto results=wifi_service_get_scan();
    if(results.generation==g_wifi_scan_generation) return;
    g_wifi_scan_generation=results.generation;g_wifi_scan_snapshot=results;
    lv_obj_clean(g_wifi_scan_list);
    char text[96];
    if(results.state==WifiScanState::Scanning) lv_label_set_text(g_wifi_scan_status,"Scanning once... (2.4 GHz)");
    else if(results.state==WifiScanState::Failed) {std::snprintf(text,sizeof(text),"Scan failed: 0x%x; retry when station settles",results.last_error);lv_label_set_text(g_wifi_scan_status,text);}
    else {std::snprintf(text,sizeof(text),"%u networks; hidden SSIDs: enter manually",static_cast<unsigned>(results.count));lv_label_set_text(g_wifi_scan_status,text);}
    for(size_t i=0;i<results.count;++i) {
        const auto &network=results.networks[i];
        std::snprintf(text,sizeof(text),"%s\n%d dBm - %s",network.ssid.data(),network.rssi,network.open?"Open":"Secured");
        make_button(g_wifi_scan_list,text,wifi_select_cb,400,60,reinterpret_cast<void *>(i));
    }
}
void update_ota_status() {
    if(!g_ota_status || lv_screen_active()!=g_ota_screen) return;
    if(g_ota_feedback[0] && lv_tick_elaps(g_ota_feedback_time)<6000) return;
    g_ota_feedback[0]=0;
    const auto st=ota_get_status();const auto wifi=wifi_service_get_status();const auto server=ota_local_server_status();char text[360];
    const uint32_t remaining=ota_local_server_seconds_remaining();
    if(g_ota_web_button) lv_label_set_text(button_label(g_ota_web_button),
        ota_local_server_window_active()?"DISABLE WEB UPDATE":"ENABLE WEB UPDATE (120s)");
    if(server.stage==LocalServerStage::Listening) {
        std::snprintf(text,sizeof(text),"Version: %s\n%s (%d%%)  error: 0x%x\nWeb: ACTIVE - %us remaining\nBluetooth: paused\nAP page: http://%s\nOTA layout: %s",ota_running_version(),st.message[0]?st.message:"Ready for local upload",st.progress_percent,st.last_error,static_cast<unsigned>(remaining),wifi.ip[0]?wifi.ip.data():"192.168.4.1",ota_partition_layout_valid()?"ready":"incompatible; use USB");
    } else {
        std::snprintf(text,sizeof(text),"Version: %s\n%s (%d%%)  error: 0x%x\nWeb: %s%s\nBluetooth: normal\nOTA layout: %s",ota_running_version(),st.message[0]?st.message:"Enter HTTPS URL or enable AP web update",st.progress_percent,st.last_error,
            ota_local_server_window_active()?ota_local_server_stage_name(server.stage):"disabled",
            server.error?" (startup error)":"",ota_partition_layout_valid()?"ready":"incompatible; use USB");
    }
    lv_label_set_text(g_ota_status,text);
}
void ota_screen_cb(lv_event_t *) { hide_wifi_keyboard();lv_screen_load(g_ota_screen);update_ota_status(); }
void ota_start_cb(lv_event_t *) {
    g_ota_feedback[0]=0;
    if(!wifi_service_is_connected()) {ota_feedback("HTTPS update needs a station connection");return;}
    if(!ota_start_https(lv_textarea_get_text(g_ota_url))) ota_feedback("Invalid HTTPS URL, scan busy, or update already active");
}
void ota_web_cb(lv_event_t *) {
    g_ota_feedback[0]=0;
    if(ota_local_server_window_active()) {
        if(!ota_disable_local_server()) ota_feedback("Web update cannot stop during upload or while reboot is ready");
        else ota_feedback("Stopping web update; Bluetooth will resume");
        return;
    }
    if(wifi_service_get_status().state!=WifiState::AccessPoint) {
        ota_feedback("Enable and save Access Point mode first");
        return;
    }
    if(!ota_enable_local_server(120)) ota_feedback("Could not start web update window");
    else ota_feedback("Starting web update; Bluetooth paused for up to 120 seconds");
}
void ota_reboot_cb(lv_event_t *) {if(ota_get_status().state==OtaState::ReadyToReboot)esp_restart();}
void ota_focus_cb(lv_event_t *) {lv_keyboard_set_textarea(g_ota_keyboard,g_ota_url);lv_obj_remove_flag(g_ota_keyboard,LV_OBJ_FLAG_HIDDEN);}
void ota_keyboard_cb(lv_event_t *e) {if(lv_event_get_code(e)==LV_EVENT_READY||lv_event_get_code(e)==LV_EVENT_CANCEL)lv_obj_add_flag(g_ota_keyboard,LV_OBJ_FLAG_HIDDEN);}
void update_shunts_list(){for(size_t i=0;i<MAX_SMARTSHUNTS;++i){char b[48];const auto&c=g_settings.smartshunts[i];std::snprintf(b,sizeof(b),"%u. %s",static_cast<unsigned>(i+1),c.configured?(c.name[0]?c.name.data():"SmartShunt"):"ADD SMARTSHUNT");lv_label_set_text(g_shunt_slot_labels[i],b);}}
void shunts_screen_cb(lv_event_t *){update_shunts_list();lv_screen_load(g_shunts_screen);}void shunt_slot_cb(lv_event_t *e){g_edit_shunt=static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));auto &c=g_settings.smartshunts[g_edit_shunt];lv_textarea_set_text(g_shunt_name,c.name.data());lv_textarea_set_text(g_shunt_key,c.bindkey.data());if(c.n2k_enabled)lv_obj_add_state(g_shunt_n2k,LV_STATE_CHECKED);else lv_obj_remove_state(g_shunt_n2k,LV_STATE_CHECKED);char b[12];std::snprintf(b,sizeof(b),"%u",c.battery_instance);lv_label_set_text(g_shunt_instance,b);lv_screen_load(g_shunt_edit_screen);}
void update_shunt_picker()
{
    // Keep row identities unchanged throughout a press, so a refreshed list
    // cannot select a different device when the finger is released.
    for(lv_indev_t *input=lv_indev_get_next(nullptr);input;input=lv_indev_get_next(input))
        if(lv_indev_get_state(input)==LV_INDEV_STATE_PRESSED)return;
    g_picker_devices = {};
    g_picker_count = smartshunt_ble_get_discovered(g_picker_devices);
    for (size_t i = 0; i < MAX_DISCOVERED_SMARTSHUNTS; ++i) {
        if (i >= g_picker_count) {
            lv_obj_add_flag(g_shunt_picker_buttons[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(g_shunt_picker_buttons[i], LV_OBJ_FLAG_HIDDEN);
        char b[64]{};
        std::snprintf(b, sizeof(b), "%s   %d dBm", g_picker_devices[i].name.data(), g_picker_devices[i].rssi);
        lv_label_set_text(g_shunt_picker_labels[i], b);
    }
}
void choose_nearby_cb(lv_event_t *)
{
    update_shunt_picker();
    lv_screen_load(g_shunt_picker_screen);
}
void shunt_picker_select_cb(lv_event_t *e)
{
    const size_t idx = static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (idx >= g_picker_count) return;
    const auto &picked = g_picker_devices[idx];
    auto &c = g_settings.smartshunts[g_edit_shunt];
    c.configured = true;
    c.enabled = true;
    std::snprintf(c.mac.data(), c.mac.size(), "%s", picked.mac.data());
    std::snprintf(c.name.data(), c.name.size(), "%s", picked.name.data());
    lv_textarea_set_text(g_shunt_name, c.name.data());
    persist();
    lv_screen_load(g_shunt_edit_screen);
}
void shunt_picker_back_cb(lv_event_t *) { lv_screen_load(g_shunt_edit_screen); }
void shunt_instance_down_cb(lv_event_t *){auto&c=g_settings.smartshunts[g_edit_shunt];if(c.battery_instance>0)--c.battery_instance;char b[12];std::snprintf(b,sizeof(b),"%u",c.battery_instance);lv_label_set_text(g_shunt_instance,b);persist();}void shunt_instance_up_cb(lv_event_t *){auto&c=g_settings.smartshunts[g_edit_shunt];if(c.battery_instance<252)++c.battery_instance;char b[12];std::snprintf(b,sizeof(b),"%u",c.battery_instance);lv_label_set_text(g_shunt_instance,b);persist();}
void shunt_save_cb(lv_event_t *){auto&c=g_settings.smartshunts[g_edit_shunt];std::snprintf(c.name.data(),c.name.size(),"%s",lv_textarea_get_text(g_shunt_name));std::snprintf(c.bindkey.data(),c.bindkey.size(),"%s",lv_textarea_get_text(g_shunt_key));c.n2k_enabled=lv_obj_has_state(g_shunt_n2k,LV_STATE_CHECKED);persist();update_shunts_list();lv_screen_load(g_shunts_screen);}void keyboard_cb(lv_event_t *e){const auto code=lv_event_get_code(e);if(code==LV_EVENT_READY||code==LV_EVENT_CANCEL){lv_obj_add_flag(g_keyboard,LV_OBJ_FLAG_HIDDEN);lv_keyboard_set_textarea(g_keyboard,nullptr);}}void textarea_focus_cb(lv_event_t *e){lv_keyboard_set_textarea(g_keyboard,lv_event_get_target_obj(e));lv_obj_remove_flag(g_keyboard,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(g_keyboard);}

void data_gesture_cb(lv_event_t *event)
{
    lv_indev_t *indev = lv_event_get_indev(event);
    if (!indev || lv_screen_active() != g_data_screen) return;
    // Consume the release before loading a different screen to prevent a click.
    lv_indev_wait_release(indev);
    switch (lv_indev_get_gesture_dir(indev)) {
    case LV_DIR_LEFT: next_page_cb(nullptr); break;
    case LV_DIR_RIGHT: previous_page_cb(nullptr); break;
    case LV_DIR_TOP: settings_screen_cb(nullptr); break;
    case LV_DIR_BOTTOM:
        g_edit_page = g_active_page; page_setup_screen_cb(nullptr); break;
    default: break;
    }
}
void boot_touch_cb(lv_event_t *event)
{
    if(lv_screen_active()!=g_boot_screen)return;
    const auto code=lv_event_get_code(event);
    if(code==LV_EVENT_PRESSED){
        g_boot_indev=lv_event_get_indev(event);
        g_boot_recovery.press(lv_tick_get());
    }else if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST){
        g_boot_recovery.release(lv_tick_get());
    }
}
void boot_timer_cb(lv_timer_t *timer)
{
    const uint32_t now=lv_tick_get();
    if(g_boot_recovery.restore_due(now)){
        const bool saved=settings_restore_brightness();
        g_boot_recovery.attempt_completed(saved,lv_tick_get());
        // Restore visibility even if NVS fails; a new hold can retry the save.
        g_settings.day_brightness=DEFAULT_DAY_BRIGHTNESS;
        g_settings.night_brightness=DEFAULT_NIGHT_BRIGHTNESS;
        lv_label_set_text(g_boot_recovery_status,saved?
            "BRIGHTNESS RESTORED: DAY 80% / NIGHT 20%":
            "Brightness restored for this boot; save failed.\nRelease and hold again to retry.");
    }
    if(!g_boot_recovery.can_finish(now))return;
    if(g_boot_indev)lv_indev_wait_release(g_boot_indev);
    lv_screen_load(g_data_screen);
    g_boot_brightness_override=false;
    apply_backlight();
    g_boot_status=nullptr;g_boot_recovery_status=nullptr;
    lv_obj_delete(g_boot_screen);g_boot_screen=nullptr;g_boot_indev=nullptr;
    lv_timer_delete(timer);
}
void update_input_status()
{
    const N2kInputStatus input=n2k_input_status();
    if(g_input_status && !g_input_feedback) {
        if(g_mode_draft==OperatingMode::CanN2kBluetooth)
            lv_label_set_text(g_input_status,"CAN/TWAI + Victron Bluetooth\nWi-Fi and web server are disabled in this mode.");
        else if(g_mode_draft==OperatingMode::WifiN2k)
            lv_label_set_text_fmt(g_input_status,"Wi-Fi N2K via W2K-1 TCP\n%s\nMessages: %lu  Rejected: %lu  Dropped: %lu",
                input.message.data(),static_cast<unsigned long>(input.received),
                static_cast<unsigned long>(input.rejected),static_cast<unsigned long>(input.dropped));
        else
            lv_label_set_text(g_input_status,"Firmware Update\nWi-Fi + web server only; CAN and Bluetooth disabled.\nAP or Station is selected on the Wi-Fi setup page.");
    }
    if(g_boot_status) {
        if(g_settings.operating_mode==OperatingMode::CanN2kBluetooth)
            lv_label_set_text(g_boot_status,"Mode 1: CAN N2K + Victron Bluetooth");
        else if(g_settings.operating_mode==OperatingMode::WifiN2k) {
            const WifiStatus wifi=wifi_service_get_status();
            lv_label_set_text_fmt(g_boot_status,"Mode 2: Wi-Fi N2K\nWi-Fi: %s\nN2K: %s",wifi.message.data(),input.message.data());
        } else {
            const WifiStatus wifi=wifi_service_get_status();
            lv_label_set_text_fmt(g_boot_status,"Mode 3: Firmware Update\nWi-Fi: %s\nWeb server starts automatically",wifi.message.data());
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
const char *operating_mode_label(OperatingMode mode) {
    switch(mode) {
    case OperatingMode::CanN2kBluetooth: return "MODE 1: CAN N2K + BLUETOOTH";
    case OperatingMode::WifiN2k: return "MODE 2: WI-FI N2K";
    case OperatingMode::FirmwareUpdate: return "MODE 3: FIRMWARE UPDATE";
    }
    return "MODE";
}
void input_mode_cb(lv_event_t *) {
    if(g_mode_draft==OperatingMode::CanN2kBluetooth) g_mode_draft=OperatingMode::WifiN2k;
    else if(g_mode_draft==OperatingMode::WifiN2k) g_mode_draft=OperatingMode::FirmwareUpdate;
    else g_mode_draft=OperatingMode::CanN2kBluetooth;
    lv_label_set_text(button_label(g_input_mode),operating_mode_label(g_mode_draft));
    update_input_status();
}
void input_screen_cb(lv_event_t *) {
    g_input_feedback=false;
    g_input_draft=g_settings.n2k_input;
    g_mode_draft=g_settings.operating_mode;
    lv_label_set_text(button_label(g_input_mode),operating_mode_label(g_mode_draft));
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
    if(g_mode_draft==OperatingMode::WifiN2k &&
       (!*text || *end || port>65535 || !n2k_endpoint_valid(g_input_draft.ip.data(),static_cast<uint16_t>(port)))) {
        g_input_feedback=true;lv_label_set_text(g_input_status,"Mode 2 requires a valid W2K-1 IPv4 address and port");return;
    }
    if(*text && !*end && port && port<=65535) g_input_draft.port=static_cast<uint16_t>(port);

    AppSettings candidate=g_settings;
    candidate.operating_mode=g_mode_draft;
    if(g_mode_draft==OperatingMode::WifiN2k) {
        candidate.n2k_input.mode=N2kInputMode::W2kTcp;
        candidate.n2k_input.ip=g_input_draft.ip;
        candidate.n2k_input.port=g_input_draft.port;
        WifiConfig wifi=candidate.wifi;wifi.enabled=true;wifi.mode=WifiMode::Station;
        const char *reason=nullptr;
        if(!wifi_config_valid(wifi,&reason)) {
            g_input_feedback=true;lv_label_set_text_fmt(g_input_status,"Configure Station Wi-Fi first: %s",reason?reason:"invalid Wi-Fi settings");return;
        }
    } else if(g_mode_draft==OperatingMode::CanN2kBluetooth) {
        candidate.n2k_input.mode=N2kInputMode::Wired;
    } else {
        WifiConfig wifi=candidate.wifi;wifi.enabled=true;
        const char *reason=nullptr;
        if(!wifi_config_valid(wifi,&reason)) {
            g_input_feedback=true;lv_label_set_text_fmt(g_input_status,"Configure AP or Station Wi-Fi first: %s",reason?reason:"invalid Wi-Fi settings");return;
        }
        if(g_settings.operating_mode!=OperatingMode::FirmwareUpdate)
            candidate.return_mode=g_settings.operating_mode==OperatingMode::WifiN2k?OperatingMode::WifiN2k:OperatingMode::CanN2kBluetooth;
    }
    if(g_mode_draft!=OperatingMode::FirmwareUpdate) candidate.return_mode=g_mode_draft;

    if(!settings_save(candidate)) {
        g_input_feedback=true;lv_label_set_text(g_input_status,"Could not save operating mode; please retry");return;
    }
    g_settings=candidate;
    lv_label_set_text(g_input_status,"Mode saved. Rebooting...");
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_restart();
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
    lv_obj_t *title = lv_label_create(g_input_screen); lv_label_set_text(title, "Operating Mode");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0); lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);
    g_input_mode = make_button(g_input_screen, "MODE 1: CAN N2K + BLUETOOTH", input_mode_cb, 400, 48);
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
    lv_label_set_text(hint, "Mode 2 uses W2K-1 TCP / N2K ASCII / Transmit.\nMode changes are applied by reboot.");
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
    b=make_button(g_settings_screen,"Operating Mode",input_screen_cb,200,48);
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
    b=make_button(form,"FIRMWARE UPDATE",ota_screen_cb,196,44);lv_obj_set_pos(b,216,246);
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

    g_ota_screen=lv_obj_create(nullptr);lv_obj_remove_flag(g_ota_screen,LV_OBJ_FLAG_SCROLLABLE);
    title=lv_label_create(g_ota_screen);lv_label_set_text(title,"Firmware update (HTTPS / AP)");lv_obj_align(title,LV_ALIGN_TOP_MID,0,18);
    g_ota_url=lv_textarea_create(g_ota_screen);lv_obj_set_size(g_ota_url,420,50);lv_obj_align(g_ota_url,LV_ALIGN_TOP_MID,0,70);lv_textarea_set_one_line(g_ota_url,true);lv_textarea_set_max_length(g_ota_url,255);lv_textarea_set_placeholder_text(g_ota_url,"https://.../firmware.bin");lv_obj_add_event_cb(g_ota_url,ota_focus_cb,LV_EVENT_FOCUSED,nullptr);
    b=make_button(g_ota_screen,"DOWNLOAD HTTPS",ota_start_cb,200,48);lv_obj_align(b,LV_ALIGN_TOP_LEFT,24,140);
    g_ota_web_button=make_button(g_ota_screen,"ENABLE WEB UPDATE (120s)",ota_web_cb,200,48);lv_obj_align(g_ota_web_button,LV_ALIGN_TOP_RIGHT,-24,140);
    b=make_button(g_ota_screen,"REBOOT IF READY",ota_reboot_cb,200,48);lv_obj_align(b,LV_ALIGN_TOP_MID,0,198);
    g_ota_status=lv_label_create(g_ota_screen);lv_obj_set_width(g_ota_status,420);lv_obj_align(g_ota_status,LV_ALIGN_TOP_MID,0,258);
    b=make_button(g_ota_screen,"BACK",wifi_picker_back_cb,120,48);lv_obj_align(b,LV_ALIGN_BOTTOM_MID,0,-16);
    g_ota_keyboard=lv_keyboard_create(g_ota_screen);lv_obj_set_size(g_ota_keyboard,460,205);lv_obj_align(g_ota_keyboard,LV_ALIGN_BOTTOM_MID,0,0);lv_obj_add_event_cb(g_ota_keyboard,ota_keyboard_cb,LV_EVENT_ALL,nullptr);lv_obj_add_flag(g_ota_keyboard,LV_OBJ_FLAG_HIDDEN);
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
