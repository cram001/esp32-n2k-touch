/*
TWAI transport adapted from jiauka/NMEA2000_esp32xx for ESP-IDF 5.5+.
Uses the non-deprecated esp_driver_twai API.
*/

#include "NMEA2000_esp32xx.h"

#include <algorithm>
#include <cstring>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_twai_onchip.h"

namespace {
constexpr const char *TAG = "n2k_twai";
constexpr size_t RX_QUEUE_DEPTH = 64;
constexpr uint32_t N2K_BITRATE = 250000;
}

bool tNMEA2000_esp32xx::can_in_use_ = false;

tNMEA2000_esp32xx::tNMEA2000_esp32xx(gpio_num_t tx_pin, gpio_num_t rx_pin)
    : tNMEA2000(), tx_pin_(tx_pin), rx_pin_(rx_pin)
{
}

bool tNMEA2000_esp32xx::rx_done_callback(twai_node_handle_t handle,
                                         const twai_rx_done_event_data_t *,
                                         void *user_ctx)
{
    auto *self = static_cast<tNMEA2000_esp32xx *>(user_ctx);
    if (self == nullptr || self->rx_queue_ == nullptr) return false;

    uint8_t data[8]{};
    twai_frame_t frame{};
    frame.buffer = data;
    frame.buffer_len = sizeof(data);

    if (twai_node_receive_from_isr(handle, &frame) != ESP_OK) return false;

    tCANFrame queued{};
    queued.id = frame.header.id;
    queued.len = static_cast<uint8_t>(std::min<unsigned int>(frame.header.dlc, 8U));
    if (queued.len > 0) std::memcpy(queued.buf, data, queued.len);

    BaseType_t higher_priority_woken = pdFALSE;
    xQueueSendFromISR(self->rx_queue_, &queued, &higher_priority_woken);
    return higher_priority_woken == pdTRUE;
}

bool tNMEA2000_esp32xx::CANSendFrame(unsigned long id, unsigned char len,
                                    const unsigned char *buf, bool)
{
    if (node_ == nullptr) return false;

    uint8_t data[8]{};
    const uint8_t tx_len = static_cast<uint8_t>(std::min<unsigned int>(len, 8U));
    if (buf != nullptr && tx_len > 0) std::memcpy(data, buf, tx_len);

    twai_frame_t frame{};
    frame.header.id = id;
    frame.header.ide = true;
    frame.header.dlc = tx_len;
    frame.buffer = data;
    frame.buffer_len = tx_len;

    esp_err_t err = twai_node_transmit(node_, &frame, 10);
    if (err != ESP_OK) return false;

    // The new TWAI driver retains pointers to the frame buffer until TX completes.
    // Wait here so this stack-backed buffer remains valid for the full transfer.
    err = twai_node_transmit_wait_all_done(node_, 50);
    return err == ESP_OK;
}

bool tNMEA2000_esp32xx::CANOpen()
{
    if (is_open_) return true;
    if (can_in_use_) return false;

    rx_queue_ = xQueueCreate(RX_QUEUE_DEPTH, sizeof(tCANFrame));
    if (rx_queue_ == nullptr) {
        ESP_LOGE(TAG, "Could not allocate TWAI receive queue");
        return false;
    }

    twai_onchip_node_config_t config{};
    config.io_cfg.tx = tx_pin_;
    config.io_cfg.rx = rx_pin_;
    config.io_cfg.quanta_clk_out = GPIO_NUM_NC;
    config.io_cfg.bus_off_indicator = GPIO_NUM_NC;
    config.bit_timing.bitrate = N2K_BITRATE;
    config.fail_retry_cnt = -1;
    config.tx_queue_depth = 8;

    esp_err_t err = twai_new_node_onchip(&config, &node_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI node creation failed: %s", esp_err_to_name(err));
        vQueueDelete(rx_queue_);
        rx_queue_ = nullptr;
        node_ = nullptr;
        return false;
    }

    twai_event_callbacks_t callbacks{};
    callbacks.on_rx_done = rx_done_callback;
    err = twai_node_register_event_callbacks(node_, &callbacks, this);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI callback registration failed: %s", esp_err_to_name(err));
        twai_node_delete(node_);
        node_ = nullptr;
        vQueueDelete(rx_queue_);
        rx_queue_ = nullptr;
        return false;
    }

    err = twai_node_enable(node_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI node enable failed: %s", esp_err_to_name(err));
        twai_node_delete(node_);
        node_ = nullptr;
        vQueueDelete(rx_queue_);
        rx_queue_ = nullptr;
        return false;
    }

    is_open_ = true;
    can_in_use_ = true;
    ESP_LOGI(TAG, "TWAI active on TX GPIO%d / RX GPIO%d at 250 kbit/s",
             static_cast<int>(tx_pin_), static_cast<int>(rx_pin_));
    return true;
}

bool tNMEA2000_esp32xx::CANGetFrame(unsigned long &id, unsigned char &len,
                                   unsigned char *buf)
{
    if (rx_queue_ == nullptr) return false;

    tCANFrame frame{};
    if (xQueueReceive(rx_queue_, &frame, 0) != pdTRUE) return false;

    id = frame.id;
    len = frame.len;
    if (buf != nullptr && len > 0) std::memcpy(buf, frame.buf, len);
    return true;
}
