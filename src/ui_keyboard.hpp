#pragma once
#include "lvgl.h"

// Used only by the existing LVGL owner. The widget stays allocated, so opening
// an editor does not require allocating another keyboard under transport load.
class UiKeyboard {
public:
    using Closed = void (*)();
    lv_obj_t *create(lv_obj_t *parent) {
        if(widget_) return widget_;
        widget_=lv_keyboard_create(parent);
        if(widget_) {
            lv_obj_add_flag(widget_,LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_event_cb(widget_,event_cb,LV_EVENT_ALL,this);
        }
        return widget_;
    }
    void watch_screen(lv_obj_t *screen) {
        lv_obj_add_event_cb(screen,unloaded_cb,LV_EVENT_SCREEN_UNLOADED,this);
    }
    void show(lv_obj_t *screen,lv_obj_t *textarea,lv_keyboard_mode_t mode,
              int width,int height,Closed closed=nullptr) {
        if(!widget_) return;
        hide();
        lv_obj_set_parent(widget_,screen);
        lv_obj_set_size(widget_,width,height);
        lv_obj_align(widget_,LV_ALIGN_BOTTOM_MID,0,0);
        lv_keyboard_set_mode(widget_,mode);
        lv_keyboard_set_textarea(widget_,textarea);
        closed_=closed;
        lv_obj_remove_flag(widget_,LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(widget_);
    }
    void hide(lv_obj_t *screen=nullptr) {
        if(!widget_ || (screen && lv_obj_get_parent(widget_)!=screen)) return;
        lv_obj_add_flag(widget_,LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(widget_,nullptr);
        const auto closed=closed_;closed_=nullptr;
        if(closed) closed();
    }
private:
    static void event_cb(lv_event_t *event) {
        const auto code=lv_event_get_code(event);
        if(code==LV_EVENT_READY || code==LV_EVENT_CANCEL)
            static_cast<UiKeyboard *>(lv_event_get_user_data(event))->hide();
    }
    static void unloaded_cb(lv_event_t *event) {
        static_cast<UiKeyboard *>(lv_event_get_user_data(event))->hide(lv_event_get_target_obj(event));
    }
    lv_obj_t *widget_=nullptr;
    Closed closed_=nullptr;
};
