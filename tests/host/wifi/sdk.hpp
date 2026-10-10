#pragma once
// Deterministic SDK boundary doubles; no background threads or radio hardware.
#include "esp_err.h"
#include <cstdint>
#include <cstddef>
constexpr int ESP_ERR_INVALID_STATE=5, ESP_ERR_INVALID_ARG=6, ESP_ERR_NO_MEM=7;
constexpr int pdTRUE=1, pdPASS=1, portMAX_DELAY=-1;
#define pdMS_TO_TICKS(x) (x)
using SemaphoreHandle_t=void *;
using QueueHandle_t=void *;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return reinterpret_cast<void *>(1); }
inline int xSemaphoreTake(void *, int) { return pdTRUE; }
inline int xSemaphoreGive(void *) { return pdTRUE; }
inline QueueHandle_t xQueueCreate(int, size_t) { return reinterpret_cast<void *>(2); }
inline void vQueueDelete(void *) {}
extern bool fake_queue_full;
extern unsigned fake_commands;
inline int xQueueSend(void *, const void *, int) { if (fake_queue_full) return 0; ++fake_commands; return pdTRUE; }
inline int xQueueReceive(void *, void *, int) { return 0; }
inline int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, void *) { return pdPASS; }
inline int xTaskCreatePinnedToCore(void (*task)(void *), const char *name, unsigned stack,
                                   void *arg, unsigned prio, void *handle, int) {
    return xTaskCreate(task,name,stack,arg,prio,handle);
}
using TickType_t=uint32_t;
inline TickType_t xTaskGetTickCount() { return 5000; }
inline unsigned uxTaskGetStackHighWaterMark(void *) { return 4096; }
using esp_event_base_t=int;
constexpr int WIFI_EVENT=1, IP_EVENT=2, ESP_EVENT_ANY_ID=-1;
constexpr int WIFI_EVENT_STA_START=1, WIFI_EVENT_AP_START=2, WIFI_EVENT_STA_CONNECTED=3,
              WIFI_EVENT_STA_DISCONNECTED=4, WIFI_EVENT_SCAN_DONE=5, IP_EVENT_STA_GOT_IP=6;
constexpr int WIFI_EVENT_AP_STACONNECTED=7;
inline int esp_event_loop_create_default() { return ESP_OK; }
inline int esp_event_handler_register(int, int, void (*)(void *, int, int32_t, void *), void *) { return ESP_OK; }
struct esp_netif_t {};
struct esp_netif_ip_info_t { unsigned ip=0; };
struct ip_event_got_ip_t { esp_netif_ip_info_t ip_info; };
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) ((void)(ip), 192U), 168U, 4U, 1U
inline int esp_netif_init() { return ESP_OK; }
inline esp_netif_t *esp_netif_create_default_wifi_sta() { static esp_netif_t netif; return &netif; }
inline esp_netif_t *esp_netif_create_default_wifi_ap() { return esp_netif_create_default_wifi_sta(); }
inline int esp_netif_get_ip_info(esp_netif_t *, esp_netif_ip_info_t *) { return ESP_OK; }
struct esp_sntp_config_t { bool start=true; };
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server) esp_sntp_config_t{}
inline int esp_netif_sntp_init(esp_sntp_config_t *) { return ESP_OK; }
inline int esp_netif_sntp_start() { return ESP_OK; }
inline int64_t esp_timer_get_time() { return 0; }
constexpr int WIFI_REASON_NO_AP_FOUND=1, WIFI_REASON_AUTH_FAIL=2, WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT=3,
 WIFI_REASON_HANDSHAKE_TIMEOUT=4, WIFI_REASON_BEACON_TIMEOUT=5, WIFI_REASON_ASSOC_FAIL=6,
 WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY=7, WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD=8;
constexpr int WIFI_AUTH_OPEN=0, WIFI_AUTH_WPA2_PSK=3, WIFI_MODE_AP=2, WIFI_MODE_STA=1,
 WIFI_IF_AP=1, WIFI_IF_STA=0, WIFI_STORAGE_RAM=1, WIFI_SCAN_TYPE_ACTIVE=0;
struct wifi_event_sta_disconnected_t { uint8_t reason=0; };
struct wifi_event_sta_scan_done_t { int status=0; };
struct wifi_ap_record_t { uint8_t ssid[33]{}; int8_t rssi=0; int authmode=0; };
struct wifi_config_t {
 struct { uint8_t ssid[32]{}, password[64]{}; unsigned ssid_len=0,channel=0,max_connection=0; int authmode=0; } ap;
 struct { uint8_t ssid[32]{}, password[64]{}; struct { int authmode=0; } threshold;
          struct { bool capable=false, required=false; } pmf_cfg; } sta;
};
struct wifi_scan_config_t { bool show_hidden=false; int scan_type=0;
 struct { struct { unsigned min=0,max=0; } active; } scan_time; };
struct wifi_init_config_t {};
#define WIFI_INIT_CONFIG_DEFAULT() wifi_init_config_t{}
extern int fake_stop_error, fake_mode_error, fake_config_error, fake_start_error;
extern unsigned fake_starts, fake_scans;
inline int esp_wifi_init(wifi_init_config_t *) { return ESP_OK; }
inline int esp_wifi_set_storage(int) { return ESP_OK; }
inline int esp_wifi_connect() { return ESP_OK; }
inline int esp_wifi_stop() { return fake_stop_error; }
inline int esp_wifi_start() { ++fake_starts; return fake_start_error; }
inline int esp_wifi_set_mode(int) { return fake_mode_error; }
inline int esp_wifi_set_config(int, wifi_config_t *) { return fake_config_error; }
inline int esp_wifi_scan_stop() { return ESP_OK; }
inline int esp_wifi_scan_start(wifi_scan_config_t *, bool) { ++fake_scans; return ESP_OK; }
inline int esp_wifi_clear_ap_list() { return ESP_OK; }
inline int esp_wifi_sta_get_ap_info(wifi_ap_record_t *) { return ESP_OK; }
inline int esp_wifi_scan_get_ap_records(uint16_t *count, wifi_ap_record_t *) { *count=0; return ESP_OK; }
