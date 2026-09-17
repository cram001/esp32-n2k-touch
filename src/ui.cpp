#include "ui.hpp"

#include <cstdint>
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
size_t g_active_shunt = 0;
size_t g_edit_shunt = 0;

lv_obj_t *g_depth_screen = nullptr;
lv_obj_t *g_battery_screen = nullptr;
lv_obj_t *g_settings_screen = nullptr;
lv_obj_t *g_shunt_manager_screen = nullptr;
lv_obj_t *g_discovery_screen = nullptr;
lv_obj_t *g_editor_screen = nullptr;

lv_obj_t *g_brightness_slider = nullptr;
lv_obj_t *g_brightness_value = nullptr;
lv_obj_t *g_theme_value = nullptr;

lv_obj_t *g_battery_title = nullptr;
lv_obj_t *g_soc_value = nullptr;
lv_obj_t *g_voltage_value = nullptr;
lv_obj_t *g_current_value = nullptr;
lv_obj_t *g_consumed_value = nullptr;
lv_obj_t *g_ttg_value = nullptr;
lv_obj_t *g_battery_status = nullptr;

lv_obj_t *g_manager_list = nullptr;
lv_obj_t *g_discovery_list = nullptr;
lv_obj_t *g_editor_title = nullptr;
lv_obj_t *g_name_textarea = nullptr;
lv_obj_t *g_key_textarea = nullptr;
lv_obj_t *g_enabled_switch = nullptr;
lv_obj_t *g_n2k_switch = nullptr;
lv_obj_t *g_instance_value = nullptr;
lv_obj_t *g_editor_warning = nullptr;
lv_obj_t *g_keyboard = nullptr;

lv_timer_t *g_battery_timer = nullptr;

uint8_t active_brightness()
{
    return g_settings.theme == DisplayTheme::Day ? g_settings.day_brightness : g_settings.night_brightness;
}

size_t configured_count()
{
    size_t count = 0;
    for (const auto &s : g_settings.smartshunts) if (s.configured) ++count;
    return count;
}

size_t first_configured()
{
    for (size_t i = 0; i < g_settings.smartshunts.size(); ++i) if (g_settings.smartshunts[i].configured) return i;
    return 0;
}

size_t adjacent_configured(size_t from, int direction)
{
    if (configured_count() == 0) return 0;
    for (size_t step = 1; step <= g_settings.smartshunts.size(); ++step) {
        int candidate = static_cast<int>(from) + direction * static_cast<int>(step);
        while (candidate < 0) candidate += static_cast<int>(g_settings.smartshunts.size());
        candidate %= static_cast<int>(g_settings.smartshunts.size());
        if (g_settings.smartshunts[static_cast<size_t>(candidate)].configured) return static_cast<size_t>(candidate);
    }
    return from;
}

uint8_t first_free_instance(size_t ignore = MAX_SMARTSHUNTS)
{
    for (uint16_t candidate = 0; candidate <= 252; ++candidate) {
        bool used = false;
        for (size_t i = 0; i < g_settings.smartshunts.size(); ++i) {
            if (i == ignore) continue;
            const auto &s = g_settings.smartshunts[i];
            if (s.configured && s.n2k_enabled && s.battery_instance == candidate) { used = true; break; }
        }
        if (!used) return static_cast<uint8_t>(candidate);
    }
    return 0;
}

bool duplicate_instance(size_t index)
{
    if (index >= g_settings.smartshunts.size()) return false;
    const auto &s = g_settings.smartshunts[index];
    if (!s.configured || !s.n2k_enabled) return false;
    for (size_t i = 0; i < g_settings.smartshunts.size(); ++i) {
        if (i == index) continue;
        const auto &other = g_settings.smartshunts[i];
        if (other.configured && other.n2k_enabled && other.battery_instance == s.battery_instance) return true;
    }
    return false;
}

void apply_backlight()
{
    const esp_err_t err = bsp_display_brightness_set(active_brightness());
    if (err != ESP_OK) ESP_LOGW(TAG, "Unable to set display brightness: %s", esp_err_to_name(err));
}

void theme_screen(lv_obj_t *screen)
{
    if (!screen) return;
    const bool night = g_settings.theme == DisplayTheme::Night;
    lv_obj_set_style_bg_color(screen, night ? lv_color_hex(0x050000) : lv_color_hex(0xF5F7FA), 0);
    lv_obj_set_style_text_color(screen, night ? lv_color_hex(0xFF5A45) : lv_color_hex(0x101418), 0);
}

void apply_theme()
{
    theme_screen(g_depth_screen); theme_screen(g_battery_screen); theme_screen(g_settings_screen);
    theme_screen(g_shunt_manager_screen); theme_screen(g_discovery_screen); theme_screen(g_editor_screen);
    apply_backlight();
}

void apply_runtime_settings()
{
    smartshunt_ble_apply_settings(g_settings);
    n2k_bridge_apply_settings(g_settings);
}

void persist_and_apply()
{
    if (!settings_save(g_settings)) ESP_LOGW(TAG, "Settings changed but could not be persisted");
    apply_runtime_settings();
    apply_theme();
}

lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, int width = 150, int height = 52, void *user = nullptr)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_center(label);
    return button;
}

void update_display_settings()
{
    if (g_theme_value) lv_label_set_text(g_theme_value, g_settings.theme == DisplayTheme::Day ? "DAY" : "NIGHT");
    if (g_brightness_slider) lv_slider_set_value(g_brightness_slider, active_brightness(), LV_ANIM_OFF);
    if (g_brightness_value) {
        char b[8]; std::snprintf(b, sizeof(b), "%u%%", active_brightness()); lv_label_set_text(g_brightness_value, b);
    }
}

void depth_button_cb(lv_event_t *) { lv_screen_load(g_depth_screen); }
void settings_button_cb(lv_event_t *) { update_display_settings(); lv_screen_load(g_settings_screen); }

void blank_battery()
{
    lv_label_set_text(g_soc_value, "--.- %");
    lv_label_set_text(g_voltage_value, "--.-- V");
    lv_label_set_text(g_current_value, "--.-- A");
    lv_label_set_text(g_consumed_value, "Used: --.- Ah");
    lv_label_set_text(g_ttg_value, "TTG: --");
}

void update_battery_labels(lv_timer_t *)
{
    if (!g_battery_title) return;
    if (configured_count() == 0) {
        lv_label_set_text(g_battery_title, "NO SMARTSHUNT");
        blank_battery();
        lv_label_set_text(g_battery_status, "Settings > SmartShunts > Add SmartShunt");
        return;
    }
    if (!g_settings.smartshunts[g_active_shunt].configured) g_active_shunt = first_configured();
    const auto &cfg = g_settings.smartshunts[g_active_shunt];
    lv_label_set_text(g_battery_title, cfg.name[0] ? cfg.name.data() : "SMARTSHUNT");

    if (!cfg.enabled) {
        blank_battery();
        lv_label_set_text(g_battery_status, "Reading disabled for this SmartShunt");
        return;
    }

    const SmartShuntData d = smartshunt_ble_get_data(g_active_shunt);
    if (!d.valid) {
        blank_battery();
        lv_label_set_text(g_battery_status, d.key_valid ? "Waiting for SmartShunt..." : "SmartShunt found - check Instant Readout key");
        return;
    }

    char b[80];
    if (d.soc_valid) { std::snprintf(b, sizeof(b), "%.1f %%", d.soc_pct); lv_label_set_text(g_soc_value, b); } else lv_label_set_text(g_soc_value, "--.- %");
    if (d.voltage_valid) { std::snprintf(b, sizeof(b), "%.2f V", d.voltage_v); lv_label_set_text(g_voltage_value, b); } else lv_label_set_text(g_voltage_value, "--.-- V");
    if (d.current_valid) { std::snprintf(b, sizeof(b), "%+.2f A", d.current_a); lv_label_set_text(g_current_value, b); } else lv_label_set_text(g_current_value, "--.-- A");
    if (d.consumed_ah_valid) { std::snprintf(b, sizeof(b), "Used: %.1f Ah", d.consumed_ah); lv_label_set_text(g_consumed_value, b); } else lv_label_set_text(g_consumed_value, "Used: --.- Ah");
    if (d.time_to_go_valid) { std::snprintf(b, sizeof(b), "TTG: %uh %02um", d.time_to_go_min / 60U, d.time_to_go_min % 60U); lv_label_set_text(g_ttg_value, b); } else lv_label_set_text(g_ttg_value, "TTG: --");

    if (d.stale) std::snprintf(b, sizeof(b), "BLE stale | %lu ms", static_cast<unsigned long>(d.age_ms));
    else if (duplicate_instance(g_active_shunt)) std::snprintf(b, sizeof(b), "BLE %d dBm | N2K instance conflict", d.rssi);
    else if (cfg.n2k_enabled) std::snprintf(b, sizeof(b), "BLE %d dBm | N2K battery %u", d.rssi, cfg.battery_instance);
    else std::snprintf(b, sizeof(b), "BLE %d dBm | N2K bridge off", d.rssi);
    lv_label_set_text(g_battery_status, b);
}

void battery_button_cb(lv_event_t *)
{
    if (configured_count() && !g_settings.smartshunts[g_active_shunt].configured) g_active_shunt = first_configured();
    update_battery_labels(nullptr);
    lv_screen_load(g_battery_screen);
}

void previous_battery_cb(lv_event_t *)
{
    g_active_shunt = adjacent_configured(g_active_shunt, -1);
    update_battery_labels(nullptr);
}

void next_battery_cb(lv_event_t *)
{
    g_active_shunt = adjacent_configured(g_active_shunt, 1);
    update_battery_labels(nullptr);
}

void day_cb(lv_event_t *) { g_settings.theme = DisplayTheme::Day; update_display_settings(); persist_and_apply(); }
void night_cb(lv_event_t *) { g_settings.theme = DisplayTheme::Night; update_display_settings(); persist_and_apply(); }
void brightness_cb(lv_event_t *e)
{
    int32_t v = lv_slider_get_value(lv_event_get_target_obj(e));
    const uint8_t value = static_cast<uint8_t>(v < 1 ? 1 : (v > 100 ? 100 : v));
    if (g_settings.theme == DisplayTheme::Day) g_settings.day_brightness = value; else g_settings.night_brightness = value;
    update_display_settings(); persist_and_apply();
}

void editor_refresh()
{
    if (g_edit_shunt >= g_settings.smartshunts.size()) return;
    const auto &s = g_settings.smartshunts[g_edit_shunt];
    lv_label_set_text(g_editor_title, s.name[0] ? s.name.data() : "SMARTSHUNT");
    lv_textarea_set_text(g_name_textarea, s.name.data());
    lv_textarea_set_text(g_key_textarea, s.bindkey.data());
    if (s.enabled) lv_obj_add_state(g_enabled_switch, LV_STATE_CHECKED); else lv_obj_remove_state(g_enabled_switch, LV_STATE_CHECKED);
    if (s.n2k_enabled) lv_obj_add_state(g_n2k_switch, LV_STATE_CHECKED); else lv_obj_remove_state(g_n2k_switch, LV_STATE_CHECKED);
    char b[8]; std::snprintf(b, sizeof(b), "%u", s.battery_instance); lv_label_set_text(g_instance_value, b);
    lv_label_set_text(g_editor_warning, duplicate_instance(g_edit_shunt) ? "NMEA instance already used by another SmartShunt" : "");
}

void keyboard_cb(lv_event_t *e)
{
    const auto code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(g_keyboard, nullptr);
    }
}

void textarea_focus_cb(lv_event_t *e)
{
    lv_keyboard_set_textarea(g_keyboard, lv_event_get_target_obj(e));
    lv_obj_remove_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_keyboard);
}

void instance_minus_cb(lv_event_t *)
{
    auto &s = g_settings.smartshunts[g_edit_shunt];
    if (s.battery_instance > 0) --s.battery_instance;
    editor_refresh();
}

void instance_plus_cb(lv_event_t *)
{
    auto &s = g_settings.smartshunts[g_edit_shunt];
    if (s.battery_instance < 252) ++s.battery_instance;
    editor_refresh();
}

void save_editor_cb(lv_event_t *)
{
    auto &s = g_settings.smartshunts[g_edit_shunt];
    std::snprintf(s.name.data(), s.name.size(), "%s", lv_textarea_get_text(g_name_textarea));
    std::snprintf(s.bindkey.data(), s.bindkey.size(), "%s", lv_textarea_get_text(g_key_textarea));
    s.enabled = lv_obj_has_state(g_enabled_switch, LV_STATE_CHECKED);
    s.n2k_enabled = lv_obj_has_state(g_n2k_switch, LV_STATE_CHECKED);
    if (!s.enabled) s.n2k_enabled = false;
    persist_and_apply();
    editor_refresh();
}

void delete_editor_cb(lv_event_t *)
{
    g_settings.smartshunts[g_edit_shunt] = {};
    if (g_active_shunt == g_edit_shunt) g_active_shunt = first_configured();
    persist_and_apply();
    lv_screen_load(g_shunt_manager_screen);
}

void open_editor(size_t index)
{
    g_edit_shunt = index;
    editor_refresh();
    lv_screen_load(g_editor_screen);
}

void manager_device_cb(lv_event_t *e)
{
    const size_t index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)) - 1U;
    if (index < g_settings.smartshunts.size()) open_editor(index);
}

void rebuild_manager_list()
{
    lv_obj_clean(g_manager_list);
    size_t row = 0;
    for (size_t i = 0; i < g_settings.smartshunts.size(); ++i) {
        const auto &s = g_settings.smartshunts[i];
        if (!s.configured) continue;
        char label[48];
        std::snprintf(label, sizeof(label), "%s%s", s.name[0] ? s.name.data() : "SmartShunt", s.enabled ? "" : " (off)");
        lv_obj_t *button = make_button(g_manager_list, label, manager_device_cb, 380, 48, reinterpret_cast<void *>(i + 1U));
        lv_obj_align(button, LV_ALIGN_TOP_MID, 0, static_cast<int>(row * 55));
        ++row;
    }
    if (row == 0) {
        lv_obj_t *empty = lv_label_create(g_manager_list);
        lv_label_set_text(empty, "No SmartShunts configured");
        lv_obj_align(empty, LV_ALIGN_TOP_MID, 0, 12);
    }
}

void manager_button_cb(lv_event_t *)
{
    rebuild_manager_list();
    lv_screen_load(g_shunt_manager_screen);
}

void discovery_select_cb(lv_event_t *e)
{
    const size_t discovered_index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)) - 1U;
    std::array<DiscoveredSmartShunt, MAX_DISCOVERED_SMARTSHUNTS> devices{};
    const size_t count = smartshunt_ble_get_discovered(devices);
    if (discovered_index >= count) return;

    size_t slot = MAX_SMARTSHUNTS;
    for (size_t i = 0; i < g_settings.smartshunts.size(); ++i) {
        if (g_settings.smartshunts[i].configured && std::strcmp(g_settings.smartshunts[i].mac.data(), devices[discovered_index].mac.data()) == 0) {
            slot = i;
            break;
        }
        if (!g_settings.smartshunts[i].configured && slot == MAX_SMARTSHUNTS) slot = i;
    }
    if (slot >= MAX_SMARTSHUNTS) return;

    auto &cfg = g_settings.smartshunts[slot];
    if (!cfg.configured) {
        cfg = {};
        cfg.configured = true;
        cfg.enabled = true;
        cfg.battery_instance = first_free_instance(slot);
        std::snprintf(cfg.name.data(), cfg.name.size(), "%s", devices[discovered_index].name.data());
        std::snprintf(cfg.mac.data(), cfg.mac.size(), "%s", devices[discovered_index].mac.data());
    }
    persist_and_apply();
    open_editor(slot);
}

void rebuild_discovery_list()
{
    lv_obj_clean(g_discovery_list);
    std::array<DiscoveredSmartShunt, MAX_DISCOVERED_SMARTSHUNTS> devices{};
    const size_t count = smartshunt_ble_get_discovered(devices);
    if (count == 0) {
        lv_obj_t *empty = lv_label_create(g_discovery_list);
        lv_label_set_text(empty, "Scanning for nearby SmartShunts...");
        lv_obj_align(empty, LV_ALIGN_TOP_MID, 0, 12);
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        char label[42];
        std::snprintf(label, sizeof(label), "%s  (%d dBm)", devices[i].name.data(), devices[i].rssi);
        lv_obj_t *button = make_button(g_discovery_list, label, discovery_select_cb, 390, 48, reinterpret_cast<void *>(i + 1U));
        lv_obj_align(button, LV_ALIGN_TOP_MID, 0, static_cast<int>(i * 55));
    }
}

void add_shunt_cb(lv_event_t *)
{
    rebuild_discovery_list();
    lv_screen_load(g_discovery_screen);
}

void refresh_discovery_cb(lv_event_t *) { rebuild_discovery_list(); }

void create_depth_screen()
{
    g_depth_screen = lv_obj_create(nullptr); lv_obj_remove_flag(g_depth_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lv_label_create(g_depth_screen); lv_label_set_text(title, "DEPTH"); lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0); lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_t *depth = lv_label_create(g_depth_screen); lv_label_set_text(depth, "--.- m"); lv_obj_set_style_text_font(depth, &lv_font_montserrat_48, 0); lv_obj_align(depth, LV_ALIGN_CENTER, 0, -40);
    lv_obj_t *status = lv_label_create(g_depth_screen); lv_label_set_text(status, "NMEA 2000 depth not connected"); lv_obj_align(status, LV_ALIGN_CENTER, 0, 24);
    lv_obj_align(make_button(g_depth_screen, "BATTERY", battery_button_cb, 160, 56), LV_ALIGN_BOTTOM_LEFT, 45, -34);
    lv_obj_align(make_button(g_depth_screen, "SETTINGS", settings_button_cb, 160, 56), LV_ALIGN_BOTTOM_RIGHT, -45, -34);
}

void create_battery_screen()
{
    g_battery_screen = lv_obj_create(nullptr); lv_obj_remove_flag(g_battery_screen, LV_OBJ_FLAG_SCROLLABLE);
    g_battery_title = lv_label_create(g_battery_screen); lv_obj_set_style_text_font(g_battery_title, &lv_font_montserrat_24, 0); lv_obj_align(g_battery_title, LV_ALIGN_TOP_MID, 0, 20);
    g_soc_value = lv_label_create(g_battery_screen); lv_obj_set_style_text_font(g_soc_value, &lv_font_montserrat_48, 0); lv_obj_align(g_soc_value, LV_ALIGN_TOP_MID, 0, 62);
    g_voltage_value = lv_label_create(g_battery_screen); lv_obj_set_style_text_font(g_voltage_value, &lv_font_montserrat_32, 0); lv_obj_align(g_voltage_value, LV_ALIGN_TOP_LEFT, 55, 140);
    g_current_value = lv_label_create(g_battery_screen); lv_obj_set_style_text_font(g_current_value, &lv_font_montserrat_32, 0); lv_obj_align(g_current_value, LV_ALIGN_TOP_RIGHT, -55, 140);
    g_consumed_value = lv_label_create(g_battery_screen); lv_obj_set_style_text_font(g_consumed_value, &lv_font_montserrat_20, 0); lv_obj_align(g_consumed_value, LV_ALIGN_TOP_LEFT, 55, 205);
    g_ttg_value = lv_label_create(g_battery_screen); lv_obj_set_style_text_font(g_ttg_value, &lv_font_montserrat_20, 0); lv_obj_align(g_ttg_value, LV_ALIGN_TOP_RIGHT, -55, 205);
    g_battery_status = lv_label_create(g_battery_screen); lv_obj_align(g_battery_status, LV_ALIGN_TOP_MID, 0, 265);
    lv_obj_align(make_button(g_battery_screen, "<", previous_battery_cb, 70, 48), LV_ALIGN_BOTTOM_LEFT, 20, -30);
    lv_obj_align(make_button(g_battery_screen, "DEPTH", depth_button_cb, 120, 48), LV_ALIGN_BOTTOM_MID, -65, -30);
    lv_obj_align(make_button(g_battery_screen, "SETTINGS", settings_button_cb, 140, 48), LV_ALIGN_BOTTOM_MID, 75, -30);
    lv_obj_align(make_button(g_battery_screen, ">", next_battery_cb, 70, 48), LV_ALIGN_BOTTOM_RIGHT, -20, -30);
}

void create_settings_screen()
{
    g_settings_screen = lv_obj_create(nullptr); lv_obj_remove_flag(g_settings_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lv_label_create(g_settings_screen); lv_label_set_text(title, "SETTINGS"); lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0); lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_t *theme = lv_label_create(g_settings_screen); lv_label_set_text(theme, "Theme"); lv_obj_align(theme, LV_ALIGN_TOP_LEFT, 38, 72);
    g_theme_value = lv_label_create(g_settings_screen); lv_obj_align(g_theme_value, LV_ALIGN_TOP_RIGHT, -38, 72);
    lv_obj_align(make_button(g_settings_screen, "DAY", day_cb, 145, 50), LV_ALIGN_TOP_LEFT, 60, 105);
    lv_obj_align(make_button(g_settings_screen, "NIGHT", night_cb, 145, 50), LV_ALIGN_TOP_RIGHT, -60, 105);
    lv_obj_t *br = lv_label_create(g_settings_screen); lv_label_set_text(br, "Brightness"); lv_obj_align(br, LV_ALIGN_TOP_LEFT, 38, 178);
    g_brightness_value = lv_label_create(g_settings_screen); lv_obj_align(g_brightness_value, LV_ALIGN_TOP_RIGHT, -38, 178);
    g_brightness_slider = lv_slider_create(g_settings_screen); lv_obj_set_size(g_brightness_slider, 380, 26); lv_slider_set_range(g_brightness_slider, 1, 100); lv_obj_align(g_brightness_slider, LV_ALIGN_TOP_MID, 0, 216); lv_obj_add_event_cb(g_brightness_slider, brightness_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_align(make_button(g_settings_screen, "SMARTSHUNTS", manager_button_cb, 220, 56), LV_ALIGN_TOP_MID, 0, 280);
    lv_obj_align(make_button(g_settings_screen, "BACK", depth_button_cb, 150, 52), LV_ALIGN_BOTTOM_MID, 0, -24);
}

void create_manager_screen()
{
    g_shunt_manager_screen = lv_obj_create(nullptr); lv_obj_remove_flag(g_shunt_manager_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lv_label_create(g_shunt_manager_screen); lv_label_set_text(title, "SMARTSHUNTS"); lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0); lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);
    g_manager_list = lv_obj_create(g_shunt_manager_screen); lv_obj_set_size(g_manager_list, 420, 265); lv_obj_align(g_manager_list, LV_ALIGN_TOP_MID, 0, 55); lv_obj_set_scroll_dir(g_manager_list, LV_DIR_VER);
    lv_obj_align(make_button(g_shunt_manager_screen, "ADD SMARTSHUNT", add_shunt_cb, 210, 48), LV_ALIGN_BOTTOM_LEFT, 25, -18);
    lv_obj_align(make_button(g_shunt_manager_screen, "BACK", settings_button_cb, 120, 48), LV_ALIGN_BOTTOM_RIGHT, -25, -18);
}

void create_discovery_screen()
{
    g_discovery_screen = lv_obj_create(nullptr); lv_obj_remove_flag(g_discovery_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lv_label_create(g_discovery_screen); lv_label_set_text(title, "NEARBY SMARTSHUNTS"); lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0); lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);
    g_discovery_list = lv_obj_create(g_discovery_screen); lv_obj_set_size(g_discovery_list, 425, 300); lv_obj_align(g_discovery_list, LV_ALIGN_TOP_MID, 0, 55); lv_obj_set_scroll_dir(g_discovery_list, LV_DIR_VER);
    lv_obj_align(make_button(g_discovery_screen, "REFRESH", refresh_discovery_cb, 140, 48), LV_ALIGN_BOTTOM_LEFT, 28, -18);
    lv_obj_align(make_button(g_discovery_screen, "BACK", manager_button_cb, 120, 48), LV_ALIGN_BOTTOM_RIGHT, -28, -18);
}

void create_editor_screen()
{
    g_editor_screen = lv_obj_create(nullptr); lv_obj_remove_flag(g_editor_screen, LV_OBJ_FLAG_SCROLLABLE);
    g_editor_title = lv_label_create(g_editor_screen); lv_obj_set_style_text_font(g_editor_title, &lv_font_montserrat_24, 0); lv_obj_align(g_editor_title, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *name_label = lv_label_create(g_editor_screen); lv_label_set_text(name_label, "Name"); lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 28, 52);
    g_name_textarea = lv_textarea_create(g_editor_screen); lv_obj_set_size(g_name_textarea, 300, 42); lv_textarea_set_one_line(g_name_textarea, true); lv_textarea_set_max_length(g_name_textarea, 24); lv_obj_align(g_name_textarea, LV_ALIGN_TOP_RIGHT, -28, 43); lv_obj_add_event_cb(g_name_textarea, textarea_focus_cb, LV_EVENT_FOCUSED, nullptr);

    lv_obj_t *read_label = lv_label_create(g_editor_screen); lv_label_set_text(read_label, "Read this SmartShunt"); lv_obj_align(read_label, LV_ALIGN_TOP_LEFT, 28, 101);
    g_enabled_switch = lv_switch_create(g_editor_screen); lv_obj_align(g_enabled_switch, LV_ALIGN_TOP_RIGHT, -30, 91);

    lv_obj_t *n2k_label = lv_label_create(g_editor_screen); lv_label_set_text(n2k_label, "Send to NMEA 2000"); lv_obj_align(n2k_label, LV_ALIGN_TOP_LEFT, 28, 145);
    g_n2k_switch = lv_switch_create(g_editor_screen); lv_obj_align(g_n2k_switch, LV_ALIGN_TOP_RIGHT, -30, 135);

    lv_obj_t *inst_label = lv_label_create(g_editor_screen); lv_label_set_text(inst_label, "Battery instance"); lv_obj_align(inst_label, LV_ALIGN_TOP_LEFT, 28, 190);
    lv_obj_align(make_button(g_editor_screen, "-", instance_minus_cb, 52, 38), LV_ALIGN_TOP_RIGHT, -132, 174);
    g_instance_value = lv_label_create(g_editor_screen); lv_obj_set_style_text_font(g_instance_value, &lv_font_montserrat_20, 0); lv_obj_align(g_instance_value, LV_ALIGN_TOP_RIGHT, -92, 184);
    lv_obj_align(make_button(g_editor_screen, "+", instance_plus_cb, 52, 38), LV_ALIGN_TOP_RIGHT, -28, 174);

    lv_obj_t *key_label = lv_label_create(g_editor_screen); lv_label_set_text(key_label, "Instant Readout key"); lv_obj_align(key_label, LV_ALIGN_TOP_LEFT, 28, 235);
    g_key_textarea = lv_textarea_create(g_editor_screen); lv_obj_set_size(g_key_textarea, 424, 42); lv_textarea_set_one_line(g_key_textarea, true); lv_textarea_set_max_length(g_key_textarea, 32); lv_textarea_set_password_mode(g_key_textarea, true); lv_obj_align(g_key_textarea, LV_ALIGN_TOP_MID, 0, 257); lv_obj_add_event_cb(g_key_textarea, textarea_focus_cb, LV_EVENT_FOCUSED, nullptr);

    g_editor_warning = lv_label_create(g_editor_screen); lv_obj_set_style_text_font(g_editor_warning, &lv_font_montserrat_14, 0); lv_obj_align(g_editor_warning, LV_ALIGN_TOP_MID, 0, 308);

    lv_obj_align(make_button(g_editor_screen, "SAVE", save_editor_cb, 105, 46), LV_ALIGN_BOTTOM_LEFT, 20, -18);
    lv_obj_align(make_button(g_editor_screen, "DELETE", delete_editor_cb, 105, 46), LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_align(make_button(g_editor_screen, "BACK", manager_button_cb, 105, 46), LV_ALIGN_BOTTOM_RIGHT, -20, -18);

    g_keyboard = lv_keyboard_create(g_editor_screen); lv_obj_set_size(g_keyboard, 460, 205); lv_obj_align(g_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0); lv_obj_add_event_cb(g_keyboard, keyboard_cb, LV_EVENT_ALL, nullptr); lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
}
}

void ui_start(AppSettings initial_settings)
{
    g_settings = initial_settings;
    g_active_shunt = first_configured();
    create_depth_screen(); create_battery_screen(); create_settings_screen(); create_manager_screen(); create_discovery_screen(); create_editor_screen();
    update_display_settings(); apply_theme(); rebuild_manager_list(); update_battery_labels(nullptr); lv_screen_load(g_depth_screen);
    g_battery_timer = lv_timer_create(update_battery_labels, 500, nullptr);
}
