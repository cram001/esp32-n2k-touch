/*
NMEA2000 ESP32-S3 TWAI transport.

Derived from jiauka/NMEA2000_esp32xx.
Copyright (c) 2015-2020 Timo Lappalainen, Kave Oy
Copyright (c) 2023 Jaume Clarens "jiauka"

MIT License: permission is granted, free of charge, to use, copy, modify,
merge, publish, distribute, sublicense, and/or sell copies of this software,
subject to retaining this copyright and permission notice.
*/

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_twai.h"
#include "NMEA2000.h"
#include "N2kMsg.h"

class tNMEA2000_esp32xx : public tNMEA2000 {
private:
    struct tCANFrame {
        uint32_t id = 0;
        uint8_t len = 0;
        uint8_t buf[8]{};
    };

    bool is_open_ = false;
    static bool can_in_use_;
    gpio_num_t tx_pin_;
    gpio_num_t rx_pin_;
    twai_node_handle_t node_ = nullptr;
    QueueHandle_t rx_queue_ = nullptr;

    static bool rx_done_callback(twai_node_handle_t handle,
                                 const twai_rx_done_event_data_t *edata,
                                 void *user_ctx);

protected:
    bool CANSendFrame(unsigned long id, unsigned char len,
                      const unsigned char *buf, bool wait_sent=true) override;
    bool CANOpen() override;
    bool CANGetFrame(unsigned long &id, unsigned char &len,
                     unsigned char *buf) override;

public:
    explicit tNMEA2000_esp32xx(gpio_num_t tx_pin=GPIO_NUM_5,
                               gpio_num_t rx_pin=GPIO_NUM_4);
};
