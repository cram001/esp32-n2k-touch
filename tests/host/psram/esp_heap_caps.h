#pragma once
#include <cstddef>
#include <cstdint>
constexpr uint32_t MALLOC_CAP_SPIRAM=1U<<10;
constexpr uint32_t MALLOC_CAP_8BIT=1U<<2;
void *heap_caps_malloc(size_t bytes,uint32_t capabilities);
void heap_caps_free(void *memory);
