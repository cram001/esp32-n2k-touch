#include "app_settings.hpp"

#include <algorithm>
#include <cstring>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {
constexpr const char *TAG = "settings";
constexpr const char *NAMESPACE = "app";
constexpr const char *KEY_THEME = "theme";
constexpr const char *KEY_DAY_BRIGHTNESS = "day_br";
constexpr const char *KEY_NIGHT_BRIGHTNESS = "night_br";
constexpr const char *KEY_SHUNT_ENABLE = "shunt_en";
constexpr const char *KEY_SHUNT_N2K = "shunt_n2k";
constexpr const char *KEY_BAT_INSTANCE = "bat_inst";
constexpr const char *KEY_SHUNT_MAC = "shunt_mac";
constexpr const char *KEY_SHUNT_KEY = "shunt_key";

uint8_t clamp_brightness(uint8_t value)
{
    return value > 100 ? 100 : value;
}

template <size_t N>
void load_string(nvs_handle_t handle, const char *key, std::array<char, N> &target)
{
    size_t required = target.size();
    if (nvs_get_str(handle, key, target.data(), &required) != ESP_OK) {
        target.fill('\0');
    } else {
        target.back() = '\0';
    }
}

template <size_t N>
esp_err_t save_string(nvs_handle_t handle, const char *key, const std::array<char, N> &value)
{
    return nvs_set_str(handle, key, value.data());
}
}

bool settings_init()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS requires erase/reinitialization: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

AppSettings settings_load()
{
    AppSettings settings;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved application settings; using defaults");
        return settings;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to open application settings: %s", esp_err_to_name(err));
        return settings;
    }

    uint8_t value = 0;
    if (nvs_get_u8(handle, KEY_THEME, &value) == ESP_OK && value <= static_cast<uint8_t>(DisplayTheme::Night)) {
        settings.theme = static_cast<DisplayTheme>(value);
    }
    if (nvs_get_u8(handle, KEY_DAY_BRIGHTNESS, &value) == ESP_OK) {
        settings.day_brightness = clamp_brightness(value);
    }
    if (nvs_get_u8(handle, KEY_NIGHT_BRIGHTNESS, &value) == ESP_OK) {
        settings.night_brightness = clamp_brightness(value);
    }
    if (nvs_get_u8(handle, KEY_SHUNT_ENABLE, &value) == ESP_OK) {
        settings.smartshunt_enabled = value != 0;
    }
    if (nvs_get_u8(handle, KEY_SHUNT_N2K, &value) == ESP_OK) {
        settings.smartshunt_n2k_enabled = value != 0;
    }
    if (nvs_get_u8(handle, KEY_BAT_INSTANCE, &value) == ESP_OK) {
        settings.battery_instance = value;
    }

    load_string(handle, KEY_SHUNT_MAC, settings.smartshunt_mac);
    load_string(handle, KEY_SHUNT_KEY, settings.smartshunt_bindkey);

    nvs_close(handle);
    return settings;
}

bool settings_save(const AppSettings &settings)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to open application settings for write: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_u8(handle, KEY_THEME, static_cast<uint8_t>(settings.theme));
    if (err == ESP_OK) err = nvs_set_u8(handle, KEY_DAY_BRIGHTNESS, clamp_brightness(settings.day_brightness));
    if (err == ESP_OK) err = nvs_set_u8(handle, KEY_NIGHT_BRIGHTNESS, clamp_brightness(settings.night_brightness));
    if (err == ESP_OK) err = nvs_set_u8(handle, KEY_SHUNT_ENABLE, settings.smartshunt_enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_u8(handle, KEY_SHUNT_N2K, settings.smartshunt_n2k_enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_u8(handle, KEY_BAT_INSTANCE, settings.battery_instance);
    if (err == ESP_OK) err = save_string(handle, KEY_SHUNT_MAC, settings.smartshunt_mac);
    if (err == ESP_OK) err = save_string(handle, KEY_SHUNT_KEY, settings.smartshunt_bindkey);
    if (err == ESP_OK) err = nvs_commit(handle);

    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed saving application settings: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}
