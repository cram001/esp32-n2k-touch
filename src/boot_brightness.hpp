#pragma once
#include <cstdint>

// LVGL supplies monotonic ticks and press/release events. Unsigned subtraction
// keeps timing correct across tick wrap; separate touches never accumulate.
class BootBrightnessRecovery {
public:
    void begin(uint32_t now) { started_=now;pressed_=attempted_=handled_=false; }
    void press(uint32_t now) { if(!pressed_){pressed_=true;pressed_at_=now;handled_=false;} }
    void release() { pressed_=false; }
    bool restore_due(uint32_t now) {
        if(!pressed_ || handled_ || now-pressed_at_<3000)return false;
        handled_=true;attempted_=true;attempt_at_=now;return true;
    }
    bool can_finish(uint32_t now) const {
        return now-started_>=8000 && !pressed_ && (!attempted_ || now-attempt_at_>=2000);
    }
private:
    uint32_t started_=0,pressed_at_=0,attempt_at_=0;
    bool pressed_=false,attempted_=false,handled_=false;
};
