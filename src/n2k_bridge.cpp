#include "n2k_bridge.hpp"

#include <algorithm>
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

void send_battery_status(const AppSettings &settings, const SmartShuntData &data, uint8_t sid)
{
    tN2kMsg message;
    const double temperature_k = data.temperature_valid ? static_cast<double>(data.temperature_c) + 273.15 : N2kDoubleNA;
    SetN2kPGN127508(message,
                    settings.battery_instance,
                    data.voltage_v,
                    data.current_a,
                    temperature_k,
                    sid);
    if (!g_nmea2000.SendMsg(message)) {
        ESP_LOGW(TAG, "Failed to send PGN 127508");
    }
}

void send_dc_detailed_status(const AppSettings &settings, const SmartShuntData &data, uint8_t sid)
{
    tN2kMsg message;
    const uint8_t soc = static_cast<uint8_t>(std::clamp(std::lround(data.soc_pct), 0L, 100L));
    const double time_remaining_s = data.time_to_go_min == 0xFFFFU
                                        ? N2kDoubleNA
                                        : static_cast<double>(data.time_to_go_min) * 60.0;

    // Capacity is intentionally NA: SmartShunt Instant Readout does not broadcast configured capacity.
    SetN2kDCStatus(message,
                   sid,
                   settings.battery_instance,
                   N2kDCt_Battery,
                   soc,
                   0xFF,
                   time_remaining_s,
                   N2kDoubleNA,
                   N2kDoubleNA);
    if (!g_nmea2000.SendMsg(message)) {
        ESP_LOGW(TAG, "Failed to send PGN 127506");
    }
}

void n2k_task(void *)
{
    uint32_t last_battery_status_ms = 0;
    uint32_t last_dc_status_ms = 0;
    uint8_t sid = 0;

    for (;;) {
        g_nmea2000.ParseMessages();

        const AppSettings settings = settings_snapshot();
        const SmartShuntData data = smartshunt_ble_get_data();
        const uint32_t now_ms = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);

        if (settings.smartshunt_n2k_enabled && settings.smartshunt_enabled && data.valid && !data.stale) {
            if (now_ms - last_battery_status_ms >= BATTERY_STATUS_PERIOD_MS) {
                last_battery_status_ms = now_ms;
                send_battery_status(settings, data, sid);
            }
            if (now_ms - last_dc_status_ms >= DC_STATUS_PERIOD_MS) {
                last_dc_status_ms = now_ms;
                send_dc_detailed_status(settings, data, sid);
                sid = sid >= 252 ? 0 : static_cast<uint8_t>(sid + 1);
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

    // The Waveshare board routes its onboard TJA1051 transceiver to TX GPIO6/RX GPIO0.
    // Device function 170 / class 35 identifies a battery monitor/electrical-generation node.
    // Manufacturer code 2046 is used for development only; production certification requires an assigned code.
    g_nmea2000.SetDeviceInformation(unique_device_number(), 170, 35, 2046);
    g_nmea2000.SetMode(tNMEA2000::N2km_ListenAndNode, 40);
    g_nmea2000.EnableForward(false);
    g_nmea2000.SetN2kCANMsgBufSize(20);
    if (!g_nmea2000.Open()) {
        ESP_LOGE(TAG, "Failed to open NMEA 2000/TWAI interface");
        return false;
    }

    if (xTaskCreate(n2k_task, "n2k", 6144, nullptr, 5, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create NMEA 2000 task");
        return false;
    }

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
