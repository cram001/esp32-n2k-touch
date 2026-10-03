#include "app_settings.hpp"
#include "n2k_bridge.hpp"
#include "n2k_sources.hpp"
#include "smartshunt_ble.hpp"
#include "wifi_service.hpp"
#include "ui.hpp"
#include "ota.hpp"
#include "local_server_retry.hpp"

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
static_assert(CONFIG_ESP_MAIN_TASK_STACK_SIZE >= 8192,
              "Startup requires 8192 bytes of stack; regenerate sdkconfig before building");
constexpr const char *TAG = "app";
constexpr gpio_num_t BOARD_I2C_SDA = GPIO_NUM_15;
constexpr gpio_num_t BOARD_I2C_SCL = GPIO_NUM_7;
bool g_startup_prerequisites=false; // assigned before health task creation

void startup_health_task(void *) {
    const int64_t started=esp_timer_get_time();
    for(;;) {
        const auto decision=startup_health_decision(g_startup_prerequisites,
            ui_is_healthy(),esp_timer_get_time()-started);
        if(decision==StartupHealthDecision::Confirm) {
            ESP_LOGI(TAG,"Startup health passed: core services and UI healthy");
            ota_confirm_running_image();break;
        }
        if(decision==StartupHealthDecision::Reject) {
            ESP_LOGE(TAG,"Startup health failed: prerequisites or HTTP/UI readiness deadline");
            ota_reject_running_image();break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(nullptr);
}

bool board_i2c_recover()
{
    auto try_gpio = [](esp_err_t err, const char *operation) {
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "I2C recovery: %s failed: %s", operation, esp_err_to_name(err));
            return false;
        }
        return true;
    };

    gpio_config_t io_conf{};
    io_conf.pin_bit_mask = (1ULL << BOARD_I2C_SDA) | (1ULL << BOARD_I2C_SCL);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    if (!try_gpio(gpio_config(&io_conf), "initial GPIO config")) return false;
    esp_rom_delay_us(20);

    if (!try_gpio(gpio_set_direction(BOARD_I2C_SCL, GPIO_MODE_OUTPUT_OD), "SCL output mode")) return false;
    if (!try_gpio(gpio_set_pull_mode(BOARD_I2C_SCL, GPIO_PULLUP_ONLY), "SCL pull-up")) return false;
    if (!try_gpio(gpio_set_level(BOARD_I2C_SCL, 1), "SCL high")) return false;
    esp_rom_delay_us(10);

    for (int i = 0; i < 9 && gpio_get_level(BOARD_I2C_SDA) == 0; ++i) {
        if (!try_gpio(gpio_set_level(BOARD_I2C_SCL, 0), "SCL recovery low")) break;
        esp_rom_delay_us(10);
        if (!try_gpio(gpio_set_level(BOARD_I2C_SCL, 1), "SCL recovery high")) break;
        esp_rom_delay_us(10);
    }

    bool ok = true;
    ok &= try_gpio(gpio_set_direction(BOARD_I2C_SDA, GPIO_MODE_OUTPUT_OD), "SDA output mode");
    ok &= try_gpio(gpio_set_pull_mode(BOARD_I2C_SDA, GPIO_PULLUP_ONLY), "SDA pull-up");
    ok &= try_gpio(gpio_set_level(BOARD_I2C_SDA, 0), "SDA low");
    esp_rom_delay_us(10);
    ok &= try_gpio(gpio_set_level(BOARD_I2C_SCL, 1), "SCL stop high");
    esp_rom_delay_us(10);
    ok &= try_gpio(gpio_set_level(BOARD_I2C_SDA, 1), "SDA stop high");
    esp_rom_delay_us(10);
    ok &= try_gpio(gpio_set_direction(BOARD_I2C_SDA, GPIO_MODE_INPUT), "SDA release");
    ok &= try_gpio(gpio_set_direction(BOARD_I2C_SCL, GPIO_MODE_INPUT), "SCL release");
    ok &= try_gpio(gpio_set_pull_mode(BOARD_I2C_SDA, GPIO_PULLUP_ONLY), "SDA final pull-up");
    ok &= try_gpio(gpio_set_pull_mode(BOARD_I2C_SCL, GPIO_PULLUP_ONLY), "SCL final pull-up");
    vTaskDelay(pdMS_TO_TICKS(20));
    return ok;
}
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting esp32-n2k-touch");
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "Firmware %s; built %s %s", app->version, app->date, app->time);
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t image_state{};
    const esp_err_t state_error = esp_ota_get_state_partition(running, &image_state);
    ESP_LOGI(TAG, "Boot slot %s; OTA state %d (query: %s)", running ? running->label : "unknown",
             state_error == ESP_OK ? static_cast<int>(image_state) : -1, esp_err_to_name(state_error));
    if (state_error != ESP_OK || image_state == ESP_OTA_IMG_UNDEFINED) {
        ESP_LOGW(TAG, "Boot is not an OTA trial; USB flashing does not establish automatic rollback");
    }

    const bool settings_ok = settings_init();
    if (!settings_ok) {
        ESP_LOGW(TAG, "Continuing with default settings because NVS initialization failed");
    }
    const AppSettings settings = settings_load();
    const bool sources_ok = settings_ok && n2k_sources_init();
    ota_prepare();
    wifi_service_prepare();

    if (!board_i2c_recover()) {
        ESP_LOGW(TAG, "I2C recovery was incomplete; continuing so BSP initialization can report the real bus error");
    }
    lv_display_t *display = bsp_display_start();
    if (display == nullptr) {
        ESP_LOGE(TAG, "Display initialization failed");
        ota_reject_running_image();
        return;
    }
    if (!bsp_display_lock(pdMS_TO_TICKS(1000))) {
        ESP_LOGE(TAG, "Timed out acquiring LVGL display lock; UI construction aborted");
        ota_reject_running_image();
        return;
    }
    // The LVGL port rotates pixels; LVGL rotates the associated input points.
    bsp_display_rotate(display, settings.rotation == DisplayRotation::Normal ? LV_DISPLAY_ROTATION_0 : LV_DISPLAY_ROTATION_180);
    ui_start(settings);
    bsp_display_unlock();
    ESP_LOGI(TAG, "Display and touch UI initialized");
    ESP_LOGI(TAG, "Main stack minimum free after settings/UI: %u bytes", static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));

    const bool wifi_ok = wifi_service_start(settings.wifi);
    if (!wifi_ok) ESP_LOGE(TAG, "Wi-Fi service failed to initialize");

    // The local firmware web server is intentionally not started at boot.
    // It is enabled by the user for a 120-second update window and pauses BLE.
    const bool ota_layout_ok=ota_partition_layout_valid();

    const bool ble_ok = smartshunt_ble_start(settings);
    if (!ble_ok) ESP_LOGE(TAG, "SmartShunt BLE service failed to initialize");

    const bool n2k_ok = n2k_bridge_start(settings);
    if (!n2k_ok) ESP_LOGE(TAG, "NMEA 2000 service failed to initialize");


    ESP_LOGI(TAG, "Main stack minimum free after services: %u bytes", static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));

    // Confirm only after core services initialize. The on-demand HTTP server is
    // deliberately not a boot-health prerequisite.
    g_startup_prerequisites=settings_ok && sources_ok && wifi_ok && ble_ok && n2k_ok && ota_layout_ok;
    if(xTaskCreate(startup_health_task,"startup_health",4096,nullptr,1,nullptr)!=pdPASS) {
        ESP_LOGE(TAG,"Startup health task allocation failed; rejecting pending OTA image");
        ota_reject_running_image();
    }
}
