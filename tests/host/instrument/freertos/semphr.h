#pragma once
#include <mutex>
#include <cstdint>
using StaticSemaphore_t=std::mutex;
using SemaphoreHandle_t=std::mutex *;
constexpr uint32_t portMAX_DELAY=UINT32_MAX;
constexpr int pdTRUE=1;
inline unsigned fake_mutex_takes=0;
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *s){return s;}
inline int xSemaphoreTake(SemaphoreHandle_t s,uint32_t){++fake_mutex_takes;s->lock();return pdTRUE;}
inline void xSemaphoreGive(SemaphoreHandle_t s){s->unlock();}
