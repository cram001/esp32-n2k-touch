#include "ota.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "ota";
constexpr size_t OTA_URL_MAX = 256;
constexpr uint32_t OTA_TASK_STACK = 8192;

SemaphoreHandle_t g_mutex = nullptr;
OtaStatus g_status{};
char g_url[OTA_URL_MAX]{};
bool g_active = false;

void ensure_mutex()
{
    if (g_mutex == nullptr) g_mutex = xSemaphoreCreateMutex();
}

void set_active(bool active)
{
    ensure_mutex();
    if (g_mutex != nullptr && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_active = active;
        xSemaphoreGive(g_mutex);
    }
}

bool claim_update(const char *url)
{
    ensure_mutex();
    if (g_mutex == nullptr) return false;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    if (g_active) {
        xSemaphoreGive(g_mutex);
        return false;
    }
    g_active = true;
    std::snprintf(g_url, sizeof(g_url), "%s", url);
    xSemaphoreGive(g_mutex);
    return true;
}

void url_snapshot(char *out, size_t out_size)
{
    if (out == nullptr || out_size == 0) return;
    out[0] = '\0';
    ensure_mutex();
    if (g_mutex != nullptr && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        std::snprintf(out, out_size, "%s", g_url);
        xSemaphoreGive(g_mutex);
    }
}

void set_status(OtaState state, int progress, esp_err_t err, const char *message)
{
    ensure_mutex();
    if (g_mutex != nullptr && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_status.state = state;
        g_status.progress_percent = std::clamp(progress, 0, 100);
        g_status.last_error = static_cast<int>(err);
        std::snprintf(g_status.message, sizeof(g_status.message), "%s", message ? message : "");
        xSemaphoreGive(g_mutex);
    }
}

void ota_task(void *)
{
    set_status(OtaState::Starting, 0, ESP_OK, "Connecting");

    char url[OTA_URL_MAX]{};
    url_snapshot(url, sizeof(url));

    esp_http_client_config_t http_config{};
    http_config.url = url;
    http_config.crt_bundle_attach = esp_crt_bundle_attach;
    http_config.timeout_ms = 15000;
    http_config.keep_alive_enable = true;

    esp_https_ota_config_t ota_config{};
    ota_config.http_config = &http_config;

    esp_https_ota_handle_t handle = nullptr;
    esp_err_t err = esp_https_ota_begin(&ota_config, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(err));
        set_status(OtaState::Failed, 0, err, "OTA connection failed");
        set_active(false);
        vTaskDelete(nullptr);
        return;
    }

    esp_app_desc_t candidate{};
    err = esp_https_ota_get_img_desc(handle, &candidate);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not read OTA image descriptor: %s", esp_err_to_name(err));
        esp_https_ota_abort(handle);
        set_status(OtaState::Failed, 0, err, "Invalid firmware image");
        set_active(false);
        vTaskDelete(nullptr);
        return;
    }

    const esp_app_desc_t *running = esp_app_get_description();
    if (running == nullptr || std::strncmp(candidate.project_name, running->project_name,
                                           sizeof(candidate.project_name)) != 0) {
        ESP_LOGE(TAG, "Rejecting OTA image for project '%s' (running '%s')",
                 candidate.project_name, running ? running->project_name : "unknown");
        esp_https_ota_abort(handle);
        set_status(OtaState::Failed, 0, ESP_ERR_INVALID_ARG, "Wrong firmware project");
        set_active(false);
        vTaskDelete(nullptr);
        return;
    }

    if (candidate.secure_version < running->secure_version) {
        ESP_LOGE(TAG, "Rejecting OTA image with lower secure version");
        esp_https_ota_abort(handle);
        set_status(OtaState::Failed, 0, ESP_ERR_INVALID_VERSION, "Firmware security downgrade");
        set_active(false);
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "OTA candidate %s version %s", candidate.project_name, candidate.version);
    const int image_size = esp_https_ota_get_image_size(handle);
    set_status(OtaState::Downloading, 0, ESP_OK, "Downloading");

    while ((err = esp_https_ota_perform(handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        const int read = esp_https_ota_get_image_len_read(handle);
        int percent = 0;
        if (image_size > 0) percent = static_cast<int>((static_cast<int64_t>(read) * 100) / image_size);
        set_status(OtaState::Downloading, percent, ESP_OK, "Downloading");
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA download failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(handle);
        set_status(OtaState::Failed, 0, err, "OTA download failed");
        set_active(false);
        vTaskDelete(nullptr);
        return;
    }

    set_status(OtaState::Validating, 100, ESP_OK, "Validating");
    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA validation/finalization failed: %s", esp_err_to_name(err));
        set_status(OtaState::Failed, 100, err, "OTA validation failed");
        set_active(false);
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "OTA image installed successfully; reboot required");
    set_status(OtaState::ReadyToReboot, 100, ESP_OK, "Update ready - reboot");
    set_active(false);
    vTaskDelete(nullptr);
}
} // namespace

void ota_confirm_running_image()
{
    ensure_mutex();

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state{};
    const esp_err_t err = esp_ota_get_state_partition(running, &state);
    if (err == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        const esp_err_t confirm = esp_ota_mark_app_valid_cancel_rollback();
        if (confirm == ESP_OK) {
            ESP_LOGI(TAG, "Running OTA image confirmed valid");
        } else {
            ESP_LOGE(TAG, "Failed to confirm OTA image: %s", esp_err_to_name(confirm));
        }
    }
}

bool ota_start_https(const char *url)
{
    if (url == nullptr || std::strncmp(url, "https://", 8) != 0) return false;
    if (std::strlen(url) >= sizeof(g_url)) return false;

    if (!claim_update(url)) return false;
    set_status(OtaState::Starting, 0, ESP_OK, "Starting");

    if (xTaskCreate(ota_task, "ota", OTA_TASK_STACK, nullptr, 4, nullptr) != pdPASS) {
        set_active(false);
        set_status(OtaState::Failed, 0, ESP_ERR_NO_MEM, "Could not start OTA task");
        return false;
    }
    return true;
}

OtaStatus ota_get_status()
{
    ensure_mutex();
    OtaStatus copy{};
    if (g_mutex != nullptr && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        copy = g_status;
        xSemaphoreGive(g_mutex);
    }
    return copy;
}

const char *ota_running_version()
{
    const esp_app_desc_t *desc = esp_app_get_description();
    return desc != nullptr ? desc->version : "unknown";
}
