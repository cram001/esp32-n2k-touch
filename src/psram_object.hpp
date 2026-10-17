#pragma once
#include <cstddef>
#include <cstdlib>
#include <new>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#endif

// Boot-lifetime storage initialized by its sole task owner, or before that task
// starts. No internal-RAM fallback: callers must handle allocation failure.
template<class T> class PsramObject {
public:
    PsramObject()=default;
    PsramObject(const PsramObject &)=delete;
    PsramObject &operator=(const PsramObject &)=delete;
    ~PsramObject(){
        if(!value_)return;
        value_->~T();release(value_);
    }
    bool initialize(){
        if(value_)return true;
#ifdef ESP_PLATFORM
        void *memory=heap_caps_malloc(sizeof(T),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(memory && !esp_ptr_external_ram(memory)){release(memory);return false;}
#else
        // Native regression tests exercise the same table/constructor behavior.
        void *memory=std::malloc(sizeof(T));
#endif
        if(!memory)return false;
        value_=::new(memory) T{};
        return true;
    }
    T *get(){return value_;}
    const T *get()const{return value_;}
private:
    static void release(void *memory){
#ifdef ESP_PLATFORM
        heap_caps_free(memory);
#else
        std::free(memory);
#endif
    }
    T *value_=nullptr;
};
