#pragma once
#include <cassert>
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NVS_NOT_FOUND=1, ESP_ERR_NVS_INVALID_LENGTH=2,
              ESP_ERR_NVS_NO_FREE_PAGES=3, ESP_ERR_NVS_NEW_VERSION_FOUND=4;
inline const char *esp_err_to_name(int) {return "fake error";}
#define ESP_ERROR_CHECK(e) assert((e)==ESP_OK)
