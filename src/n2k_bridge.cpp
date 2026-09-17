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
#include "instrument_data.hpp"
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

void handle_nmea_message(const tN2kMsg &msg)
{
    switch (msg.PGN) {
    case 127250L: {
        unsigned char sid; double heading, deviation, variation; tN2kHeadingReference ref;
        if (ParseN2kPGN127250(msg, sid, heading, deviation, variation, ref)) {
            instrument_data_update_nmea(DataMetric::Heading, heading);
        }
        break;
    }
    case 128259L: {
        unsigned char sid; double water, ground; tN2kSpeedWaterReferenceType ref;
        if (ParseN2kPGN128259(msg, sid, water, ground, ref) && water != N2kDoubleNA) {
            instrument_data_update_nmea(DataMetric::BoatSpeed, water);
        }
        break;
    }
    case 128267L: {
        unsigned char sid; double depth, offset, range;
        if (ParseN2kPGN128267(msg, sid, depth, offset, range) && depth != N2kDoubleNA) {
            instrument_data_update_nmea(DataMetric::Depth, depth);
        }
        break;
    }
    case 128275L: {
        uint16_t days; double seconds; uint32_t log, trip;
        if (ParseN2kPGN128275(msg, days, seconds, log, trip)) {
            // PGN 128275 log fields are metres.
            instrument_data_update_nmea(DataMetric::TripDistance, static_cast<double>(trip));
        }
        break;
    }
    case 129026L: {
        unsigned char sid; tN2kHeadingReference ref; double cog, sog;
        if (ParseN2kPGN129026(msg, sid, ref, cog, sog)) {
            if (cog != N2kDoubleNA) instrument_data_update_nmea(DataMetric::CourseOverGround, cog);
            if (sog != N2kDoubleNA) instrument_data_update_nmea(DataMetric::SpeedOverGround, sog);
        }
        break;
    }
    case 130306L: {
        unsigned char sid; double speed, angle; tN2kWindReference ref;
        if (ParseN2kPGN130306(msg, sid, speed, angle, ref)) {
            if (ref == N2kWind_Apparent) {
                if (speed != N2kDoubleNA) instrument_data_update_nmea(DataMetric::ApparentWindSpeed, speed);
                if (angle != N2kDoubleNA) instrument_data_update_nmea(DataMetric::ApparentWindAngle, angle);
            } else if (ref == N2kWind_True_boat || ref == N2kWind_True_water || ref == N2kWind_True_North || ref == N2kWind_Magnetic) {
                if (speed != N2kDoubleNA) instrument_data_update_nmea(DataMetric::TrueWindSpeed, speed);
                if (angle != N2kDoubleNA) instrument_data_update_nmea(DataMetric::TrueWindAngle, angle);
            }
        }
        break;
    }
    case 130312L: {
        unsigned char sid, instance; tN2kTempSource source; double actual, set;
        if (ParseN2kPGN130312(msg, sid, instance, source, actual, set) && actual != N2kDoubleNA) {
            if (source == N2kts_SeaTemperature) instrument_data_update_nmea(DataMetric::WaterTemperature, actual);
            else if (source == N2kts_OutsideTemperature) instrument_data_update_nmea(DataMetric::AirTemperature, actual);
        }
        break;
    }
    default:
        break;
    }
}

void send_battery_status(const SmartShuntConfig &cfg, const SmartShuntData &data, uint8_t sid)
{
    if (!data.voltage_valid && !data.current_valid && !data.temperature_valid) return;
    tN2kMsg message;
    SetN2kPGN127508(message, cfg.battery_instance,
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
    const uint8_t soc = data.soc_valid ? static_cast<uint8_t>(std::clamp(std::lround(data.soc_pct), 0L, 100L)) : 0xFF;
    const double time_remaining_s = data.time_to_go_valid ? static_cast<double>(data.time_to_go_min) * 60.0 : N2kDoubleNA;
    SetN2kDCStatus(message, sid, cfg.battery_instance, N2kDCt_Battery, soc, 0xFF,
                   time_remaining_s, N2kDoubleNA, N2kDoubleNA);
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
            if (!cfg.configured || !cfg.enabled || !cfg.n2k_enabled || duplicate_instance(settings, i)) continue;
            const SmartShuntData data = smartshunt_ble_get_data(i);
            if (!data.valid || data.stale) continue;
            if (now_ms - last_battery_ms[i] >= BATTERY_STATUS_PERIOD_MS) {
                last_battery_ms[i] = now_ms; send_battery_status(cfg, data, sid[i]);
            }
            if (now_ms - last_dc_ms[i] >= DC_STATUS_PERIOD_MS) {
                last_dc_ms[i] = now_ms; send_dc_status(cfg, data, sid[i]); sid[i] = sid[i] >= 252 ? 0 : static_cast<uint8_t>(sid[i] + 1);
            }
        }
        vTaskDelay(LOOP_DELAY);
    }
}
}

bool n2k_bridge_start(const AppSettings &settings)
{
    if (g_started) { n2k_bridge_apply_settings(settings); return true; }
    g_settings_mutex = xSemaphoreCreateMutex();
    if (g_settings_mutex == nullptr) return false;
    g_settings = settings;

    g_nmea2000.SetDeviceInformation(unique_device_number(), 170, 35, 2046);
    g_nmea2000.SetMode(tNMEA2000::N2km_ListenAndNode, 40);
    g_nmea2000.EnableForward(false);
    g_nmea2000.SetMsgHandler(handle_nmea_message);
    g_nmea2000.SetN2kCANMsgBufSize(20);
    if (!g_nmea2000.Open()) { ESP_LOGE(TAG, "Failed to open NMEA 2000/TWAI interface"); return false; }
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
