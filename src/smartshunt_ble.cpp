#include "smartshunt_ble.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/aes.h"

namespace {
constexpr const char *TAG = "smartshunt";
constexpr uint16_t VICTRON_COMPANY_ID = 0x02E1;
constexpr uint8_t VICTRON_PRODUCT_ADVERTISEMENT = 0x10;
constexpr uint8_t VICTRON_BATTERY_MONITOR_RECORD = 0x02;
constexpr size_t VICTRON_HEADER_LEN = 5;
constexpr size_t BATTERY_RECORD_LEN = 16;
constexpr uint32_t STALE_AFTER_MS = 5000;

SemaphoreHandle_t g_mutex = nullptr;
AppSettings g_settings;
SmartShuntData g_data;
std::array<uint8_t, 16> g_bindkey{};
bool g_bindkey_valid = false;
bool g_target_mac_valid = false;
std::array<uint8_t, 6> g_target_mac{};
bool g_ble_initialized = false;
int64_t g_last_rx_us = 0;

uint32_t read_bits_le(const uint8_t *data, size_t bit_offset, uint8_t bit_count)
{
    uint32_t value = 0;
    for (uint8_t bit = 0; bit < bit_count; ++bit) {
        const size_t source_bit = bit_offset + bit;
        const uint8_t source = static_cast<uint8_t>((data[source_bit / 8] >> (source_bit % 8)) & 0x01U);
        value |= static_cast<uint32_t>(source) << bit;
    }
    return value;
}

int32_t sign_extend(uint32_t value, uint8_t bits)
{
    const uint32_t sign_bit = 1UL << (bits - 1U);
    if ((value & sign_bit) != 0) value |= ~((1UL << bits) - 1UL);
    return static_cast<int32_t>(value);
}

int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

bool parse_bindkey(const char *text, std::array<uint8_t, 16> &key)
{
    if (text == nullptr || std::strlen(text) != 32) return false;
    for (size_t i = 0; i < key.size(); ++i) {
        const int hi = hex_nibble(text[i * 2]);
        const int lo = hex_nibble(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        key[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

bool parse_mac(const char *text, std::array<uint8_t, 6> &mac)
{
    unsigned int b[6]{};
    if (text == nullptr || std::strlen(text) != 17) return false;
    if (std::sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) return false;
    for (size_t i = 0; i < 6; ++i) mac[i] = static_cast<uint8_t>(b[i]);
    return true;
}

void format_mac(const uint8_t mac[6], char out[18])
{
    std::snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool mac_matches(const uint8_t mac[6])
{
    return !g_target_mac_valid || std::memcmp(mac, g_target_mac.data(), 6) == 0;
}

const uint8_t *find_manufacturer_payload(const uint8_t *adv, size_t adv_len, size_t &payload_len)
{
    payload_len = 0;
    size_t pos = 0;
    while (pos < adv_len) {
        const uint8_t field_len = adv[pos];
        if (field_len == 0 || pos + 1U + field_len > adv_len) break;
        const uint8_t type = adv[pos + 1U];
        if (type == 0xFF && field_len >= 3) {
            const uint8_t *data = &adv[pos + 2U];
            const size_t data_len = field_len - 1U;
            const uint16_t company_id = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8U);
            if (company_id == VICTRON_COMPANY_ID) {
                payload_len = data_len - 2U;
                return data + 2U;
            }
        }
        pos += static_cast<size_t>(field_len) + 1U;
    }
    return nullptr;
}

bool decrypt_payload(const uint8_t *encrypted, size_t encrypted_len, uint8_t nonce_lsb, uint8_t nonce_msb, uint8_t *decrypted)
{
    if (!g_bindkey_valid || encrypted_len > BATTERY_RECORD_LEN) return false;
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    if (mbedtls_aes_setkey_enc(&ctx, g_bindkey.data(), 128) != 0) {
        mbedtls_aes_free(&ctx);
        return false;
    }
    size_t nc_offset = 0;
    unsigned char nonce_counter[16]{};
    unsigned char stream_block[16]{};
    nonce_counter[0] = nonce_lsb;
    nonce_counter[1] = nonce_msb;
    const int rc = mbedtls_aes_crypt_ctr(&ctx, encrypted_len, &nc_offset, nonce_counter, stream_block, encrypted, decrypted);
    mbedtls_aes_free(&ctx);
    return rc == 0;
}

void publish_decoded(const uint8_t *record, int rssi, const uint8_t mac[6])
{
    SmartShuntData next{};
    next.valid = true;
    next.key_valid = true;
    next.stale = false;
    next.rssi = rssi;
    format_mac(mac, next.source_mac);

    const uint32_t ttg_raw = read_bits_le(record, 0, 16);
    const uint32_t voltage_raw = read_bits_le(record, 16, 16);
    next.alarm_reason = static_cast<uint16_t>(read_bits_le(record, 32, 16));
    const uint32_t aux_raw = read_bits_le(record, 48, 16);
    const uint32_t aux_mode = read_bits_le(record, 64, 2);
    const uint32_t current_raw = read_bits_le(record, 66, 22);
    const uint32_t consumed_raw = read_bits_le(record, 88, 20);
    const uint32_t soc_raw = read_bits_le(record, 108, 10);

    if (ttg_raw != 0xFFFFU) {
        next.time_to_go_valid = true;
        next.time_to_go_min = static_cast<uint16_t>(ttg_raw);
    }
    if (voltage_raw != 0x7FFFU) {
        next.voltage_valid = true;
        next.voltage_v = static_cast<float>(sign_extend(voltage_raw, 16)) * 0.01f;
    }
    if (current_raw != 0x3FFFFFU) {
        next.current_valid = true;
        next.current_a = static_cast<float>(sign_extend(current_raw, 22)) * 0.001f;
    }
    if (consumed_raw != 0xFFFFFU) {
        next.consumed_ah_valid = true;
        next.consumed_ah = -static_cast<float>(consumed_raw) * 0.1f;
    }
    if (soc_raw != 0x3FFU) {
        next.soc_valid = true;
        next.soc_pct = static_cast<float>(soc_raw) * 0.1f;
    }
    if (aux_mode == 2U && aux_raw != 0xFFFFU) {
        next.temperature_valid = true;
        next.temperature_c = static_cast<float>(aux_raw) * 0.01f - 273.15f;
    }

    if (g_mutex != nullptr && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        g_data = next;
        g_last_rx_us = esp_timer_get_time();
        xSemaphoreGive(g_mutex);
    }
}

void publish_key_problem(int rssi, const uint8_t mac[6])
{
    SmartShuntData pending{};
    pending.valid = false;
    pending.key_valid = false;
    pending.stale = false;
    pending.rssi = rssi;
    format_mac(mac, pending.source_mac);
    if (g_mutex != nullptr && xSemaphoreTake(g_mutex, 0) == pdTRUE) {
        g_data = pending;
        g_last_rx_us = esp_timer_get_time();
        xSemaphoreGive(g_mutex);
    }
}

void handle_victron_advertisement(const esp_ble_gap_cb_param_t::ble_scan_result_evt_param &scan)
{
    if (!g_settings.smartshunt_enabled || scan.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT || !mac_matches(scan.bda)) return;

    size_t payload_len = 0;
    const size_t raw_len = static_cast<size_t>(scan.adv_data_len) + static_cast<size_t>(scan.scan_rsp_len);
    const uint8_t *payload = find_manufacturer_payload(scan.ble_adv, raw_len, payload_len);
    if (payload == nullptr || payload_len < VICTRON_HEADER_LEN) return;
    if (payload[0] != VICTRON_PRODUCT_ADVERTISEMENT || payload[1] != VICTRON_BATTERY_MONITOR_RECORD) return;

    if (!g_bindkey_valid || payload[4] != g_bindkey[0]) {
        publish_key_problem(scan.rssi, scan.bda);
        return;
    }

    const size_t encrypted_len = payload_len - VICTRON_HEADER_LEN;
    if (encrypted_len < BATTERY_RECORD_LEN) return;

    uint8_t decrypted[BATTERY_RECORD_LEN]{};
    if (!decrypt_payload(payload + VICTRON_HEADER_LEN, BATTERY_RECORD_LEN, payload[2], payload[3], decrypted)) {
        ESP_LOGW(TAG, "AES-CTR decrypt failed");
        return;
    }
    publish_decoded(decrypted, scan.rssi, scan.bda);
}

void gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) esp_ble_gap_start_scanning(0);
        else ESP_LOGE(TAG, "BLE scan parameter setup failed: %d", param->scan_param_cmpl.status);
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
        handle_victron_advertisement(param->scan_rst);
        break;
    default:
        break;
    }
}

void refresh_parsed_settings(const AppSettings &settings)
{
    g_settings = settings;
    g_bindkey_valid = parse_bindkey(settings.smartshunt_bindkey.data(), g_bindkey);
    g_target_mac_valid = parse_mac(settings.smartshunt_mac.data(), g_target_mac);
    if (!g_bindkey_valid && settings.smartshunt_enabled) ESP_LOGW(TAG, "SmartShunt enabled but Instant Readout key is missing or invalid");
}
}

bool smartshunt_ble_start(const AppSettings &settings)
{
    refresh_parsed_settings(settings);
    if (g_mutex == nullptr) g_mutex = xSemaphoreCreateMutex();
    if (g_mutex == nullptr) return false;
    if (g_ble_initialized) return true;

    esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "Unable to release Classic BT memory: %s", esp_err_to_name(err));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&bt_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    err = esp_bluedroid_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    err = esp_bluedroid_enable();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_callback));
    static esp_ble_scan_params_t scan_params{
        .scan_type = BLE_SCAN_TYPE_PASSIVE,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval = 0x80,
        .scan_window = 0x30,
        .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
    };
    ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&scan_params));
    g_ble_initialized = true;
    ESP_LOGI(TAG, "Passive SmartShunt Instant Readout scanner started");
    return true;
}

void smartshunt_ble_apply_settings(const AppSettings &settings)
{
    refresh_parsed_settings(settings);
}

SmartShuntData smartshunt_ble_get_data()
{
    SmartShuntData copy{};
    int64_t last_rx_us = 0;
    if (g_mutex != nullptr && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        copy = g_data;
        last_rx_us = g_last_rx_us;
        xSemaphoreGive(g_mutex);
    }
    if (last_rx_us > 0) {
        copy.age_ms = static_cast<uint32_t>((esp_timer_get_time() - last_rx_us) / 1000);
        copy.stale = copy.age_ms > STALE_AFTER_MS;
    }
    return copy;
}
