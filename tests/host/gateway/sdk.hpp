#pragma once
#include <cstdint>
#include <cstddef>
using TickType_t = uint32_t;
using SemaphoreHandle_t = void *;
using QueueHandle_t = void *;
constexpr int pdTRUE = 1, pdPASS = 1;
constexpr uint32_t portMAX_DELAY = UINT32_MAX, portTICK_PERIOD_MS = 1;
#define pdMS_TO_TICKS(ms) (ms)
SemaphoreHandle_t xSemaphoreCreateMutex();
int xSemaphoreTake(SemaphoreHandle_t, uint32_t);
void xSemaphoreGive(SemaphoreHandle_t);
void vSemaphoreDelete(SemaphoreHandle_t);
QueueHandle_t xQueueCreate(unsigned, size_t);
int xQueueSend(QueueHandle_t, const void *, uint32_t);
int xQueueReceive(QueueHandle_t, void *, uint32_t);
void vQueueDelete(QueueHandle_t);
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, int, void *);
TickType_t xTaskGetTickCount();
void vTaskDelay(uint32_t);
