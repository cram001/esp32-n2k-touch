#include "app_settings.hpp"

#include <algorithm>
#include <cstdio>
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
constexpr const char *KEY_DISPLAY = "display_v1";
constexpr const char *KEY_WIFI = "wifi_v1";
constexpr uint32_t SHUNTS_SCHEMA = 2;
constexpr uint32_t DISPLAY_SCHEMA = 1;
constexpr uint32_t WIFI_SCHEMA = 1;

struct PersistedSmartShunts {
    uint32_t schema = SHUNTS_SCHEMA;
    std::array<SmartShuntConfig, MAX_SMARTSHUNTS> devices{};
};

struct PersistedDisplayConfig {
    uint32_t schema = DISPLAY_SCHEMA;
    UnitsSettings units{};
    std::array<DataPageConfig, MAX_DATA_PAGES> pages{};
};

struct PersistedWifiConfig {
    uint32_t schema = WIFI_SCHEMA;
    WifiConfig config{};
};

uint8_t clamp_brightness(uint8_t value)
{
    return value > 100 ? 100 : value;
}

bool valid_layout(PageLayout layout)
{
    return layout == PageLayout::One || layout == PageLayout::Two ||
           layout == PageLayout::Four || layout == PageLayout::Six;
}

void apply_default_pages(AppSettings &settings)
{
    for (size_t i = 0; i < settings.pages.size(); ++i) {
        std::snprintf(settings.pages[i].name.data(), settings.pages[i].name.size(), "PAGE %u",
                      static_cast<unsigned>(i + 1));
    }
    settings.pages[0].enabled = true;
    settings.pages[0].layout = PageLayout::One;
    settings.pages[0].fields[0] = {DataSourceType::Nmea2000, DataMetric::Depth, 0};
}

void sanitize_display_settings(AppSettings &settings)
{
    if (static_cast<uint8_t>(settings.units.depth) > static_cast<uint8_t>(DepthUnit::Feet)) settings.units.depth = DepthUnit::Metres;
    if (static_cast<uint8_t>(settings.units.temperature) > static_cast<uint8_t>(TemperatureUnit::Fahrenheit)) settings.units.temperature = TemperatureUnit::Celsius;
    if (static_cast<uint8_t>(settings.units.wind_speed) > static_cast<uint8_t>(SpeedUnit::MetresPerSecond)) settings.units.wind_speed = SpeedUnit::Knots;
    if (static_cast<uint8_t>(settings.units.vessel_speed) > static_cast<uint8_t>(SpeedUnit::MetresPerSecond)) settings.units.vessel_speed = SpeedUnit::Knots;
    if (static_cast<uint8_t>(settings.units.distance) > static_cast<uint8_t>(DistanceUnit::Kilometres)) settings.units.distance = DistanceUnit::NauticalMiles;
    if (static_cast<uint8_t>(settings.units.short_distance) > static_cast<uint8_t>(ShortDistanceUnit::Yards)) settings.units.short_distance = ShortDistanceUnit::Metres;
    if (settings.units.short_distance_threshold_nm < 0.01f || settings.units.short_distance_threshold_nm > 1.0f) {
        settings.units.short_distance_threshold_nm = 0.2f;
    }

    for (size_t i = 0; i < settings.pages.size(); ++i) {
        auto &page = settings.pages[i];
        page.name.back() = '\0';
        if (page.name[0] == '\0') {
            std::snprintf(page.name.data(), page.name.size(), "PAGE %u", static_cast<unsigned>(i + 1));
        }
        if (!valid_layout(page.layout)) page.layout = PageLayout::Four;
        for (auto &field : page.fields) {
            if (static_cast<uint8_t>(field.source) > static_cast<uint8_t>(DataSourceType::SmartShunt)) field.source = DataSourceType::Nmea2000;
            if (static_cast<uint8_t>(field.metric) > static_cast<uint8_t>(DataMetric::BatteryTemperature)) field.metric = DataMetric::None;
            if (field.source_index >= MAX_SMARTSHUNTS) field.source_index = 0;
        }
    }
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
    apply_default_pages(settings);

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

    PersistedSmartShunts persisted_shunts{};
    size_t size = sizeof(persisted_shunts);
    if (nvs_get_blob(handle, KEY_SHUNTS, &persisted_shunts, &size) == ESP_OK &&
        size == sizeof(persisted_shunts) && persisted_shunts.schema == SHUNTS_SCHEMA) {
        settings.smartshunts = persisted_shunts.devices;
        for (auto &device : settings.smartshunts) {
            device.name.back() = '\0';
            device.mac.back() = '\0';
            device.bindkey.back() = '\0';
            if (device.battery_instance > 252) device.battery_instance = 0;
        }
    }

    PersistedDisplayConfig persisted_display{};
    size = sizeof(persisted_display);
    if (nvs_get_blob(handle, KEY_DISPLAY, &persisted_display, &size) == ESP_OK &&
        size == sizeof(persisted_display) && persisted_display.schema == DISPLAY_SCHEMA) {
        settings.units = persisted_display.units;
        settings.pages = persisted_display.pages;
    }
    PersistedWifiConfig persisted_wifi{};
    size = sizeof(persisted_wifi);
    if (nvs_get_blob(handle, KEY_WIFI, &persisted_wifi, &size) == ESP_OK &&
        size == sizeof(persisted_wifi) && persisted_wifi.schema == WIFI_SCHEMA) {
        settings.wifi = persisted_wifi.config;
        settings.wifi.ssid.back() = '\0';
        settings.wifi.password.back() = '\0';
    }

    sanitize_display_settings(settings);

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

    PersistedSmartShunts persisted_shunts{};
    persisted_shunts.devices = settings.smartshunts;

    PersistedDisplayConfig persisted_display{};
    persisted_display.units = settings.units;
    persisted_display.pages = settings.pages;

    PersistedWifiConfig persisted_wifi{};
    persisted_wifi.config = settings.wifi;

    err = nvs_set_u8(handle, KEY_THEME, static_cast<uint8_t>(settings.theme));
    if (err == ESP_OK) err = nvs_set_u8(handle, KEY_DAY_BRIGHTNESS, clamp_brightness(settings.day_brightness));
    if (err == ESP_OK) err = nvs_set_u8(handle, KEY_NIGHT_BRIGHTNESS, clamp_brightness(settings.night_brightness));
    if (err == ESP_OK) err = nvs_set_blob(handle, KEY_SHUNTS, &persisted_shunts, sizeof(persisted_shunts));
    if (err == ESP_OK) err = nvs_set_blob(handle, KEY_DISPLAY, &persisted_display, sizeof(persisted_display));
    if (err == ESP_OK) err = nvs_set_blob(handle, KEY_WIFI, &persisted_wifi, sizeof(persisted_wifi));
    if (err == ESP_OK) err = nvs_commit(handle);

    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed saving application settings: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}
