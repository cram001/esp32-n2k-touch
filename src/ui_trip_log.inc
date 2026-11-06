// Included in ui.cpp's private namespace; all access is on the LVGL task.
lv_obj_t *g_trip_screen = nullptr;
lv_obj_t *g_trip_back = nullptr;
lv_obj_t *g_trip_total = nullptr;
lv_obj_t *g_trip_status = nullptr;
lv_obj_t *g_trip_enabled = nullptr;
lv_obj_t *g_trip_source = nullptr;
lv_obj_t *g_trip_feedback = nullptr;

void update_trip_screen() {
    if (!g_trip_screen || lv_screen_active() != g_trip_screen) return;
    const auto config = g_trip_log.config();
    char value[32], unit[12], total[48];
    DataFieldSelection field{};
    field.metric = DataMetric::LocalTripDistance;
    instrument_format_value(field, g_settings.units, field_value(field, 255),
                            value, sizeof(value), unit, sizeof(unit));
    std::snprintf(total, sizeof(total), "%s %s", value, unit);
    if (std::strcmp(lv_label_get_text(g_trip_total), total) != 0)
        lv_label_set_text(g_trip_total, total);
    const char *status = g_trip_log.checkpoint_failed() ? "Trip save failed; will retry" :
        !config.enabled ? "Paused" :
        (g_trip_log.receiving() ? "Logging distance" : "Waiting for fresh speed data");
    if (lv_label_get_text(g_trip_status) != status)
        lv_label_set_text_static(g_trip_status, status);
}

void trip_settings_changed(TripLogConfig config) {
    if (!g_trip_log.save_settings(config, esp_timer_get_time())) {
        lv_label_set_text(g_trip_feedback, "Save failed. Previous setting kept.");
        return;
    }
    lv_label_set_text(button_label(g_trip_enabled), config.enabled ? "Logging: ON" : "Logging: OFF");
    lv_label_set_text(button_label(g_trip_source), config.source == TripSpeed::Stw ? "Source: STW" : "Source: GPS SOG");
    lv_label_set_text(g_trip_feedback, "");
    update_trip_screen();
}

void trip_log_screen_cb(lv_event_t *) {
    g_trip_back = lv_screen_active();
    if (!g_trip_screen) {
        g_trip_screen = require_obj(lv_obj_create(nullptr), "trip log screen");
        lv_obj_remove_flag(g_trip_screen, LV_OBJ_FLAG_SCROLLABLE);
        auto label = [](const char *text, int y) {
            auto *l = require_obj(lv_label_create(g_trip_screen), "trip log label");
            lv_label_set_text(l, text);
            lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
            return l;
        };
        lv_obj_set_style_text_font(label("Trip Log", 20), &lv_font_montserrat_24, 0);
        g_trip_total = label("", 78);
        lv_obj_set_style_text_font(g_trip_total, &lv_font_montserrat_48, 0);
        g_trip_status = label("", 145);
        g_trip_enabled = make_button(g_trip_screen, "", [](lv_event_t *) {
            auto config = g_trip_log.config();
            config.enabled = !config.enabled;
            trip_settings_changed(config);
        }, 196, 52);
        lv_obj_align(g_trip_enabled, LV_ALIGN_TOP_LEFT, 34, 190);
        g_trip_source = make_button(g_trip_screen, "", [](lv_event_t *) {
            auto config = g_trip_log.config();
            config.source = config.source == TripSpeed::Stw ? TripSpeed::Sog : TripSpeed::Stw;
            trip_settings_changed(config);
        }, 196, 52);
        lv_obj_align(g_trip_source, LV_ALIGN_TOP_RIGHT, -34, 190);
        auto *hint = label("STW: speed through water. SOG: GPS speed over ground.\n"
                           "Distance is saved about once a minute.", 263);
        lv_obj_set_width(hint, 420);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        g_trip_feedback = label("", 337);
        auto *reset = make_button(g_trip_screen, "RESET TO ZERO", [](lv_event_t *) {
            if (!g_trip_log.save_reset(esp_timer_get_time())) {
                lv_label_set_text(g_trip_feedback, "Reset save failed. Trip kept.");
                return;
            }
            lv_label_set_text(g_trip_feedback, "Trip reset");
            update_trip_screen();
        }, 190, 48);
        lv_obj_align(reset, LV_ALIGN_BOTTOM_LEFT, 34, -18);
        auto *back = make_button(g_trip_screen, "BACK", [](lv_event_t *) {
            lv_screen_load(g_trip_back);
        }, 120, 48);
        lv_obj_align(back, LV_ALIGN_BOTTOM_RIGHT, -34, -18);
    }
    const auto config = g_trip_log.config();
    lv_label_set_text(button_label(g_trip_enabled), config.enabled ? "Logging: ON" : "Logging: OFF");
    lv_label_set_text(button_label(g_trip_source), config.source == TripSpeed::Stw ? "Source: STW" : "Source: GPS SOG");
    lv_label_set_text(g_trip_feedback, "");
    apply_theme_to(g_trip_screen);
    lv_screen_load(g_trip_screen);
    update_trip_screen();
}

void sample_trip_log() {
    if (g_trip_log.config().enabled) {
        InstrumentValue speed{};
        n2k_sources_get(g_trip_log.speed_metric(), 255, speed);
        g_trip_log.sample(esp_timer_get_time(), speed);
    }
    if (!g_trip_log.checkpoint(esp_timer_get_time())) ESP_LOGW(TAG, "Trip log checkpoint failed");
    update_trip_screen();
}
