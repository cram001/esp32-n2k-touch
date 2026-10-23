AppSettings settings_snapshot()
{

    if (g_settings_mutex != nullptr && xSemaphoreTake(g_settings_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        g_cached_settings = g_settings;
        xSemaphoreGive(g_settings_mutex);
    }
    return g_cached_settings;
}
