// LVGL task only; included in ui.cpp private namespace.
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
    case WifiState::Error: std::snprintf(b,sizeof(b),"Wi-Fi error 0x%x\n%s",st.last_error,st.message.data()); break;
    }
    lv_label_set_text(g_wifi_status,b);
}
