bool ota_enable_local_server(uint32_t seconds)
{
    ensure_mutex();
    if(!g_mutex || !seconds || wifi_service_get_status().state!=WifiState::Connected)return false;
    xSemaphoreTake(g_mutex,portMAX_DELAY);
    bool start=false;
    const bool accepted=g_window.enable(esp_timer_get_time(),static_cast<int64_t>(seconds)*1000000LL,start);
    bool failed=false;
    if(accepted && start && xTaskCreate(local_server_worker,"http_window",4096,nullptr,1,nullptr)!=pdPASS) {
        g_window.stopped();failed=true;
    }
    xSemaphoreGive(g_mutex);
    if(failed) {
        publish_server_status({LocalServerStage::StartFailed,ESP_ERR_NO_MEM,0,0,false},server_heap());
        ota_log_local_server_status();
    }
    return accepted && !failed;
}
bool ota_disable_local_server()
{
    ensure_mutex();
    if(!g_mutex)return false;
    xSemaphoreTake(g_mutex,portMAX_DELAY);
    const bool busy=g_active || g_status.state==OtaState::ReadyToReboot;
    if(!busy && g_window.active())g_window.deadline=esp_timer_get_time();
    xSemaphoreGive(g_mutex);
    return !busy;
}
bool ota_local_server_window_active(){return g_window.active();}
uint32_t ota_local_server_seconds_remaining()
{
    if(!g_mutex)return 0;
    xSemaphoreTake(g_mutex,portMAX_DELAY);
    const int64_t remaining=g_window.active()?g_window.deadline-esp_timer_get_time():0;
    xSemaphoreGive(g_mutex);
    return remaining>0?static_cast<uint32_t>((remaining+999999)/1000000):0;
}
