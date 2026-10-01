#pragma once
#include "actisense_ascii.hpp"
#include "app_settings.hpp"

struct N2kInputStatus {
    std::array<char, 96> message{};
    uint32_t received = 0, rejected = 0, dropped = 0;
    int socket_error = 0;
};
bool n2k_input_start(const N2kInputConfig &config);
void n2k_input_apply(const N2kInputConfig &config);
bool n2k_input_receive(ActisenseMessage &message);
N2kInputStatus n2k_input_status();
