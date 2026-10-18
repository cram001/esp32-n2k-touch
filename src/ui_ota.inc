// Included in ui.cpp private namespace; LVGL task callbacks.
void update_ota_status() {
    if(!g_ota_status || lv_screen_active()!=g_ota_screen) return;
    if(g_ota_feedback[0] && lv_tick_elaps(g_ota_feedback_time)<6000) return;
    g_ota_feedback[0]=0;
    const auto st=ota_get_status();const auto wifi=wifi_service_get_status();const auto server=ota_local_server_status();char text[360];char pin[7]{};ota_local_server_pin(pin,sizeof(pin));
    const uint32_t remaining=ota_local_server_seconds_remaining();
    if(g_ota_web_button) lv_label_set_text(button_label(g_ota_web_button),
        ota_local_server_window_active()?"DISABLE WEB UPDATE":"ENABLE WEB UPDATE (120s)");
    if(server.stage==LocalServerStage::Listening) {
        std::snprintf(text,sizeof(text),"Version: %s\n%s (%d%%)  error: 0x%x\nWeb: ACTIVE - %us remaining\nLocal page: http://%s\nPIN: %s\nOTA layout: %s",ota_running_version(),st.message[0]?st.message:"Ready for local upload",st.progress_percent,st.last_error,static_cast<unsigned>(remaining),wifi.ip[0]?wifi.ip.data():"unavailable",pin,ota_partition_layout_valid()?"ready":"incompatible; use USB");
    } else {
        std::snprintf(text,sizeof(text),"Version: %s\n%s (%d%%)  error: 0x%x\nWeb: %s%s\nOTA layout: %s",ota_running_version(),st.message[0]?st.message:"Enter HTTPS URL or enable local web update",st.progress_percent,st.last_error,
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
        else ota_feedback("Stopping web update");
        return;
    }
    if(wifi_service_get_status().state!=WifiState::Connected) {
        ota_feedback("Connect to Wi-Fi in Station mode first");
        return;
    }
    if(!ota_enable_local_server(120)) ota_feedback("Could not start web update window");
    else ota_feedback("Starting web update for up to 120 seconds");
}
void ota_reboot_cb(lv_event_t *) {if(ota_get_status().state==OtaState::ReadyToReboot)esp_restart();}
void ota_focus_cb(lv_event_t *) {lv_keyboard_set_textarea(g_ota_keyboard,g_ota_url);lv_obj_remove_flag(g_ota_keyboard,LV_OBJ_FLAG_HIDDEN);}
void ota_keyboard_cb(lv_event_t *e) {if(lv_event_get_code(e)==LV_EVENT_READY||lv_event_get_code(e)==LV_EVENT_CANCEL)lv_obj_add_flag(g_ota_keyboard,LV_OBJ_FLAG_HIDDEN);}
