#pragma once
#include <cstdint>
extern int64_t fake_time_us;
inline int64_t esp_timer_get_time() { return fake_time_us; }
