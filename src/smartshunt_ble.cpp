#include "smartshunt_ble.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "aes/esp_aes.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {
constexpr const char *TAG = "smartshunt";
constexpr uint16_t VICTRON_COMPANY_ID = 0x02E1;
constexpr uint8_t VICTRON_PRODUCT_ADVERTISEMENT = 0x10;
constexpr uint8_t VICTRON_BATTERY_MONITOR_RECORD = 0x02;
constexpr size_t VICTRON_HEADER_LEN = 5;
constexpr size_t BATTERY_RECORD_LEN = 16;
constexpr uint32_t STALE_AFTER_MS = 5000;
constexpr uint32_t DISCOVERY_STALE_MS = 15000;

struct RuntimeSlot {
    std::array<uint8_t, 6> mac{};
    std::array<uint8_t, 16> key{};
    bool mac_valid = false;
    bool key_valid = false;
    SmartShuntData data{};
    int64_t last_rx_us = 0;
};

struct DiscoveredSlot {
    DiscoveredSmartShunt device{};
    int64_t last_seen_us = 0;
};

SemaphoreHandle_t g_mutex = nullptr;
std::array<SmartShuntConfig, MAX_SMARTSHUNTS> g_configs{};
uint32_t g_config_generation = 0;
std::array<RuntimeSlot, MAX_SMARTSHUNTS> g_runtime{};
std::array<DiscoveredSlot, MAX_DISCOVERED_SMARTSHUNTS> g_discovered{};
bool g_ble_initialized = false;

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

const uint8_t *find_ad_type(const uint8_t *adv, size_t adv_len, uint8_t wanted_type, size_t &data_len)
{
    data_len = 0;
    size_t pos = 0;
    while (pos < adv_len) {
        const uint8_t field_len = adv[pos];
        if (field_len == 0 || pos + 1U + field_len > adv_len) break;
        const uint8_t type = adv[pos + 1U];
        if (type == wanted_type && field_len >= 2) {
            data_len = field_len - 1U;
            return &adv[pos + 2U];
        }
        pos += static_cast<size_t>(field_len) + 1U;
    }
    return nullptr;
}

const uint8_t *find_manufacturer_payload(const uint8_t *adv, size_t adv_len, size_t &payload_len)
{
    size_t len = 0;
    const uint8_t *data = find_ad_type(adv, adv_len, 0xFF, len);
    if (data == nullptr || len < 3) return nullptr;
    const uint16_t company_id = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8U);
    if (company_id != VICTRON_COMPANY_ID) return nullptr;
    payload_len = len - 2U;
    return data + 2U;
}

void advertised_name(const uint8_t *adv, size_t adv_len, char out[25], const uint8_t mac[6])
{
    size_t len = 0;
    const uint8_t *name = find_ad_type(adv, adv_len, 0x09, len);
    if (name == nullptr) name = find_ad_type(adv, adv_len, 0x08, len);
    if (name != nullptr && len > 0) {
        const size_t copy = len < 24 ? len : 24;
        std::memcpy(out, name, copy);
        out[copy] = '\0';
        return;
    }
    std::snprintf(out, 25, "SmartShunt %02X%02X", mac[4], mac[5]);
}

void update_discovery(const esp_ble_gap_cb_param_t::ble_scan_result_evt_param &scan)
{
    char mac_text[18]{};
    format_mac(scan.bda, mac_text);
    const int64_t now = esp_timer_get_time();

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    size_t target = MAX_DISCOVERED_SMARTSHUNTS;
    size_t oldest = 0;
    for (size_t i = 0; i < g_discovered.size(); ++i) {
        if (g_discovered[i].device.valid && std::strcmp(g_discovered[i].device.mac.data(), mac_text) == 0) {
            target = i;
            break;
        }
        if (!g_discovered[i].device.valid && target == MAX_DISCOVERED_SMARTSHUNTS) target = i;
        if (g_discovered[i].last_seen_us < g_discovered[oldest].last_seen_us) oldest = i;
    }
    if (target == MAX_DISCOVERED_SMARTSHUNTS) target = oldest;

    auto &slot = g_discovered[target];
    slot.device = {};
    slot.device.valid = true;
    slot.device.rssi = scan.rssi;
    std::snprintf(slot.device.mac.data(), slot.device.mac.size(), "%s", mac_text);
    advertised_name(scan.ble_adv, static_cast<size_t>(scan.adv_data_len) + static_cast<size_t>(scan.scan_rsp_len), slot.device.name.data(), scan.bda);
    slot.last_seen_us = now;
    xSemaphoreGive(g_mutex);
}

bool decrypt_payload(const std::array<uint8_t, 16> &key, const uint8_t *encrypted, size_t encrypted_len,
                     uint8_t nonce_lsb, uint8_t nonce_msb, uint8_t *decrypted)
{
    esp_aes_context ctx;
    esp_aes_init(&ctx);
    if (esp_aes_setkey(&ctx, key.data(), 128) != 0) {
        esp_aes_free(&ctx);
        return false;
    }
    size_t nc_offset = 0;
    unsigned char nonce_counter[16]{};
    unsigned char stream_block[16]{};
    nonce_counter[0] = nonce_lsb;
    nonce_counter[1] = nonce_msb;
    const int rc = esp_aes_crypt_ctr(&ctx, encrypted_len, &nc_offset, nonce_counter, stream_block, encrypted, decrypted);
    esp_aes_free(&ctx);
    return rc == 0;
}

SmartShuntData decode_record(const uint8_t *record, int rssi)
{
    SmartShuntData next{};
    next.valid = true;
    next.key_valid = true;
    next.stale = false;
    next.rssi = rssi;

    const uint32_t ttg_raw = read_bits_le(record, 0, 16);
    const uint32_t voltage_raw = read_bits_le(record, 16, 16);
    next.alarm_reason = static_cast<uint16_t>(read_bits_le(record, 32, 16));
    const uint32_t aux_raw = read_bits_le(record, 48, 16);
    const uint32_t aux_mode = read_bits_le(record, 64, 2);
    const uint32_t current_raw = read_bits_le(record, 66, 22);
    const uint32_t consumed_raw = read_bits_le(record, 88, 20);
    const uint32_t soc_raw = read_bits_le(record, 108, 10);

    if (ttg_raw != 0xFFFFU) { next.time_to_go_valid = true; next.time_to_go_min = static_cast<uint16_t>(ttg_raw); }
    if (voltage_raw != 0x7FFFU) { next.voltage_valid = true; next.voltage_v = static_cast<float>(sign_extend(voltage_raw, 16)) * 0.01f; }
    if (current_raw != 0x3FFFFFU) { next.current_valid = true; next.current_a = static_cast<float>(sign_extend(current_raw, 22)) * 0.001f; }
    if (consumed_raw != 0xFFFFFU) { next.consumed_ah_valid = true; next.consumed_ah = -static_cast<float>(consumed_raw) * 0.1f; }
    if (soc_raw != 0x3FFU) { next.soc_valid = true; next.soc_pct = static_cast<float>(soc_raw) * 0.1f; }
    if (aux_mode == 2U && aux_raw != 0xFFFFU) { next.temperature_valid = true; next.temperature_c = static_cast<float>(aux_raw) * 0.01f - 273.15f; }
    return next;
}

void handle_victron_advertisement(const esp_ble_gap_cb_param_t::ble_scan_result_evt_param &scan)
{
    if (scan.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) return;

    size_t payload_len = 0;
    const size_t raw_len = static_cast<size_t>(scan.adv_data_len) + static_cast<size_t>(scan.scan_rsp_len);
    const uint8_t *payload = find_manufacturer_payload(scan.ble_adv, raw_len, payload_len);
    if (payload == nullptr || payload_len < VICTRON_HEADER_LEN) return;
    if (payload[0] != VICTRON_PRODUCT_ADVERTISEMENT || payload[1] != VICTRON_BATTERY_MONITOR_RECORD) return;

    update_discovery(scan);

    std::array<uint8_t, 6> incoming{};
    std::memcpy(incoming.data(), scan.bda, incoming.size());

    for (size_t i = 0; i < MAX_SMARTSHUNTS; ++i) {
        SmartShuntConfig cfg{};
        RuntimeSlot runtime_snapshot{};
        uint32_t generation = 0;

        if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(5)) != pdTRUE) continue;
        cfg = g_configs[i];
        runtime_snapshot.mac = g_runtime[i].mac;
        runtime_snapshot.key = g_runtime[i].key;
        runtime_snapshot.mac_valid = g_runtime[i].mac_valid;
        runtime_snapshot.key_valid = g_runtime[i].key_valid;
        generation = g_config_generation;
        xSemaphoreGive(g_mutex);

        if (!cfg.configured || !cfg.enabled || !runtime_snapshot.mac_valid || incoming != runtime_snapshot.mac) continue;

        if (!runtime_snapshot.key_valid || payload[4] != runtime_snapshot.key[0]) {
            if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                if (generation == g_config_generation && incoming == g_runtime[i].mac) {
                    g_runtime[i].data = {};
                    g_runtime[i].data.key_valid = false;
                    g_runtime[i].data.rssi = scan.rssi;
                    g_runtime[i].last_rx_us = esp_timer_get_time();
                }
                xSemaphoreGive(g_mutex);
            }
            continue;
        }

        const size_t encrypted_len = payload_len - VICTRON_HEADER_LEN;
        if (encrypted_len < BATTERY_RECORD_LEN) continue;

        uint8_t decrypted[BATTERY_RECORD_LEN]{};
        if (!decrypt_payload(runtime_snapshot.key, payload + VICTRON_HEADER_LEN, BATTERY_RECORD_LEN,
                             payload[2], payload[3], decrypted)) {
            continue;
        }

        const SmartShuntData decoded = decode_record(decrypted, scan.rssi);
        if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            if (generation == g_config_generation && incoming == g_runtime[i].mac) {
                g_runtime[i].data = decoded;
                g_runtime[i].last_rx_us = esp_timer_get_time();
            }
            xSemaphoreGive(g_mutex);
        }
    }
}
void gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    if (event == ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT) {
        if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) esp_ble_gap_start_scanning(0);
        else ESP_LOGE(TAG, "BLE scan parameter setup failed: %d", param->scan_param_cmpl.status);
    } else if (event == ESP_GAP_BLE_SCAN_RESULT_EVT) {
        handle_victron_advertisement(param->scan_rst);
    }
}

void refresh_runtime(const AppSettings &settings)
{
    if (g_mutex != nullptr) xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_configs = settings.smartshunts;
    ++g_config_generation;
    for (size_t i = 0; i < g_runtime.size(); ++i) {
        const auto previous_mac = g_runtime[i].mac;
        const auto previous_key = g_runtime[i].key;
        const bool previous_mac_valid = g_runtime[i].mac_valid;
        const bool previous_key_valid = g_runtime[i].key_valid;

        g_runtime[i].mac_valid = parse_mac(settings.smartshunts[i].mac.data(), g_runtime[i].mac);
        g_runtime[i].key_valid = parse_bindkey(settings.smartshunts[i].bindkey.data(), g_runtime[i].key);

        const bool identity_changed =
            previous_mac_valid != g_runtime[i].mac_valid ||
            previous_key_valid != g_runtime[i].key_valid ||
            previous_mac != g_runtime[i].mac ||
            previous_key != g_runtime[i].key;
        if (identity_changed) {
            g_runtime[i].data = {};
            g_runtime[i].last_rx_us = 0;
        }
    }
    if (g_mutex != nullptr) xSemaphoreGive(g_mutex);
}
}

bool smartshunt_ble_start(const AppSettings &settings)
{
    if (g_mutex == nullptr) g_mutex = xSemaphoreCreateMutex();
    if (g_mutex == nullptr) return false;
    refresh_runtime(settings);
    if (g_ble_initialized) return true;

    esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "Unable to release Classic BT memory: %s", esp_err_to_name(err));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((err = esp_bt_controller_init(&bt_cfg)) != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    if ((err = esp_bt_controller_enable(ESP_BT_MODE_BLE)) != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    if ((err = esp_bluedroid_init()) != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    if ((err = esp_bluedroid_enable()) != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_callback));
    static esp_ble_scan_params_t scan_params{};
    scan_params.scan_type = BLE_SCAN_TYPE_PASSIVE;
    scan_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    scan_params.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
    scan_params.scan_interval = 0x80;
    scan_params.scan_window = 0x30;
    scan_params.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;
    ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&scan_params));
    g_ble_initialized = true;
    ESP_LOGI(TAG, "Passive multi-SmartShunt scanner started");
    return true;
}

void smartshunt_ble_apply_settings(const AppSettings &settings)
{
    refresh_runtime(settings);
}

SmartShuntData smartshunt_ble_get_data(size_t configured_index)
{
    SmartShuntData copy{};
    int64_t last_rx_us = 0;
    if (configured_index >= g_runtime.size() || g_mutex == nullptr) return copy;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        copy = g_runtime[configured_index].data;
        last_rx_us = g_runtime[configured_index].last_rx_us;
        xSemaphoreGive(g_mutex);
    }
    if (last_rx_us > 0) {
        copy.age_ms = static_cast<uint32_t>((esp_timer_get_time() - last_rx_us) / 1000);
        copy.stale = copy.age_ms > STALE_AFTER_MS;
    }
    return copy;
}

size_t smartshunt_ble_get_discovered(std::array<DiscoveredSmartShunt, MAX_DISCOVERED_SMARTSHUNTS> &out)
{
    out = {};
    if (g_mutex == nullptr) return 0;
    const int64_t now = esp_timer_get_time();
    size_t count = 0;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        for (const auto &slot : g_discovered) {
            if (!slot.device.valid) continue;
            const uint32_t age = static_cast<uint32_t>((now - slot.last_seen_us) / 1000);
            if (age > DISCOVERY_STALE_MS) continue;
            out[count] = slot.device;
            out[count].age_ms = age;
            if (++count >= out.size()) break;
        }
        xSemaphoreGive(g_mutex);
    }
    return count;
}
