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
constexpr const char *KEY_SHUNTS = "shunts_v2";
constexpr uint32_t SHUNTS_SCHEMA = 2;

struct PersistedSmartShunts {
    uint32_t schema = SHUNTS_SCHEMA;
    std::array<SmartShuntConfig, MAX_SMARTSHUNTS> devices{};
};

uint8_t clamp_brightness(uint8_t value)
{
    return value > 100 ? 100 : value;
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
    if (err == ESP_ERR_NVS_NOT_FOUND) return settings;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to open application settings: %s", esp_err_to_name(err));
        return settings;
    }

    uint8_t value = 0;
    if (nvs_get_u8(handle, KEY_THEME, &value) == ESP_OK && value <= static_cast<uint8_t>(DisplayTheme::Night)) {
        settings.theme = static_cast<DisplayTheme>(value);
    }
    if (nvs_get_u8(handle, KEY_DAY_BRIGHTNESS, &value) == ESP_OK) settings.day_brightness = clamp_brightness(value);
    if (nvs_get_u8(handle, KEY_NIGHT_BRIGHTNESS, &value) == ESP_OK) settings.night_brightness = clamp_brightness(value);

    PersistedSmartShunts persisted{};
    size_t size = sizeof(persisted);
    if (nvs_get_blob(handle, KEY_SHUNTS, &persisted, &size) == ESP_OK &&
        size == sizeof(persisted) && persisted.schema == SHUNTS_SCHEMA) {
        settings.smartshunts = persisted.devices;
        for (auto &device : settings.smartshunts) {
            device.name.back() = '\0';
            device.mac.back() = '\0';
            device.bindkey.back() = '\0';
            if (device.battery_instance > 252) device.battery_instance = 0;
        }
    }

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

    PersistedSmartShunts persisted{};
    persisted.devices = settings.smartshunts;

    err = nvs_set_u8(handle, KEY_THEME, static_cast<uint8_t>(settings.theme));
    if (err == ESP_OK) err = nvs_set_u8(handle, KEY_DAY_BRIGHTNESS, clamp_brightness(settings.day_brightness));
    if (err == ESP_OK) err = nvs_set_u8(handle, KEY_NIGHT_BRIGHTNESS, clamp_brightness(settings.night_brightness));
    if (err == ESP_OK) err = nvs_set_blob(handle, KEY_SHUNTS, &persisted, sizeof(persisted));
    if (err == ESP_OK) err = nvs_commit(handle);

    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed saving application settings: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}
