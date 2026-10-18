void set_active(bool active)
{
    ensure_mutex();
    if (g_mutex != nullptr && xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        g_active = active;
        xSemaphoreGive(g_mutex);
    }
}

void set_status(OtaState state, int progress, esp_err_t err, const char *message)
{
    ensure_mutex();
    if (g_mutex != nullptr && xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        g_status.state = state;
        g_status.progress_percent = std::clamp(progress, 0, 100);
        g_status.last_error = static_cast<int>(err);
        std::snprintf(g_status.message, sizeof(g_status.message), "%s", message ? message : "");
        xSemaphoreGive(g_mutex);
    }
}
