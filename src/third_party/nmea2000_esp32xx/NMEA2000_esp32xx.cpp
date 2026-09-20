/*
TWAI transport adapted from jiauka/NMEA2000_esp32xx for this ESP-IDF project.
Original copyright and MIT license are retained in NMEA2000_esp32xx.h.
*/

#include "NMEA2000_esp32xx.h"

#include <algorithm>
#include <cstring>

#include "driver/twai.h"
#include "esp_err.h"
#include "esp_log.h"

namespace {
constexpr const char *TAG = "n2k_twai";
}

bool tNMEA2000_esp32xx::can_in_use_ = false;

tNMEA2000_esp32xx::tNMEA2000_esp32xx(gpio_num_t tx_pin, gpio_num_t rx_pin)
    : tNMEA2000(), tx_pin_(tx_pin), rx_pin_(rx_pin)
{
}

bool tNMEA2000_esp32xx::CANSendFrame(unsigned long id, unsigned char len,
                                    const unsigned char *buf, bool)
{
    twai_message_t msg{};
    msg.extd = 1;
    msg.ss = 1;
    msg.identifier = id;
    msg.data_length_code = static_cast<uint8_t>(std::min<unsigned int>(len, 8U));
    if (buf != nullptr && msg.data_length_code > 0) {
        std::memcpy(msg.data, buf, msg.data_length_code);
    }
    return twai_transmit(&msg, 0) == ESP_OK;
}

bool tNMEA2000_esp32xx::CANOpen()
{
    if (is_open_) return true;
    if (can_in_use_) return false;

    twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(tx_pin_, rx_pin_, TWAI_MODE_NORMAL);
    twai_timing_config_t timing = TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&general, &timing, &filter);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI install failed: %s", esp_err_to_name(err));
        return false;
    }

    err = twai_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI start failed: %s", esp_err_to_name(err));
        twai_driver_uninstall();
        return false;
    }

    is_open_ = true;
    can_in_use_ = true;
    ESP_LOGI(TAG, "TWAI active on TX GPIO%d / RX GPIO%d at 250 kbit/s",
             static_cast<int>(tx_pin_), static_cast<int>(rx_pin_));
    return true;
}

bool tNMEA2000_esp32xx::CANGetFrame(unsigned long &id, unsigned char &len, unsigned char *buf)
{
    twai_message_t msg{};
    if (twai_receive(&msg, 0) != ESP_OK) return false;

    id = msg.identifier;
    len = static_cast<unsigned char>(std::min<unsigned int>(msg.data_length_code, 8U));
    if (buf != nullptr && len > 0) std::memcpy(buf, msg.data, len);
    return true;
}
