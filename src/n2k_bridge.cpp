#include "n2k_bridge.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "N2kMessages.h"
#include "NMEA2000_esp32xx.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "smartshunt_ble.hpp"

namespace {
constexpr const char *TAG = "n2k_bridge";
constexpr gpio_num_t CAN_TX = GPIO_NUM_6;
constexpr gpio_num_t CAN_RX = GPIO_NUM_0;
constexpr TickType_t LOOP_DELAY = pdMS_TO_TICKS(20);
constexpr uint32_t BATTERY_STATUS_PERIOD_MS = 1000;
constexpr uint32_t DC_STATUS_PERIOD_MS = 1500;

SemaphoreHandle_t g_settings_mutex = nullptr;
AppSettings g_settings;
tNMEA2000_esp32xx g_nmea2000(CAN_TX, CAN_RX);
bool g_started = false;

AppSettings settings_snapshot()
{
    AppSettings copy;
    if (g_settings_mutex != nullptr && xSemaphoreTake(g_settings_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        copy = g_settings;
        xSemaphoreGive(g_settings_mutex);
    }
    return copy;
}

uint32_t unique_device_number()
{
    uint8_t mac[6]{};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) return 1;
    const uint32_t folded = (static_cast<uint32_t>(mac[2]) << 24U) |
                            (static_cast<uint32_t>(mac[3]) << 16U) |
                            (static_cast<uint32_t>(mac[4]) << 8U) |
                            static_cast<uint32_t>(mac[5]);
    return folded & 0x1FFFFFU;
}

void send_battery_status(const SmartShuntConfig &cfg, const SmartShuntData &data, uint8_t sid)
{
    if (!data.voltage_valid && !data.current_valid && !data.temperature_valid) return;
    tN2kMsg message;
    SetN2kPGN127508(message,
                    cfg.battery_instance,
                    data.voltage_valid ? data.voltage_v : N2kDoubleNA,
                    data.current_valid ? data.current_a : N2kDoubleNA,
                    data.temperature_valid ? static_cast<double>(data.temperature_c) + 273.15 : N2kDoubleNA,
                    sid);
    if (!g_nmea2000.SendMsg(message)) ESP_LOGW(TAG, "Failed to send PGN 127508 for %s", cfg.name.data());
}

void send_dc_status(const SmartShuntConfig &cfg, const SmartShuntData &data, uint8_t sid)
{
    if (!data.soc_valid && !data.time_to_go_valid) return;
    tN2kMsg message;
    const uint8_t soc = data.soc_valid
                            ? static_cast<uint8_t>(std::clamp(std::lround(data.soc_pct), 0L, 100L))
                            : 0xFF;
    const double time_remaining_s = data.time_to_go_valid
                                        ? static_cast<double>(data.time_to_go_min) * 60.0
                                        : N2kDoubleNA;
    SetN2kDCStatus(message, sid, cfg.battery_instance, N2kDCt_Battery,
                   soc, 0xFF, time_remaining_s, N2kDoubleNA, N2kDoubleNA);
    if (!g_nmea2000.SendMsg(message)) ESP_LOGW(TAG, "Failed to send PGN 127506 for %s", cfg.name.data());
}

bool duplicate_instance(const AppSettings &settings, size_t index)
{
    const auto &cfg = settings.smartshunts[index];
    if (!cfg.configured || !cfg.enabled || !cfg.n2k_enabled) return false;
    for (size_t i = 0; i < settings.smartshunts.size(); ++i) {
        if (i == index) continue;
        const auto &other = settings.smartshunts[i];
        if (other.configured && other.enabled && other.n2k_enabled && other.battery_instance == cfg.battery_instance) return true;
    }
    return false;
}

void n2k_task(void *)
{
    std::array<uint32_t, MAX_SMARTSHUNTS> last_battery_ms{};
    std::array<uint32_t, MAX_SMARTSHUNTS> last_dc_ms{};
    std::array<uint8_t, MAX_SMARTSHUNTS> sid{};

    for (;;) {
        g_nmea2000.ParseMessages();
        const AppSettings settings = settings_snapshot();
        const uint32_t now_ms = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);

        for (size_t i = 0; i < settings.smartshunts.size(); ++i) {
            const auto &cfg = settings.smartshunts[i];
            if (!cfg.configured || !cfg.enabled || !cfg.n2k_enabled) continue;
            if (duplicate_instance(settings, i)) continue; // Never emit ambiguous duplicate instances.

            const SmartShuntData data = smartshunt_ble_get_data(i);
            if (!data.valid || data.stale) continue;

            if (now_ms - last_battery_ms[i] >= BATTERY_STATUS_PERIOD_MS) {
                last_battery_ms[i] = now_ms;
                send_battery_status(cfg, data, sid[i]);
            }
            if (now_ms - last_dc_ms[i] >= DC_STATUS_PERIOD_MS) {
                last_dc_ms[i] = now_ms;
                send_dc_status(cfg, data, sid[i]);
                sid[i] = sid[i] >= 252 ? 0 : static_cast<uint8_t>(sid[i] + 1);
            }
        }
        vTaskDelay(LOOP_DELAY);
    }
}
}

bool n2k_bridge_start(const AppSettings &settings)
{
    if (g_started) {
        n2k_bridge_apply_settings(settings);
        return true;
    }
    g_settings_mutex = xSemaphoreCreateMutex();
    if (g_settings_mutex == nullptr) return false;
    g_settings = settings;

    g_nmea2000.SetDeviceInformation(unique_device_number(), 170, 35, 2046);
    g_nmea2000.SetMode(tNMEA2000::N2km_ListenAndNode, 40);
    g_nmea2000.EnableForward(false);
    g_nmea2000.SetN2kCANMsgBufSize(20);
    if (!g_nmea2000.Open()) {
        ESP_LOGE(TAG, "Failed to open NMEA 2000/TWAI interface");
        return false;
    }
    if (xTaskCreate(n2k_task, "n2k", 6144, nullptr, 5, nullptr) != pdPASS) return false;
    g_started = true;
    ESP_LOGI(TAG, "NMEA 2000 node active on TX GPIO6/RX GPIO0");
    return true;
}

void n2k_bridge_apply_settings(const AppSettings &settings)
{
    if (g_settings_mutex != nullptr && xSemaphoreTake(g_settings_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        g_settings = settings;
        xSemaphoreGive(g_settings_mutex);
    }
}
