#include "wifi_service.hpp"
#include "ota.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "wifi";
SemaphoreHandle_t g_mutex = nullptr;
QueueHandle_t g_commands = nullptr;
WifiConfig g_config{};
WifiStatus g_status{};
WifiScanResults g_scan{};
std::atomic<bool> g_switching{false};
std::atomic<bool> g_scanning{false};
std::atomic<bool> g_reconnect{false};
std::atomic<bool> g_initialized{false};
std::atomic<bool> g_scan_finished{false};
std::atomic<int> g_scan_error{0};
int64_t g_next_retry = 0;
unsigned g_retry_count = 0; // Owned exclusively by worker.
bool g_started = false; // Owned exclusively by worker after initialization.
enum class CommandType { Apply, Scan };
struct Command { CommandType type; WifiConfig config; };

WifiConfig snapshot() {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    auto result = g_config;
    xSemaphoreGive(g_mutex);
    return result;
}
void state(WifiState value, esp_err_t error = ESP_OK, const char *message = "") {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_status.state = value;
    g_status.last_error = error;
    std::snprintf(g_status.message.data(), g_status.message.size(), "%s", message);
    if (value != WifiState::Connected && value != WifiState::AccessPoint) {
        g_status.ip.fill(0); g_status.rssi = 0;
    }
    xSemaphoreGive(g_mutex);
}
const char *disconnect_text(uint8_t reason) {
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND: return "SSID not found (2.4 GHz only)";
    case WIFI_REASON_AUTH_FAIL: return "Authentication failed; check password";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "Security handshake timed out; check password";
    case WIFI_REASON_BEACON_TIMEOUT: return "Router signal lost";
    case WIFI_REASON_ASSOC_FAIL: return "Router rejected association";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY: return "Router security incompatible";
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD: return "Router below WPA2 security threshold";
    default: return "Disconnected; retrying";
    }
}
void connect() {
    const auto config = snapshot();
    if (!config.enabled || config.mode != WifiMode::Station || g_scanning) return;
    const char *reason = nullptr;
    if (!wifi_config_valid(config, &reason)) { state(WifiState::CredentialsRequired, ESP_ERR_INVALID_ARG, reason); return; }
    state(WifiState::Connecting);
    const esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        state(WifiState::Error, err, esp_err_to_name(err));
        g_reconnect = true;
    }
}
void event(void *, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        const auto config=snapshot();
        if (config.enabled && config.mode != WifiMode::Station) return;
        g_switching = false;
        if (snapshot().enabled) connect();
        else state(WifiState::Disabled);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        const auto config=snapshot();
        if (!g_switching && config.enabled && config.mode == WifiMode::Station)
            state(WifiState::Connecting,ESP_OK,"Associated; waiting for DHCP address");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (g_switching || !snapshot().enabled || snapshot().mode != WifiMode::Station) return;
        auto *disconnected = static_cast<wifi_event_sta_disconnected_t *>(data);
        state(WifiState::Disconnected, ESP_OK, disconnect_text(disconnected->reason));
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        g_status.disconnect_reason = disconnected->reason;
        xSemaphoreGive(g_mutex);
        ESP_LOGW(TAG, "Disconnected: reason %u (%s)", disconnected->reason, disconnect_text(disconnected->reason));
        g_reconnect = true;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        if (!g_scanning) { esp_wifi_clear_ap_list(); return; }
        g_scan_error = static_cast<wifi_event_sta_scan_done_t *>(data)->status;
        g_scan_finished = true;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        if (g_switching || !snapshot().enabled || snapshot().mode != WifiMode::Station) return;
        auto *got = static_cast<ip_event_got_ip_t *>(data);
        wifi_ap_record_t ap{};
        esp_wifi_sta_get_ap_info(&ap);
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        g_status.state = WifiState::Connected;
        g_status.rssi = ap.rssi;
        g_status.disconnect_reason = 0;
        g_status.last_error = 0;
        g_status.message.fill(0);
        std::snprintf(g_status.ip.data(), g_status.ip.size(), IPSTR, IP2STR(&got->ip_info.ip));
        xSemaphoreGive(g_mutex);
        g_reconnect = false;
    }
}
// Process AP records on the worker's stack, not the smaller event-loop stack.
void complete_scan() {
    if (!g_scanning) return;
    wifi_ap_record_t records[MAX_WIFI_NETWORKS]{};
    uint16_t count = MAX_WIFI_NETWORKS;
    esp_err_t err = g_scan_error.load() == 0 ? esp_wifi_scan_get_ap_records(&count, records) : ESP_FAIL;
    if (err != ESP_OK) esp_wifi_clear_ap_list();
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_scan.count = 0;
    g_scan.last_error = err;
    g_scan.state = err == ESP_OK ? WifiScanState::Complete : WifiScanState::Failed;
    if (err == ESP_OK) for (uint16_t i = 0; i < count; ++i) {
        if (!records[i].ssid[0]) continue; // Hidden SSIDs use manual entry.
        bool duplicate = false;
        for (size_t j=0; j<g_scan.count; ++j)
            duplicate |= std::strcmp(g_scan.networks[j].ssid.data(), reinterpret_cast<char *>(records[i].ssid)) == 0;
        if (duplicate) continue;
        auto &network = g_scan.networks[g_scan.count++];
        std::memcpy(network.ssid.data(), records[i].ssid, 32);
        network.ssid.back() = 0;
        network.rssi = records[i].rssi;
        network.open = records[i].authmode == WIFI_AUTH_OPEN;
    }
    ++g_scan.generation;
    xSemaphoreGive(g_mutex);
    g_scanning = false;
    if (snapshot().enabled && wifi_service_get_status().state != WifiState::Connected) g_reconnect = true;
}
bool configure(const WifiConfig &config) {
    if (config.mode != WifiMode::Station) {
        ESP_LOGE(TAG,"Rejecting non-station Wi-Fi configuration in W2K profile");
        return false;
    }
    if (!ota_begin_network_change()) return false;
    struct Guard { ~Guard() { ota_end_network_change(); } } guard;
    g_switching = true;
    g_reconnect = false;
    if (g_scanning.exchange(false)) esp_wifi_scan_stop();
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    if (g_scan.state == WifiScanState::Scanning) {
        g_scan.state = WifiScanState::Failed; g_scan.last_error = ESP_ERR_INVALID_STATE; ++g_scan.generation;
    }
    xSemaphoreGive(g_mutex);
    if (g_started) {
        const esp_err_t err = esp_wifi_stop();
        if (err != ESP_OK) { g_switching = false; state(WifiState::Error, err, esp_err_to_name(err)); return false; }
        g_started = false;
    }
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_config = config;
    g_status = WifiStatus{};
    g_status.ssid = config.mode == WifiMode::AccessPoint ? config.ap_ssid : config.ssid;
    xSemaphoreGive(g_mutex);
    g_retry_count = 0;
    g_next_retry = 0;
    const char *reason = nullptr;
    if (!wifi_config_valid(config, &reason)) {
        // No interface will start, so no start event can release this flag.
        g_switching = false;
        state(WifiState::CredentialsRequired, ESP_ERR_INVALID_ARG, reason);
        return true; // Invalid user configuration is not a service init failure.
    }
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        wifi_config_t cfg{};
        std::memcpy(cfg.sta.ssid, config.ssid.data(), strnlen(config.ssid.data(),32));
        if (!config.open_network) std::memcpy(cfg.sta.password, config.password.data(), strnlen(config.password.data(),64));
        cfg.sta.threshold.authmode = config.open_network ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
        cfg.sta.pmf_cfg.capable = true; cfg.sta.pmf_cfg.required = false;
        err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    }
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) { g_switching = false; state(WifiState::Error, err, esp_err_to_name(err)); return false; }
    g_started = true;
    return true;
}
void scan() {
    const auto config = snapshot();
    // A disabled station may perform a one-shot scan.
    esp_err_t err = ESP_OK;
    if (err == ESP_OK && !g_started) {
        // Start a station for this explicit scan without changing saved/runtime
        // intent (including an enabled station awaiting credentials).
        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err == ESP_OK) err = esp_wifi_start();
        if (err == ESP_OK) g_started = true;
    }
    wifi_scan_config_t cfg{};
    cfg.show_hidden = false;
    cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    cfg.scan_time.active.min = 30; cfg.scan_time.active.max = 80;
    if (err == ESP_OK) { g_scanning = true; err = esp_wifi_scan_start(&cfg, false); }
    if (err != ESP_OK) {
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        g_scan.state = WifiScanState::Failed; g_scan.last_error = err; ++g_scan.generation;
        xSemaphoreGive(g_mutex);
        g_scanning = false;
    }
}
void worker(void *) {
    const TickType_t started=xTaskGetTickCount();
    bool stack_logged=false;
    for (;;) {
        Command command{};
        if (xQueueReceive(g_commands, &command, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (command.type == CommandType::Apply) configure(command.config);
            else scan();
        }
        if (g_scan_finished.exchange(false)) complete_scan();
        const auto st = wifi_service_get_status();
        if (st.state == WifiState::Connected) { g_retry_count = 0; g_next_retry = 0; }
        const int64_t now = esp_timer_get_time();
        if (g_reconnect && !g_scanning && !g_switching) {
            if (!g_next_retry) g_next_retry = now + std::min<int64_t>(1000000LL << std::min(g_retry_count++,5U),30000000LL);
            if (now >= g_next_retry) { g_reconnect = false; g_next_retry = 0; connect(); }
        }
        if(!stack_logged && xTaskGetTickCount()-started>=pdMS_TO_TICKS(5000)) {
            ESP_LOGI(TAG,"Wi-Fi worker minimum free stack after startup: %u bytes",
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
            stack_logged=true;
        }
    }
}
} // namespace

bool wifi_service_start(const WifiConfig &config) {
    if (config.mode != WifiMode::Station) {
        ESP_LOGE(TAG,"Wi-Fi service is station-only in the current NMEA profile");
        return false;
    }
    if (g_initialized) return wifi_service_apply_config(config);
    if (!wifi_service_prepare()) return false;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    if (!esp_netif_create_default_wifi_sta()) return false;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK || esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) return false;
    if (esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,nullptr) != ESP_OK ||
        esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,nullptr) != ESP_OK) return false;
    if (!configure(config)) return false;
    if (xTaskCreate(worker,"wifi_worker",6144,nullptr,3,nullptr) != pdPASS) { vQueueDelete(g_commands); g_commands=nullptr; return false; }
    g_initialized = true;
    return true;
}
bool wifi_service_apply_config(const WifiConfig &config) {
    if (!g_initialized || ota_update_in_progress() || config.mode != WifiMode::Station) return false;
    const auto current = snapshot();
    if (current.enabled == config.enabled && current.mode == config.mode && current.open_network == config.open_network &&
        current.ssid == config.ssid && current.password == config.password && current.ap_ssid == config.ap_ssid && current.ap_password == config.ap_password &&
        wifi_service_get_status().state != WifiState::Error && wifi_service_get_status().state != WifiState::Disconnected) return true;
    Command command{CommandType::Apply,config};
    return xQueueSend(g_commands,&command,0) == pdTRUE;
}
bool wifi_service_request_scan() {
    if (!g_initialized || g_switching || ota_update_in_progress() || g_scanning.exchange(true)) return false;
    xSemaphoreTake(g_mutex,portMAX_DELAY);
    g_scan.state=WifiScanState::Scanning; g_scan.count=0; ++g_scan.generation;
    xSemaphoreGive(g_mutex);
    Command command{CommandType::Scan,{}};
    if (xQueueSend(g_commands,&command,0) == pdTRUE) return true;
    g_scanning = false;
    xSemaphoreTake(g_mutex,portMAX_DELAY);
    g_scan.state=WifiScanState::Failed; g_scan.last_error=ESP_ERR_NO_MEM; ++g_scan.generation;
    xSemaphoreGive(g_mutex);
    return false;
}
WifiScanResults wifi_service_get_scan() {
    if (!g_mutex) return {};
    xSemaphoreTake(g_mutex,portMAX_DELAY); auto copy=g_scan; xSemaphoreGive(g_mutex); return copy;
}
WifiStatus wifi_service_get_status() {
    if (!g_mutex) return {};
    xSemaphoreTake(g_mutex,portMAX_DELAY); auto copy=g_status; xSemaphoreGive(g_mutex); return copy;
}
bool wifi_service_is_connected() { return wifi_service_get_status().state == WifiState::Connected; }

bool wifi_service_prepare() {
    if (!g_mutex) g_mutex=xSemaphoreCreateMutex();
    if (!g_commands) g_commands=xQueueCreate(4,sizeof(Command));
    return g_mutex && g_commands;
}
bool wifi_service_scan_active() {return g_scanning.load();}
