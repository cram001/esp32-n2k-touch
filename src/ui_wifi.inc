// LVGL task only; included in ui.cpp private namespace.
void wifi_textarea_focus_cb(lv_event_t *e){
    g_editor_keyboard.show(g_wifi_screen,lv_event_get_target_obj(e),LV_KEYBOARD_MODE_TEXT_LOWER,460,205);
}
void hide_wifi_keyboard(){g_editor_keyboard.hide(g_wifi_screen);}
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
    g_wifi_draft.mode=WifiMode::Station;
    std::snprintf(g_wifi_draft.ssid.data(),g_wifi_draft.ssid.size(),"%s",lv_textarea_get_text(g_wifi_ssid));
    std::snprintf(g_wifi_draft.password.data(),g_wifi_draft.password.size(),"%s",lv_textarea_get_text(g_wifi_password));
    g_wifi_draft.open_network=lv_obj_has_state(g_wifi_open,LV_STATE_CHECKED);
    return true;
}
void show_wifi_draft() {
    g_wifi_draft.mode=WifiMode::Station;
    lv_dropdown_set_selected(g_wifi_mode,0);
    if(g_wifi_draft.enabled) lv_obj_add_state(g_wifi_enabled,LV_STATE_CHECKED); else lv_obj_remove_state(g_wifi_enabled,LV_STATE_CHECKED);
    lv_textarea_set_text(g_wifi_ssid,g_wifi_draft.ssid.data());
    lv_textarea_set_text(g_wifi_password,g_wifi_draft.password.data());
    if(g_wifi_draft.open_network) lv_obj_add_state(g_wifi_open,LV_STATE_CHECKED); else lv_obj_remove_state(g_wifi_open,LV_STATE_CHECKED);
    lv_obj_remove_flag(g_wifi_open,LV_OBJ_FLAG_HIDDEN);
    wifi_mask_cb(nullptr);
}
void wifi_mode_cb(lv_event_t *) {
    g_wifi_draft.mode=WifiMode::Station;
    lv_dropdown_set_selected(g_wifi_mode,0);
}
void wifi_screen_cb(lv_event_t *) {
    g_wifi_feedback[0]=0;
    g_wifi_draft=g_settings.wifi;
    g_wifi_draft.mode=WifiMode::Station;
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
    if(g_settings.n2k_input.mode==N2kInputMode::Wired) {
        wifi_feedback("Saved for Wi-Fi NMEA 2000 source");
        return;
    }
    WifiConfig runtime=candidate.wifi;
    runtime.enabled=true;
    runtime.mode=WifiMode::Station;
    if(!wifi_service_apply_config(runtime)) {wifi_feedback("Saved; Wi-Fi is switching or busy");return;}
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
