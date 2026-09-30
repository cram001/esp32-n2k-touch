/*
TWAI transport adapted from jiauka/NMEA2000_esp32xx for this ESP-IDF project.
Original copyright and MIT license are retained in NMEA2000_esp32xx.h.
Uses the current ESP-IDF esp_twai node API for ESP32-S3.
*/

#include "NMEA2000_esp32xx.h"

#include <algorithm>
#include <cstring>

#include "esp_twai_onchip.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/queue.h"

namespace {
constexpr const char *TAG = "n2k_twai";
constexpr size_t RX_QUEUE_DEPTH = 32;

struct RxFrame {
    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t data[8]{};
};
}

bool tNMEA2000_esp32xx::can_in_use_ = false;

tNMEA2000_esp32xx::tNMEA2000_esp32xx(gpio_num_t tx_pin, gpio_num_t rx_pin)
    : tNMEA2000(), tx_pin_(tx_pin), rx_pin_(rx_pin)
{
}

bool tNMEA2000_esp32xx::rx_done_cb(twai_node_handle_t handle,
                                   const twai_rx_done_event_data_t *,
                                   void *user_ctx)
{
    auto *self = static_cast<tNMEA2000_esp32xx *>(user_ctx);
    if (self == nullptr || self->rx_queue_ == nullptr) return false;

    uint8_t data[8]{};
    twai_frame_t frame{};
    frame.buffer = data;
    frame.buffer_len = sizeof(data);

    BaseType_t woken = pdFALSE;
    if (twai_node_receive_from_isr(handle, &frame) == ESP_OK) {
        RxFrame rx{};
        rx.id = frame.header.id;
        rx.len = static_cast<uint8_t>(std::min<size_t>(frame.buffer_len, sizeof(rx.data)));
        if (rx.len > 0) std::memcpy(rx.data, data, rx.len);
        xQueueSendFromISR(self->rx_queue_, &rx, &woken);
    }
    return woken == pdTRUE;
}

bool tNMEA2000_esp32xx::error_cb(twai_node_handle_t,
                                  const twai_error_event_data_t *edata,
                                  void *user_ctx)
{
    auto *self = static_cast<tNMEA2000_esp32xx *>(user_ctx);
    if (self != nullptr && edata != nullptr) self->last_error_flags_ = edata->err_flags.val;
    return false;
}

bool tNMEA2000_esp32xx::state_change_cb(twai_node_handle_t,
                                         const twai_state_change_event_data_t *edata,
                                         void *user_ctx)
{
    auto *self = static_cast<tNMEA2000_esp32xx *>(user_ctx);
    if (self != nullptr && edata != nullptr && edata->new_sta == TWAI_ERROR_BUS_OFF) {
        self->recovery_requested_ = true;
    }
    return false;
}

void tNMEA2000_esp32xx::service_bus_recovery()
{
    if (!recovery_requested_ || node_ == nullptr) return;
    recovery_requested_ = false;

    ESP_LOGW(TAG, "TWAI bus-off detected (error flags 0x%08lx); starting recovery",
             static_cast<unsigned long>(last_error_flags_));
    const esp_err_t err = twai_node_recover(node_);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "TWAI recovery request failed: %s", esp_err_to_name(err));
    }
}

bool tNMEA2000_esp32xx::CANSendFrame(unsigned long id, unsigned char len,
                                    const unsigned char *buf, bool)
{
    if (!is_open_ || node_ == nullptr) return false;
    service_bus_recovery();

    uint8_t data[8]{};
    const size_t data_len = std::min<size_t>(len, sizeof(data));
    if (buf != nullptr && data_len > 0) std::memcpy(data, buf, data_len);

    twai_frame_t frame{};
    frame.header.id = static_cast<uint32_t>(id);
    frame.header.ide = 1; // NMEA 2000 uses 29-bit extended CAN identifiers.
    frame.header.dlc = static_cast<uint16_t>(data_len);
    frame.buffer = data;
    frame.buffer_len = data_len;

    return twai_node_transmit(node_, &frame, 0) == ESP_OK;
}

bool tNMEA2000_esp32xx::CANOpen()
{
    if (is_open_) return true;
    if (can_in_use_) return false;

    rx_queue_ = xQueueCreate(RX_QUEUE_DEPTH, sizeof(RxFrame));
    if (rx_queue_ == nullptr) {
        ESP_LOGE(TAG, "Could not allocate TWAI RX queue");
        return false;
    }

    twai_onchip_node_config_t config{};
    config.io_cfg.tx = tx_pin_;
    config.io_cfg.rx = rx_pin_;
    config.io_cfg.quanta_clk_out = GPIO_NUM_NC;
    config.io_cfg.bus_off_indicator = GPIO_NUM_NC;
    config.bit_timing.bitrate = 250000;
    config.fail_retry_cnt = -1;
    config.tx_queue_depth = 20;

    esp_err_t err = twai_new_node_onchip(&config, &node_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI node creation failed: %s", esp_err_to_name(err));
        vQueueDelete(rx_queue_);
        rx_queue_ = nullptr;
        return false;
    }

    twai_event_callbacks_t callbacks{};
    callbacks.on_rx_done = rx_done_cb;
    callbacks.on_error = error_cb;
    callbacks.on_state_change = state_change_cb;
    err = twai_node_register_event_callbacks(node_, &callbacks, this);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI callback registration failed: %s", esp_err_to_name(err));
        twai_node_delete(node_);
        node_ = nullptr;
        vQueueDelete(rx_queue_);
        rx_queue_ = nullptr;
        return false;
    }

    // NMEA 2000 exclusively uses extended 29-bit CAN IDs. Reject standard and
    // CAN-FD traffic before it reaches the software queue.
    twai_mask_filter_config_t filter{};
    filter.id = 0;
    filter.mask = 0;
    filter.is_ext = true;
    filter.no_classic = false;
    filter.no_fd = true;
    err = twai_node_config_mask_filter(node_, 0, &filter);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI filter configuration failed: %s", esp_err_to_name(err));
        twai_node_delete(node_);
        node_ = nullptr;
        vQueueDelete(rx_queue_);
        rx_queue_ = nullptr;
        return false;
    }

    err = twai_node_enable(node_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI enable failed: %s", esp_err_to_name(err));
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

bool tNMEA2000_esp32xx::CANGetFrame(unsigned long &id, unsigned char &len, unsigned char *buf)
{
    if (!is_open_ || rx_queue_ == nullptr) return false;
    service_bus_recovery();

    RxFrame rx{};
    if (xQueueReceive(rx_queue_, &rx, 0) != pdTRUE) return false;

    id = rx.id;
    len = rx.len;
    if (buf != nullptr && len > 0) std::memcpy(buf, rx.data, len);
    return true;
}
