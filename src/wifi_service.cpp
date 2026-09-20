#include "wifi_service.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {
constexpr const char *TAG = "wifi";
constexpr int MAX_BACKOFF_EXPONENT = 5;
constexpr int64_t MAX_RECONNECT_DELAY_US = 30000000;

SemaphoreHandle_t g_mutex = nullptr;
WifiStatus g_status{};
WifiConfig g_config{};
esp_netif_t *g_sta_netif = nullptr;
esp_event_handler_instance_t g_wifi_handler = nullptr;
esp_event_handler_instance_t g_ip_handler = nullptr;
esp_timer_handle_t g_reconnect_timer = nullptr;
bool g_initialized = false;
int g_retry_count = 0;

bool ensure_mutex()
{
    if (g_mutex == nullptr) g_mutex = xSemaphoreCreateMutex();
    return g_mutex != nullptr;
}

WifiConfig config_snapshot()
{
    WifiConfig copy{};
    if (!ensure_mutex()) return copy;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        copy = g_config;
        xSemaphoreGive(g_mutex);
    }
    return copy;
}

void store_config(const WifiConfig &config)
{
    if (!ensure_mutex()) return;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_config = config;
        std::snprintf(g_status.ssid.data(), g_status.ssid.size(), "%s", config.ssid.data());
        xSemaphoreGive(g_mutex);
    }
}

void set_state(WifiState state)
{
    if (!ensure_mutex()) return;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_status.state = state;
        xSemaphoreGive(g_mutex);
    }
}

void clear_link_details()
{
    if (!ensure_mutex()) return;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_status.rssi = 0;
        g_status.ip[0] = '\0';
        xSemaphoreGive(g_mutex);
    }
}

bool same_config(const WifiConfig &a, const WifiConfig &b)
{
    return a.enabled == b.enabled &&
           a.open_network == b.open_network &&
           a.ssid == b.ssid &&
           a.password == b.password;
}

bool credentials_available(const WifiConfig &config)
{
    if (!config.enabled || config.ssid[0] == '\0') return false;
    return config.open_network || config.password[0] != '\0';
}

void reconnect_timer_cb(void *)
{
    const WifiConfig config = config_snapshot();
    if (!credentials_available(config)) return;
    set_state(WifiState::Connecting);
    const esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Reconnect attempt failed to start: %s", esp_err_to_name(err));
    }
}

void schedule_reconnect()
{
    if (g_reconnect_timer == nullptr) return;
    const int exponent = std::min(g_retry_count, MAX_BACKOFF_EXPONENT);
    const int64_t delay_us = std::min<int64_t>(1000000LL << exponent, MAX_RECONNECT_DELAY_US);
    ++g_retry_count;
    esp_timer_stop(g_reconnect_timer);
    const esp_err_t err = esp_timer_start_once(g_reconnect_timer, delay_us);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not schedule Wi-Fi reconnect: %s", esp_err_to_name(err));
    }
}

bool configure_station(const WifiConfig &config)
{
    wifi_config_t wifi_config{};
    const size_t ssid_len = strnlen(config.ssid.data(), config.ssid.size() - 1);
    const size_t pass_len = strnlen(config.password.data(), config.password.size() - 1);

    std::memcpy(wifi_config.sta.ssid, config.ssid.data(),
                std::min(ssid_len, sizeof(wifi_config.sta.ssid)));
    if (!config.open_network) {
        std::memcpy(wifi_config.sta.password, config.password.data(),
                    std::min(pass_len, sizeof(wifi_config.sta.password)));
    }

    wifi_config.sta.threshold.authmode = config.open_network ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return false;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return false;
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    return err == ESP_OK;
}

void event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        const WifiConfig config = config_snapshot();
        if (!config.enabled || config.ssid[0] == '\0') {
            set_state(WifiState::Disabled);
        } else if (!credentials_available(config)) {
            set_state(WifiState::CredentialsRequired);
        } else {
            set_state(WifiState::Connecting);
            const esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Initial Wi-Fi connect failed to start: %s", esp_err_to_name(err));
            }
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        clear_link_details();
        const WifiConfig config = config_snapshot();
        if (!config.enabled || config.ssid[0] == '\0') {
            set_state(WifiState::Disabled);
        } else if (!credentials_available(config)) {
            set_state(WifiState::CredentialsRequired);
        } else {
            set_state(WifiState::Disconnected);
            schedule_reconnect();
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const auto *event = static_cast<ip_event_got_ip_t *>(event_data);
        char ip[16]{};
        std::snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));

        wifi_ap_record_t ap{};
        int8_t rssi = 0;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) rssi = ap.rssi;

        const WifiConfig config = config_snapshot();
        if (ensure_mutex() && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            g_status.state = WifiState::Connected;
            g_status.rssi = rssi;
            std::snprintf(g_status.ip.data(), g_status.ip.size(), "%s", ip);
            xSemaphoreGive(g_mutex);
        }

        g_retry_count = 0;
        ESP_LOGI(TAG, "Connected to %s, IP %s, RSSI %d dBm", config.ssid.data(), ip, rssi);
    }
}
} // namespace

bool wifi_service_start(const WifiConfig &config)
{
    if (g_initialized) {
        wifi_service_apply_config(config);
        return true;
    }

    if (!ensure_mutex()) return false;
    store_config(config);

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

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, nullptr, &g_wifi_handler));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, nullptr, &g_ip_handler));

    esp_timer_create_args_t timer_args{};
    timer_args.callback = reconnect_timer_cb;
    timer_args.name = "wifi_reconnect";
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &g_reconnect_timer));

    if (!configure_station(config)) {
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
    if (!config.enabled || config.ssid[0] == '\0') {
        set_state(WifiState::Disabled);
    } else if (!credentials_available(config)) {
        set_state(WifiState::CredentialsRequired);
    } else {
        set_state(WifiState::Connecting);
    }
    return true;
}

void wifi_service_apply_config(const WifiConfig &config)
{
    if (!g_initialized) {
        wifi_service_start(config);
        return;
    }

    const WifiConfig previous = config_snapshot();
    if (same_config(config, previous)) return;

    store_config(config);
    if (g_reconnect_timer != nullptr) esp_timer_stop(g_reconnect_timer);
    esp_wifi_disconnect();
    clear_link_details();
    g_retry_count = 0;

    if (!configure_station(config)) {
        set_state(WifiState::Error);
        return;
    }

    if (!config.enabled || config.ssid[0] == '\0') {
        set_state(WifiState::Disabled);
        return;
    }

    if (!credentials_available(config)) {
        set_state(WifiState::CredentialsRequired);
        return;
    }

    set_state(WifiState::Connecting);
    const esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        set_state(WifiState::Error);
    }
}

WifiStatus wifi_service_get_status()
{
    WifiStatus copy{};
    if (!ensure_mutex()) return copy;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        copy = g_status;
        xSemaphoreGive(g_mutex);
    }
    return copy;
}

bool wifi_service_is_connected()
{
    return wifi_service_get_status().state == WifiState::Connected;
}
