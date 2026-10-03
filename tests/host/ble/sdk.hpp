#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "esp_err.h"
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_LOGI(...) do {} while(false)
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() esp_bt_controller_config_t{}
#define ESP_BT_MODE_CLASSIC_BT 1
#define ESP_BT_MODE_BLE 2
#define BLE_SCAN_TYPE_ACTIVE 1
#define BLE_ADDR_TYPE_PUBLIC 0
#define BLE_SCAN_FILTER_ALLOW_ALL 0
#define BLE_SCAN_DUPLICATE_DISABLE 0
#define ESP_BT_STATUS_SUCCESS 0
#define ESP_GAP_SEARCH_INQ_RES_EVT 1
#define ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT 1
#define ESP_GAP_BLE_SCAN_RESULT_EVT 2
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY 0xffffffffU
using SemaphoreHandle_t=void*;
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return reinterpret_cast<void*>(1);}
inline int xSemaphoreTake(SemaphoreHandle_t,uint32_t){return pdTRUE;}
inline void xSemaphoreGive(SemaphoreHandle_t){}
extern int64_t fake_ble_time;
inline int64_t esp_timer_get_time(){return fake_ble_time;}
struct esp_bt_controller_config_t{};
inline esp_err_t esp_bt_controller_mem_release(int){return ESP_OK;}
inline esp_err_t esp_bt_controller_init(esp_bt_controller_config_t*){return ESP_OK;}
inline esp_err_t esp_bt_controller_enable(int){return ESP_OK;}
inline esp_err_t esp_bt_controller_disable(){return ESP_OK;}
inline esp_err_t esp_bt_controller_deinit(){return ESP_OK;}
inline esp_err_t esp_bluedroid_init(){return ESP_OK;}
inline esp_err_t esp_bluedroid_enable(){return ESP_OK;}
inline esp_err_t esp_bluedroid_disable(){return ESP_OK;}
inline esp_err_t esp_bluedroid_deinit(){return ESP_OK;}
using esp_gap_ble_cb_event_t=int;
struct esp_ble_gap_cb_param_t{
    struct ble_scan_result_evt_param {
        int search_evt=ESP_GAP_SEARCH_INQ_RES_EVT;
        uint8_t bda[6]{};
        int rssi=-50;
        uint8_t ble_adv[62]{};
        uint8_t adv_data_len=0,scan_rsp_len=0;
    } scan_rst;
    struct {int status=0;} scan_param_cmpl;
};
struct esp_ble_scan_params_t{int scan_type,own_addr_type,scan_filter_policy,scan_interval,scan_window,scan_duplicate;};
extern esp_ble_scan_params_t fake_scan_params;
inline esp_err_t esp_ble_gap_register_callback(void(*)(esp_gap_ble_cb_event_t,esp_ble_gap_cb_param_t*)){return ESP_OK;}
inline esp_err_t esp_ble_gap_set_scan_params(esp_ble_scan_params_t *p){fake_scan_params=*p;return ESP_OK;}
inline esp_err_t esp_ble_gap_start_scanning(int){return ESP_OK;}
inline esp_err_t esp_ble_gap_stop_scanning(){return ESP_OK;}
struct esp_aes_context{};
inline void esp_aes_init(esp_aes_context*){}
inline void esp_aes_free(esp_aes_context*){}
inline int esp_aes_setkey(esp_aes_context*,const uint8_t*,int){return 0;}
// No simulated successful decryption: these tests exercise discovery and
// filtering through the real service, not the AES implementation.
inline int esp_aes_crypt_ctr(esp_aes_context*,size_t,size_t*,unsigned char*,unsigned char*,const uint8_t*,uint8_t*){return -1;}
