#include "wifi_service.hpp"

#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {
constexpr const char *TAG = "wifi";
constexpr int MAX_RETRY_BEFORE_BACKOFF = 10;

SemaphoreHandle_t g_mutex = nullptr;
WifiStatus g_status{};
WifiConfig g_config{};
esp_netif_t *g_sta_netif = nullptr;
esp_event_handler_instance_t g_wifi_handler = nullptr;
esp_event_handler_instance_t g_ip_handler = nullptr;
bool g_initialized = false;
int g_retry_count = 0;

void lock_status()
{
    if (g_mutex == nullptr) g_mutex = xSemaphoreCreateMutex();
}

void set_state(WifiState state)
{
    lock_status();
    if (g_mutex != nullptr && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_status.state = state;
        xSemaphoreGive(g_mutex);
    }
}

void copy_ssid(const char *ssid)
{
    lock_status();
    if (g_mutex != nullptr && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        std::snprintf(g_status.ssid.data(), g_status.ssid.size(), "%s", ssid ? ssid : "");
        xSemaphoreGive(g_mutex);
    }
}

void clear_link_details()
{
    lock_status();
    if (g_mutex != nullptr && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_status.rssi = 0;
        g_status.ip[0] = '\0';
        xSemaphoreGive(g_mutex);
    }
}

void event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (g_config.enabled && g_config.ssid[0] != '\0') {
            set_state(WifiState::Connecting);
            esp_wifi_connect();
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        clear_link_details();
        if (!g_config.enabled || g_config.ssid[0] == '\0') {
            set_state(WifiState::Disabled);
            return;
        }

        set_state(WifiState::Disconnected);
        if (g_retry_count < MAX_RETRY_BEFORE_BACKOFF) ++g_retry_count;
        esp_wifi_connect();
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const auto *event = static_cast<ip_event_got_ip_t *>(event_data);
        char ip[16]{};
        std::snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));

        wifi_ap_record_t ap{};
        int8_t rssi = 0;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) rssi = ap.rssi;

        lock_status();
        if (g_mutex != nullptr && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            g_status.state = WifiState::Connected;
            g_status.rssi = rssi;
            std::snprintf(g_status.ip.data(), g_status.ip.size(), "%s", ip);
            xSemaphoreGive(g_mutex);
        }
        g_retry_count = 0;
        ESP_LOGI(TAG, "Connected to %s, IP %s, RSSI %d dBm", g_config.ssid.data(), ip, rssi);
    }
}

bool configure_station(const WifiConfig &config)
{
    wifi_config_t wifi_config{};
    std::snprintf(reinterpret_cast<char *>(wifi_config.sta.ssid), sizeof(wifi_config.sta.ssid), "%s", config.ssid.data());
    std::snprintf(reinterpret_cast<char *>(wifi_config.sta.password), sizeof(wifi_config.sta.password), "%s", config.password.data());
    wifi_config.sta.threshold.authmode = config.password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return false;
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) return false;
    return true;
}
} // namespace

bool wifi_service_start(const WifiConfig &config)
{
    if (g_initialized) {
        wifi_service_apply_config(config);
        return true;
    }

    lock_status();
    g_config = config;
    copy_ssid(g_config.ssid.data());

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        set_state(WifiState::Error);
        return false;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop init failed: %s", esp_err_to_name(err));
        set_state(WifiState::Error);
        return false;
    }

    g_sta_netif = esp_netif_create_default_wifi_sta();
    if (g_sta_netif == nullptr) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi station interface");
        set_state(WifiState::Error);
        return false;
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        set_state(WifiState::Error);
        return false;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, nullptr, &g_wifi_handler));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, nullptr, &g_ip_handler));

    if (!configure_station(g_config)) {
        set_state(WifiState::Error);
        return false;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
        set_state(WifiState::Error);
        return false;
    }

    g_initialized = true;
    if (!g_config.enabled || g_config.ssid[0] == '\0') {
        set_state(WifiState::Disabled);
    } else {
        set_state(WifiState::Connecting);
    }
    return true;
}

void wifi_service_apply_config(const WifiConfig &config)
{
    g_config = config;
    copy_ssid(g_config.ssid.data());

    if (!g_initialized) {
        wifi_service_start(config);
        return;
    }

    esp_wifi_disconnect();
    clear_link_details();

    if (!configure_station(g_config)) {
        set_state(WifiState::Error);
        return;
    }

    if (!g_config.enabled || g_config.ssid[0] == '\0') {
        set_state(WifiState::Disabled);
        return;
    }

    g_retry_count = 0;
    set_state(WifiState::Connecting);
    const esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        set_state(WifiState::Error);
    }
}

WifiStatus wifi_service_get_status()
{
    lock_status();
    WifiStatus copy{};
    if (g_mutex != nullptr && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        copy = g_status;
        xSemaphoreGive(g_mutex);
    }
    return copy;
}

bool wifi_service_is_connected()
{
    return wifi_service_get_status().state == WifiState::Connected;
}
