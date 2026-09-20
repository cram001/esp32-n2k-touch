/*
NMEA2000_esp32xx.h

Copyright (c) 2015-2020 Timo Lappalainen, Kave Oy, www.kave.fi
Copyright (c) 2023 Jaume Clarens "jiauka"

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "NMEA2000.h"
#include "N2kMsg.h"

class tNMEA2000_esp32xx : public tNMEA2000 {
private:
    bool is_open_ = false;
    static bool can_in_use_;

protected:
    gpio_num_t tx_pin_;
    gpio_num_t rx_pin_;

    bool CANSendFrame(unsigned long id, unsigned char len, const unsigned char *buf, bool wait_sent=true) override;
    bool CANOpen() override;
    bool CANGetFrame(unsigned long &id, unsigned char &len, unsigned char *buf) override;

public:
    explicit tNMEA2000_esp32xx(gpio_num_t tx_pin=GPIO_NUM_5, gpio_num_t rx_pin=GPIO_NUM_4);
};
