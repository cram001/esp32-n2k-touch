#include "ui.hpp"

#include <cstdio>

#include "bsp/display.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"

namespace {
constexpr const char *TAG = "ui";

AppSettings g_settings;
lv_obj_t *g_depth_screen = nullptr;
lv_obj_t *g_settings_screen = nullptr;
lv_obj_t *g_brightness_slider = nullptr;
lv_obj_t *g_brightness_value = nullptr;
lv_obj_t *g_theme_value = nullptr;

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
    if (screen == nullptr) {
        return;
    }

    const bool night = g_settings.theme == DisplayTheme::Night;
    const lv_color_t bg = night ? lv_color_hex(0x050000) : lv_color_hex(0xF5F7FA);
    const lv_color_t fg = night ? lv_color_hex(0xFF5A45) : lv_color_hex(0x101418);
    lv_obj_set_style_bg_color(screen, bg, 0);
    lv_obj_set_style_text_color(screen, fg, 0);
}

void apply_theme()
{
    apply_theme_to_screen(g_depth_screen);
    apply_theme_to_screen(g_settings_screen);
    apply_backlight();
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
}

void persist_and_apply()
{
    if (!settings_save(g_settings)) {
        ESP_LOGW(TAG, "Display settings changed but could not be persisted");
    }
    update_settings_labels();
    apply_theme();
}

void settings_button_cb(lv_event_t *)
{
    update_settings_labels();
    lv_screen_load(g_settings_screen);
}

void back_button_cb(lv_event_t *)
{
    lv_screen_load(g_depth_screen);
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
    const uint8_t brightness = static_cast<uint8_t>(value < 0 ? 0 : (value > 100 ? 100 : value));

    if (g_settings.theme == DisplayTheme::Day) {
        g_settings.day_brightness = brightness;
    } else {
        g_settings.night_brightness = brightness;
    }

    persist_and_apply();
}

lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 150, 56);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_center(label);
    return button;
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
    lv_obj_align(depth, LV_ALIGN_CENTER, 0, -28);

    lv_obj_t *status = lv_label_create(g_depth_screen);
    lv_label_set_text(status, "NMEA 2000 not connected");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 34);

    lv_obj_t *settings = make_button(g_depth_screen, "SETTINGS", settings_button_cb);
    lv_obj_align(settings, LV_ALIGN_BOTTOM_MID, 0, -34);
}

void create_settings_screen()
{
    g_settings_screen = lv_obj_create(nullptr);
    lv_obj_remove_flag(g_settings_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(g_settings_screen);
    lv_label_set_text(title, "DISPLAY SETTINGS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    lv_obj_t *theme_label = lv_label_create(g_settings_screen);
    lv_label_set_text(theme_label, "Theme");
    lv_obj_set_style_text_font(theme_label, &lv_font_montserrat_20, 0);
    lv_obj_align(theme_label, LV_ALIGN_TOP_LEFT, 38, 92);

    g_theme_value = lv_label_create(g_settings_screen);
    lv_obj_set_style_text_font(g_theme_value, &lv_font_montserrat_20, 0);
    lv_obj_align(g_theme_value, LV_ALIGN_TOP_RIGHT, -38, 92);

    lv_obj_t *day = make_button(g_settings_screen, "DAY", day_button_cb);
    lv_obj_align(day, LV_ALIGN_TOP_LEFT, 58, 130);

    lv_obj_t *night = make_button(g_settings_screen, "NIGHT", night_button_cb);
    lv_obj_align(night, LV_ALIGN_TOP_RIGHT, -58, 130);

    lv_obj_t *brightness_label = lv_label_create(g_settings_screen);
    lv_label_set_text(brightness_label, "Brightness");
    lv_obj_set_style_text_font(brightness_label, &lv_font_montserrat_20, 0);
    lv_obj_align(brightness_label, LV_ALIGN_TOP_LEFT, 38, 220);

    g_brightness_value = lv_label_create(g_settings_screen);
    lv_obj_set_style_text_font(g_brightness_value, &lv_font_montserrat_20, 0);
    lv_obj_align(g_brightness_value, LV_ALIGN_TOP_RIGHT, -38, 220);

    g_brightness_slider = lv_slider_create(g_settings_screen);
    lv_obj_set_size(g_brightness_slider, 380, 28);
    lv_slider_set_range(g_brightness_slider, 1, 100);
    lv_obj_align(g_brightness_slider, LV_ALIGN_TOP_MID, 0, 265);
    lv_obj_add_event_cb(g_brightness_slider, brightness_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *note = lv_label_create(g_settings_screen);
    lv_label_set_text(note, "Day and night brightness are stored separately.");
    lv_obj_set_style_text_font(note, &lv_font_montserrat_14, 0);
    lv_obj_align(note, LV_ALIGN_TOP_MID, 0, 315);

    lv_obj_t *back = make_button(g_settings_screen, "BACK", back_button_cb);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -28);
}
}

void ui_start(AppSettings initial_settings)
{
    g_settings = initial_settings;

    create_depth_screen();
    create_settings_screen();
    update_settings_labels();
    apply_theme();
    lv_screen_load(g_depth_screen);
}
