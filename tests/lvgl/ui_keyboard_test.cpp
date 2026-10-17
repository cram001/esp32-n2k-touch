#include "ui_keyboard.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

static unsigned closed=0;
static void on_closed(){++closed;}
static void key(lv_obj_t *keyboard,const char *text){
    for(unsigned i=0;i<100;++i){
        const char *button=lv_buttonmatrix_get_button_text(keyboard,i);
        if(!button)break;
        if(std::strcmp(button,text)==0){
            lv_buttonmatrix_set_selected_button(keyboard,i);
            lv_obj_send_event(keyboard,LV_EVENT_VALUE_CHANGED,nullptr);return;
        }
    }
    std::fprintf(stderr,"Missing key: %s\n",text);
    assert(false && "key missing from real LVGL keyboard map");
}
static size_t free_memory(){lv_mem_monitor_t m{};lv_mem_monitor(&m);return m.free_size;}
int main(){
    lv_init();
    auto *display=lv_display_create(480,480);assert(display);
    auto *home=lv_screen_active();
    std::array<lv_obj_t *,5> screens{},fields{};
    UiKeyboard owner;
    auto *keyboard=owner.create(home);assert(keyboard);
    assert(owner.create(home)==keyboard);
    for(size_t i=0;i<screens.size();++i){
        screens[i]=lv_obj_create(nullptr);fields[i]=lv_textarea_create(screens[i]);
        lv_textarea_set_one_line(fields[i],true);owner.watch_screen(screens[i]);
    }
    lv_textarea_set_accepted_chars(fields[0],"0123456789.");
    lv_textarea_set_password_mode(fields[3],true);
    lv_textarea_set_password_show_time(fields[3],0);
    lv_screen_load(screens[0]);
    owner.show(screens[0],fields[0],LV_KEYBOARD_MODE_NUMBER,460,220);
    key(keyboard,"3");key(keyboard,".");key(keyboard,"5");
    assert(std::strcmp(lv_textarea_get_text(fields[0]),"3.5")==0);
    key(keyboard,LV_SYMBOL_OK);
    assert(lv_obj_has_flag(keyboard,LV_OBJ_FLAG_HIDDEN));
    assert(!lv_keyboard_get_textarea(keyboard));

    // Switching from numeric to text must reset the keyboard map and target.
    lv_screen_load(screens[3]);
    owner.show(screens[3],fields[3],LV_KEYBOARD_MODE_TEXT_LOWER,460,205);
    key(keyboard,"a");
    assert(std::strcmp(lv_textarea_get_text(fields[3]),"a")==0);
    assert(std::strcmp(lv_textarea_get_text(fields[0]),"3.5")==0);
    assert(lv_textarea_get_password_mode(fields[3]));
    key(keyboard,LV_SYMBOL_KEYBOARD);
    assert(!lv_keyboard_get_textarea(keyboard));

    // Unloading closes the editor; a late unload from another screen cannot
    // detach the new editor's target. Tile-label cleanup runs once on close.
    lv_screen_load(screens[2]);
    owner.show(screens[2],fields[2],LV_KEYBOARD_MODE_TEXT_LOWER,480,200,on_closed);
    lv_screen_load(home);
    assert(closed==1 && !lv_keyboard_get_textarea(keyboard));
    owner.show(screens[1],fields[1],LV_KEYBOARD_MODE_NUMBER,460,220);
    lv_obj_send_event(screens[2],LV_EVENT_SCREEN_UNLOADED,nullptr);
    assert(lv_keyboard_get_textarea(keyboard)==fields[1]);
    owner.hide(screens[2]);assert(lv_keyboard_get_textarea(keyboard)==fields[1]);
    owner.hide(screens[1]);

    auto cycle=[&](){
        for(size_t i=0;i<screens.size();++i){
            lv_screen_load(screens[i]);
            owner.show(screens[i],fields[i],i<2?LV_KEYBOARD_MODE_NUMBER:LV_KEYBOARD_MODE_TEXT_LOWER,460,205);
            assert(lv_obj_get_parent(keyboard)==screens[i]);
            assert(lv_keyboard_get_textarea(keyboard)==fields[i]);
            lv_obj_update_layout(screens[i]);
            owner.hide(screens[i]);
        }
        lv_screen_load(home);lv_obj_update_layout(home);
    };
    cycle();const auto before=free_memory();
    for(unsigned i=0;i<200;++i)cycle();
    const auto after=free_memory();assert(after==before);

    // Quantify widget allocations on this host allocator, not ESP internal heap.
    const auto one=free_memory();
    std::array<lv_obj_t *,4> duplicates{};
    for(auto &duplicate:duplicates)duplicate=lv_keyboard_create(home);
    const auto five=free_memory();assert(five<one);
    for(auto *duplicate:duplicates)lv_obj_delete(duplicate);
    assert(free_memory()==one);
    std::printf("PASS: real LVGL numeric/text typing, password mode, target isolation, READY/CANCEL, unload cleanup, 1000 editor switches without heap growth; four duplicate keyboards consume %zu host allocator bytes\n",one-five);
    return 0;
}
