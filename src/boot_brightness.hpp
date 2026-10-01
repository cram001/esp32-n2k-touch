#pragma once
#include <cstdint>

// LVGL supplies monotonic ticks and press/release events. Unsigned subtraction
// keeps timing correct across tick wrap; separate touches never accumulate.
class BootBrightnessRecovery {
public:
    void begin(uint32_t now) { started_=now;pressed_=attempted_=handled_=failed_=false; }
    void press(uint32_t now) { if(!pressed_){pressed_=true;pressed_at_=now;handled_=false;} }
    void release(uint32_t now) { pressed_=false;if(failed_)attempt_at_=now; }
    // Start the visible confirmation after NVS returns. On failure, release()
    // starts a fresh eight-second retry window even after a very long hold.
    void attempt_completed(bool saved,uint32_t now) { failed_=!saved;attempt_at_=now; }
    bool restore_due(uint32_t now) {
        if(!pressed_ || handled_ || now-pressed_at_<3000)return false;
        handled_=true;attempted_=true;attempt_at_=now;return true;
    }
    bool can_finish(uint32_t now) const {
        return now-started_>=8000 && !pressed_ && (!attempted_ || now-attempt_at_>=(failed_?8000U:2000U));
    }
private:
    uint32_t started_=0,pressed_at_=0,attempt_at_=0;
    bool pressed_=false,attempted_=false,handled_=false,failed_=false;
};
