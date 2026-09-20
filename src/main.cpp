#include "app_settings.hpp"
#include "n2k_bridge.hpp"
#include "smartshunt_ble.hpp"
#include "ota.hpp"
#include "wifi_service.hpp"
#include "ui.hpp"

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "app";
constexpr gpio_num_t BOARD_I2C_SDA = GPIO_NUM_15;
constexpr gpio_num_t BOARD_I2C_SCL = GPIO_NUM_7;

void board_i2c_recover()
{
    gpio_config_t io_conf{};
    io_conf.pin_bit_mask = (1ULL << BOARD_I2C_SDA) | (1ULL << BOARD_I2C_SCL);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    esp_rom_delay_us(20);

    ESP_ERROR_CHECK(gpio_set_direction(BOARD_I2C_SCL, GPIO_MODE_OUTPUT_OD));
    ESP_ERROR_CHECK(gpio_set_pull_mode(BOARD_I2C_SCL, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(gpio_set_level(BOARD_I2C_SCL, 1));
    esp_rom_delay_us(10);
    for (int i = 0; i < 9 && gpio_get_level(BOARD_I2C_SDA) == 0; ++i) {
        ESP_ERROR_CHECK(gpio_set_level(BOARD_I2C_SCL, 0));
        esp_rom_delay_us(10);
        ESP_ERROR_CHECK(gpio_set_level(BOARD_I2C_SCL, 1));
        esp_rom_delay_us(10);
    }

    ESP_ERROR_CHECK(gpio_set_direction(BOARD_I2C_SDA, GPIO_MODE_OUTPUT_OD));
    ESP_ERROR_CHECK(gpio_set_pull_mode(BOARD_I2C_SDA, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(gpio_set_level(BOARD_I2C_SDA, 0));
    esp_rom_delay_us(10);
    ESP_ERROR_CHECK(gpio_set_level(BOARD_I2C_SCL, 1));
    esp_rom_delay_us(10);
    ESP_ERROR_CHECK(gpio_set_level(BOARD_I2C_SDA, 1));
    esp_rom_delay_us(10);

    ESP_ERROR_CHECK(gpio_set_direction(BOARD_I2C_SDA, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_direction(BOARD_I2C_SCL, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(BOARD_I2C_SDA, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(gpio_set_pull_mode(BOARD_I2C_SCL, GPIO_PULLUP_ONLY));
    vTaskDelay(pdMS_TO_TICKS(20));
}
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting esp32-n2k-touch");

    if (!settings_init()) {
        ESP_LOGW(TAG, "Continuing with default settings because NVS initialization failed");
    }
    const AppSettings settings = settings_load();

    board_i2c_recover();
    if (bsp_display_start() == nullptr) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }
    bsp_display_lock(0);
    ui_start(settings);
    bsp_display_unlock();
    ESP_LOGI(TAG, "Display and touch UI initialized");

    const bool wifi_ok = wifi_service_start(settings.wifi);
    if (!wifi_ok) ESP_LOGE(TAG, "Wi-Fi service failed to initialize");

    const bool ble_ok = smartshunt_ble_start(settings);
    if (!ble_ok) ESP_LOGE(TAG, "SmartShunt BLE service failed to initialize");

    const bool n2k_ok = n2k_bridge_start(settings);
    if (!n2k_ok) ESP_LOGE(TAG, "NMEA 2000 service failed to initialize");

    // A newly installed image is confirmed only after all core services have
    // initialized. If any service fails, leave the image pending so a reboot
    // can trigger bootloader rollback to the previous slot.
    if (wifi_ok && ble_ok && n2k_ok) {
        ota_confirm_running_image();
    } else {
        ESP_LOGE(TAG, "OTA image left unconfirmed because startup health checks failed");
    }
}
