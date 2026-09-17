#include "ui.hpp"

#include <cstdio>
#include <cstring>

#include "bsp/display.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "n2k_bridge.hpp"
#include "smartshunt_ble.hpp"

namespace {
constexpr const char *TAG = "ui";

AppSettings g_settings;
lv_obj_t *g_depth_screen = nullptr;
lv_obj_t *g_battery_screen = nullptr;
lv_obj_t *g_settings_screen = nullptr;
lv_obj_t *g_smartshunt_screen = nullptr;
lv_obj_t *g_brightness_slider = nullptr;
lv_obj_t *g_brightness_value = nullptr;
lv_obj_t *g_theme_value = nullptr;
lv_obj_t *g_shunt_enable_switch = nullptr;
lv_obj_t *g_shunt_n2k_switch = nullptr;
lv_obj_t *g_battery_instance_value = nullptr;
lv_obj_t *g_mac_textarea = nullptr;
lv_obj_t *g_key_textarea = nullptr;
lv_obj_t *g_keyboard = nullptr;
lv_obj_t *g_soc_value = nullptr;
lv_obj_t *g_voltage_value = nullptr;
lv_obj_t *g_current_value = nullptr;
lv_obj_t *g_consumed_value = nullptr;
lv_obj_t *g_ttg_value = nullptr;
lv_obj_t *g_battery_status = nullptr;
lv_timer_t *g_battery_timer = nullptr;

uint8_t active_brightness()
{
    return g_settings.theme == DisplayTheme::Day ? g_settings.day_brightness : g_settings.night_brightness;
}

void apply_backlight()
{
    const uint8_t brightness = active_brightness();
    const esp_err_t err = bsp_display_brightness_set(brightness);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to set display brightness to %u%%: %s", brightness, esp_err_to_name(err));
    }
}

void apply_theme_to_screen(lv_obj_t *screen)
{
    if (screen == nullptr) return;
    const bool night = g_settings.theme == DisplayTheme::Night;
    const lv_color_t bg = night ? lv_color_hex(0x050000) : lv_color_hex(0xF5F7FA);
    const lv_color_t fg = night ? lv_color_hex(0xFF5A45) : lv_color_hex(0x101418);
    lv_obj_set_style_bg_color(screen, bg, 0);
    lv_obj_set_style_text_color(screen, fg, 0);
}

void apply_theme()
{
    apply_theme_to_screen(g_depth_screen);
    apply_theme_to_screen(g_battery_screen);
    apply_theme_to_screen(g_settings_screen);
    apply_theme_to_screen(g_smartshunt_screen);
    apply_backlight();
}

void apply_runtime_settings()
{
    smartshunt_ble_apply_settings(g_settings);
    n2k_bridge_apply_settings(g_settings);
}

void update_settings_labels()
{
    if (g_brightness_value != nullptr) {
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "%u%%", active_brightness());
        lv_label_set_text(g_brightness_value, buffer);
    }
    if (g_theme_value != nullptr) {
        lv_label_set_text(g_theme_value, g_settings.theme == DisplayTheme::Day ? "DAY" : "NIGHT");
    }
    if (g_brightness_slider != nullptr) {
        lv_slider_set_value(g_brightness_slider, active_brightness(), LV_ANIM_OFF);
    }
    if (g_battery_instance_value != nullptr) {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%u", g_settings.battery_instance);
        lv_label_set_text(g_battery_instance_value, buffer);
    }
    if (g_shunt_enable_switch != nullptr) {
        if (g_settings.smartshunt_enabled) lv_obj_add_state(g_shunt_enable_switch, LV_STATE_CHECKED);
        else lv_obj_remove_state(g_shunt_enable_switch, LV_STATE_CHECKED);
    }
    if (g_shunt_n2k_switch != nullptr) {
        if (g_settings.smartshunt_n2k_enabled) lv_obj_add_state(g_shunt_n2k_switch, LV_STATE_CHECKED);
        else lv_obj_remove_state(g_shunt_n2k_switch, LV_STATE_CHECKED);
    }
}

void persist_and_apply()
{
    if (!settings_save(g_settings)) {
        ESP_LOGW(TAG, "Settings changed but could not be persisted");
    }
    update_settings_labels();
    apply_runtime_settings();
    apply_theme();
}

lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_event_cb_t callback, int width = 150, int height = 56)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_center(label);
    return button;
}

void settings_button_cb(lv_event_t *)
{
    update_settings_labels();
    lv_screen_load(g_settings_screen);
}

void depth_button_cb(lv_event_t *)
{
    lv_screen_load(g_depth_screen);
}

void battery_button_cb(lv_event_t *)
{
    lv_screen_load(g_battery_screen);
}

void smartshunt_button_cb(lv_event_t *)
{
    update_settings_labels();
    lv_textarea_set_text(g_mac_textarea, g_settings.smartshunt_mac.data());
    lv_textarea_set_text(g_key_textarea, g_settings.smartshunt_bindkey.data());
    lv_screen_load(g_smartshunt_screen);
}

void day_button_cb(lv_event_t *)
{
    g_settings.theme = DisplayTheme::Day;
    persist_and_apply();
}

void night_button_cb(lv_event_t *)
{
    g_settings.theme = DisplayTheme::Night;
    persist_and_apply();
}

void brightness_changed_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target_obj(event);
    const int32_t value = lv_slider_get_value(slider);
    const uint8_t brightness = static_cast<uint8_t>(value < 1 ? 1 : (value > 100 ? 100 : value));
    if (g_settings.theme == DisplayTheme::Day) g_settings.day_brightness = brightness;
    else g_settings.night_brightness = brightness;
    persist_and_apply();
}

void shunt_enable_changed_cb(lv_event_t *event)
{
    g_settings.smartshunt_enabled = lv_obj_has_state(lv_event_get_target_obj(event), LV_STATE_CHECKED);
    if (!g_settings.smartshunt_enabled) {
        g_settings.smartshunt_n2k_enabled = false;
    }
    persist_and_apply();
}

void shunt_n2k_changed_cb(lv_event_t *event)
{
    const bool enabled = lv_obj_has_state(lv_event_get_target_obj(event), LV_STATE_CHECKED);
    g_settings.smartshunt_n2k_enabled = enabled && g_settings.smartshunt_enabled;
    persist_and_apply();
}

void battery_instance_minus_cb(lv_event_t *)
{
    if (g_settings.battery_instance > 0) --g_settings.battery_instance;
    persist_and_apply();
}

void battery_instance_plus_cb(lv_event_t *)
{
    if (g_settings.battery_instance < 252) ++g_settings.battery_instance;
    persist_and_apply();
}

void keyboard_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(g_keyboard, nullptr);
    }
}

void textarea_focus_cb(lv_event_t *event)
{
    lv_obj_t *textarea = lv_event_get_target_obj(event);
    lv_keyboard_set_textarea(g_keyboard, textarea);
    lv_obj_remove_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_keyboard);
}

void save_shunt_identity_cb(lv_event_t *)
{
    std::snprintf(g_settings.smartshunt_mac.data(), g_settings.smartshunt_mac.size(), "%s",
                  lv_textarea_get_text(g_mac_textarea));
    std::snprintf(g_settings.smartshunt_bindkey.data(), g_settings.smartshunt_bindkey.size(), "%s",
                  lv_textarea_get_text(g_key_textarea));
    persist_and_apply();
    lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
}

void update_battery_labels(lv_timer_t *)
{
    if (g_soc_value == nullptr) return;

    if (!g_settings.smartshunt_enabled) {
        lv_label_set_text(g_soc_value, "--.- %");
        lv_label_set_text(g_voltage_value, "--.-- V");
        lv_label_set_text(g_current_value, "--.-- A");
        lv_label_set_text(g_consumed_value, "Used: --.- Ah");
        lv_label_set_text(g_ttg_value, "TTG: --");
        lv_label_set_text(g_battery_status, "SmartShunt reading disabled");
        return;
    }

    const SmartShuntData data = smartshunt_ble_get_data();
    if (!data.valid) {
        lv_label_set_text(g_soc_value, "--.- %");
        lv_label_set_text(g_voltage_value, "--.-- V");
        lv_label_set_text(g_current_value, "--.-- A");
        lv_label_set_text(g_consumed_value, "Used: --.- Ah");
        lv_label_set_text(g_ttg_value, "TTG: --");
        if (data.source_mac[0] != '\0' && !data.key_valid) {
            char status[64];
            std::snprintf(status, sizeof(status), "Found %s - check key", data.source_mac);
            lv_label_set_text(g_battery_status, status);
        } else {
            lv_label_set_text(g_battery_status, "Scanning for SmartShunt...");
        }
        return;
    }

    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.1f %%", data.soc_pct);
    lv_label_set_text(g_soc_value, buffer);
    std::snprintf(buffer, sizeof(buffer), "%.2f V", data.voltage_v);
    lv_label_set_text(g_voltage_value, buffer);
    std::snprintf(buffer, sizeof(buffer), "%+.2f A", data.current_a);
    lv_label_set_text(g_current_value, buffer);
    std::snprintf(buffer, sizeof(buffer), "Used: %.1f Ah", data.consumed_ah);
    lv_label_set_text(g_consumed_value, buffer);

    if (data.time_to_go_min == 0xFFFFU) {
        lv_label_set_text(g_ttg_value, "TTG: --");
    } else {
        const uint16_t hours = data.time_to_go_min / 60U;
        const uint16_t mins = data.time_to_go_min % 60U;
        std::snprintf(buffer, sizeof(buffer), "TTG: %uh %02um", hours, mins);
        lv_label_set_text(g_ttg_value, buffer);
    }

    if (data.stale) {
        std::snprintf(buffer, sizeof(buffer), "BLE stale - last %lu ms ago", static_cast<unsigned long>(data.age_ms));
    } else if (g_settings.smartshunt_n2k_enabled) {
        std::snprintf(buffer, sizeof(buffer), "BLE %d dBm  |  N2K battery %u", data.rssi, g_settings.battery_instance);
    } else {
        std::snprintf(buffer, sizeof(buffer), "BLE %d dBm  |  N2K bridge off", data.rssi);
    }
    lv_label_set_text(g_battery_status, buffer);
}

void create_depth_screen()
{
    g_depth_screen = lv_obj_create(nullptr);
    lv_obj_remove_flag(g_depth_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(g_depth_screen);
    lv_label_set_text(title, "DEPTH");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 42);

    lv_obj_t *depth = lv_label_create(g_depth_screen);
    lv_label_set_text(depth, "--.- m");
    lv_obj_set_style_text_font(depth, &lv_font_montserrat_48, 0);
    lv_obj_align(depth, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *status = lv_label_create(g_depth_screen);
    lv_label_set_text(status, "NMEA 2000 depth not connected");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 24);

    lv_obj_t *battery = make_button(g_depth_screen, "BATTERY", battery_button_cb, 160, 56);
    lv_obj_align(battery, LV_ALIGN_BOTTOM_LEFT, 45, -34);
    lv_obj_t *settings = make_button(g_depth_screen, "SETTINGS", settings_button_cb, 160, 56);
    lv_obj_align(settings, LV_ALIGN_BOTTOM_RIGHT, -45, -34);
}

void create_battery_screen()
{
    g_battery_screen = lv_obj_create(nullptr);
    lv_obj_remove_flag(g_battery_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(g_battery_screen);
    lv_label_set_text(title, "HOUSE BATTERY");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    g_soc_value = lv_label_create(g_battery_screen);
    lv_label_set_text(g_soc_value, "--.- %");
    lv_obj_set_style_text_font(g_soc_value, &lv_font_montserrat_48, 0);
    lv_obj_align(g_soc_value, LV_ALIGN_TOP_MID, 0, 66);

    g_voltage_value = lv_label_create(g_battery_screen);
    lv_label_set_text(g_voltage_value, "--.-- V");
    lv_obj_set_style_text_font(g_voltage_value, &lv_font_montserrat_32, 0);
    lv_obj_align(g_voltage_value, LV_ALIGN_TOP_LEFT, 55, 145);

    g_current_value = lv_label_create(g_battery_screen);
    lv_label_set_text(g_current_value, "--.-- A");
    lv_obj_set_style_text_font(g_current_value, &lv_font_montserrat_32, 0);
    lv_obj_align(g_current_value, LV_ALIGN_TOP_RIGHT, -55, 145);

    g_consumed_value = lv_label_create(g_battery_screen);
    lv_label_set_text(g_consumed_value, "Used: --.- Ah");
    lv_obj_set_style_text_font(g_consumed_value, &lv_font_montserrat_20, 0);
    lv_obj_align(g_consumed_value, LV_ALIGN_TOP_LEFT, 55, 210);

    g_ttg_value = lv_label_create(g_battery_screen);
    lv_label_set_text(g_ttg_value, "TTG: --");
    lv_obj_set_style_text_font(g_ttg_value, &lv_font_montserrat_20, 0);
    lv_obj_align(g_ttg_value, LV_ALIGN_TOP_RIGHT, -55, 210);

    g_battery_status = lv_label_create(g_battery_screen);
    lv_label_set_text(g_battery_status, "SmartShunt reading disabled");
    lv_obj_set_style_text_font(g_battery_status, &lv_font_montserrat_14, 0);
    lv_obj_align(g_battery_status, LV_ALIGN_TOP_MID, 0, 275);

    lv_obj_t *depth = make_button(g_battery_screen, "DEPTH", depth_button_cb, 160, 56);
    lv_obj_align(depth, LV_ALIGN_BOTTOM_LEFT, 45, -30);
    lv_obj_t *settings = make_button(g_battery_screen, "SETTINGS", settings_button_cb, 160, 56);
    lv_obj_align(settings, LV_ALIGN_BOTTOM_RIGHT, -45, -30);
}

void create_settings_screen()
{
    g_settings_screen = lv_obj_create(nullptr);
    lv_obj_remove_flag(g_settings_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(g_settings_screen);
    lv_label_set_text(title, "SETTINGS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);

    lv_obj_t *theme_label = lv_label_create(g_settings_screen);
    lv_label_set_text(theme_label, "Theme");
    lv_obj_set_style_text_font(theme_label, &lv_font_montserrat_20, 0);
    lv_obj_align(theme_label, LV_ALIGN_TOP_LEFT, 38, 72);

    g_theme_value = lv_label_create(g_settings_screen);
    lv_obj_set_style_text_font(g_theme_value, &lv_font_montserrat_20, 0);
    lv_obj_align(g_theme_value, LV_ALIGN_TOP_RIGHT, -38, 72);

    lv_obj_t *day = make_button(g_settings_screen, "DAY", day_button_cb, 145, 50);
    lv_obj_align(day, LV_ALIGN_TOP_LEFT, 60, 105);
    lv_obj_t *night = make_button(g_settings_screen, "NIGHT", night_button_cb, 145, 50);
    lv_obj_align(night, LV_ALIGN_TOP_RIGHT, -60, 105);

    lv_obj_t *brightness_label = lv_label_create(g_settings_screen);
    lv_label_set_text(brightness_label, "Brightness");
    lv_obj_set_style_text_font(brightness_label, &lv_font_montserrat_20, 0);
    lv_obj_align(brightness_label, LV_ALIGN_TOP_LEFT, 38, 178);

    g_brightness_value = lv_label_create(g_settings_screen);
    lv_obj_set_style_text_font(g_brightness_value, &lv_font_montserrat_20, 0);
    lv_obj_align(g_brightness_value, LV_ALIGN_TOP_RIGHT, -38, 178);

    g_brightness_slider = lv_slider_create(g_settings_screen);
    lv_obj_set_size(g_brightness_slider, 380, 26);
    lv_slider_set_range(g_brightness_slider, 1, 100);
    lv_obj_align(g_brightness_slider, LV_ALIGN_TOP_MID, 0, 216);
    lv_obj_add_event_cb(g_brightness_slider, brightness_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *shunt = make_button(g_settings_screen, "SMARTSHUNT", smartshunt_button_cb, 210, 56);
    lv_obj_align(shunt, LV_ALIGN_TOP_MID, 0, 280);

    lv_obj_t *back = make_button(g_settings_screen, "BACK", depth_button_cb, 150, 56);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -24);
}

void create_smartshunt_screen()
{
    g_smartshunt_screen = lv_obj_create(nullptr);
    lv_obj_remove_flag(g_smartshunt_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(g_smartshunt_screen);
    lv_label_set_text(title, "SMARTSHUNT");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t *read_label = lv_label_create(g_smartshunt_screen);
    lv_label_set_text(read_label, "Read BLE Instant Readout");
    lv_obj_set_style_text_font(read_label, &lv_font_montserrat_14, 0);
    lv_obj_align(read_label, LV_ALIGN_TOP_LEFT, 28, 62);
    g_shunt_enable_switch = lv_switch_create(g_smartshunt_screen);
    lv_obj_align(g_shunt_enable_switch, LV_ALIGN_TOP_RIGHT, -30, 53);
    lv_obj_add_event_cb(g_shunt_enable_switch, shunt_enable_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *n2k_label = lv_label_create(g_smartshunt_screen);
    lv_label_set_text(n2k_label, "Send battery data to NMEA 2000");
    lv_obj_set_style_text_font(n2k_label, &lv_font_montserrat_14, 0);
    lv_obj_align(n2k_label, LV_ALIGN_TOP_LEFT, 28, 106);
    g_shunt_n2k_switch = lv_switch_create(g_smartshunt_screen);
    lv_obj_align(g_shunt_n2k_switch, LV_ALIGN_TOP_RIGHT, -30, 97);
    lv_obj_add_event_cb(g_shunt_n2k_switch, shunt_n2k_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *instance_label = lv_label_create(g_smartshunt_screen);
    lv_label_set_text(instance_label, "Battery instance");
    lv_obj_set_style_text_font(instance_label, &lv_font_montserrat_14, 0);
    lv_obj_align(instance_label, LV_ALIGN_TOP_LEFT, 28, 154);

    lv_obj_t *minus = make_button(g_smartshunt_screen, "-", battery_instance_minus_cb, 52, 40);
    lv_obj_align(minus, LV_ALIGN_TOP_RIGHT, -132, 140);
    g_battery_instance_value = lv_label_create(g_smartshunt_screen);
    lv_obj_set_style_text_font(g_battery_instance_value, &lv_font_montserrat_20, 0);
    lv_obj_align(g_battery_instance_value, LV_ALIGN_TOP_RIGHT, -92, 150);
    lv_obj_t *plus = make_button(g_smartshunt_screen, "+", battery_instance_plus_cb, 52, 40);
    lv_obj_align(plus, LV_ALIGN_TOP_RIGHT, -28, 140);

    lv_obj_t *mac_label = lv_label_create(g_smartshunt_screen);
    lv_label_set_text(mac_label, "MAC (optional if only one battery monitor)");
    lv_obj_set_style_text_font(mac_label, &lv_font_montserrat_14, 0);
    lv_obj_align(mac_label, LV_ALIGN_TOP_LEFT, 28, 196);
    g_mac_textarea = lv_textarea_create(g_smartshunt_screen);
    lv_obj_set_size(g_mac_textarea, 424, 46);
    lv_textarea_set_one_line(g_mac_textarea, true);
    lv_textarea_set_max_length(g_mac_textarea, 17);
    lv_textarea_set_placeholder_text(g_mac_textarea, "AA:BB:CC:DD:EE:FF");
    lv_obj_align(g_mac_textarea, LV_ALIGN_TOP_MID, 0, 216);
    lv_obj_add_event_cb(g_mac_textarea, textarea_focus_cb, LV_EVENT_FOCUSED, nullptr);

    lv_obj_t *key_label = lv_label_create(g_smartshunt_screen);
    lv_label_set_text(key_label, "Instant Readout key (32 hex characters)");
    lv_obj_set_style_text_font(key_label, &lv_font_montserrat_14, 0);
    lv_obj_align(key_label, LV_ALIGN_TOP_LEFT, 28, 270);
    g_key_textarea = lv_textarea_create(g_smartshunt_screen);
    lv_obj_set_size(g_key_textarea, 424, 46);
    lv_textarea_set_one_line(g_key_textarea, true);
    lv_textarea_set_max_length(g_key_textarea, 32);
    lv_textarea_set_password_mode(g_key_textarea, true);
    lv_textarea_set_placeholder_text(g_key_textarea, "Encryption key from VictronConnect");
    lv_obj_align(g_key_textarea, LV_ALIGN_TOP_MID, 0, 290);
    lv_obj_add_event_cb(g_key_textarea, textarea_focus_cb, LV_EVENT_FOCUSED, nullptr);

    lv_obj_t *save = make_button(g_smartshunt_screen, "SAVE", save_shunt_identity_cb, 120, 48);
    lv_obj_align(save, LV_ALIGN_BOTTOM_LEFT, 44, -18);
    lv_obj_t *back = make_button(g_smartshunt_screen, "BACK", settings_button_cb, 120, 48);
    lv_obj_align(back, LV_ALIGN_BOTTOM_RIGHT, -44, -18);

    g_keyboard = lv_keyboard_create(g_smartshunt_screen);
    lv_obj_set_size(g_keyboard, 460, 205);
    lv_obj_align(g_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(g_keyboard, keyboard_event_cb, LV_EVENT_ALL, nullptr);
    lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
}
}

void ui_start(AppSettings initial_settings)
{
    g_settings = initial_settings;

    create_depth_screen();
    create_battery_screen();
    create_settings_screen();
    create_smartshunt_screen();
    update_settings_labels();
    apply_theme();
    lv_screen_load(g_depth_screen);

    g_battery_timer = lv_timer_create(update_battery_labels, 500, nullptr);
    update_battery_labels(nullptr);
}
