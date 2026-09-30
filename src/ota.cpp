#include "ota.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "esp_netif_sntp.h"

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "wifi_service.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "Dual-slot release firmware requires bootloader rollback"
#endif

namespace {
constexpr const char *TAG = "ota";
constexpr size_t OTA_URL_MAX = 256;
constexpr uint32_t OTA_TASK_STACK = 8192;

SemaphoreHandle_t g_mutex = nullptr;
OtaStatus g_status{};
char g_url[OTA_URL_MAX]{};
bool g_active = false;
bool g_network_change = false;

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
    if (g_active || g_network_change || wifi_service_scan_active() || g_status.state == OtaState::ReadyToReboot) {
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

    // Certificate validity checks require a real clock after a cold boot.
    if (std::time(nullptr) < 1704067200) {
        set_status(OtaState::Starting,0,ESP_OK,"Synchronizing clock");
        esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
        if (std::time(nullptr) < 1704067200) {
            set_status(OtaState::Failed,0,ESP_ERR_TIMEOUT,"Clock not synced; check Internet/NTP");
            set_active(false);vTaskDelete(nullptr);return;
        }
    }
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

    if (!esp_https_ota_is_complete_data_received(handle)) {
        esp_https_ota_abort(handle);
        set_status(OtaState::Failed, 0, ESP_FAIL, "Incomplete firmware download");
        set_active(false); vTaskDelete(nullptr); return;
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
    vTaskDelete(nullptr);
}
// HTTP runs in its own server task. It never touches LVGL objects.
httpd_handle_t g_server = nullptr;
char g_upload_token[33]{};

bool ap_request(httpd_req_t *request, bool token_required) {
    if (wifi_service_get_status().state != WifiState::AccessPoint) {
        httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Enable Access Point mode on the display first");
        return false;
    }
    if (token_required) {
        char token[33]{};
        if (httpd_req_get_hdr_value_str(request,"X-OTA-Token",token,sizeof(token)) != ESP_OK ||
            std::strcmp(token,g_upload_token) != 0) {
            httpd_resp_send_err(request,HTTPD_403_FORBIDDEN,"Reload the update page");
            return false;
        }
    }
    return true;
}

esp_err_t page_handler(httpd_req_t *request) {
    if (!ap_request(request,false)) return ESP_OK;
    // The token prevents cross-origin update/reboot requests. No CORS is enabled.
    const char *page = R"HTML(<!doctype html><html><meta name="viewport" content="width=device-width,initial-scale=1">
<title>N2K Display firmware update</title><style>body{font:18px system-ui;max-width:600px;margin:30px auto;padding:20px}button,input{font:inherit;margin:12px 0}pre{white-space:pre-wrap}</style>
<h1>Firmware update</h1><p>Select the application <b>firmware.bin</b>. Keep the display powered until validation finishes. A full-flash or bootloader image cannot be used here.</p>
<input id="file" type="file" accept=".bin"><br><button id="upload">Upload firmware</button><br><button id="reboot">Reboot after validation</button><pre id="status">Ready</pre>
<script>const token='%s';const status=document.getElementById('status');let busy=false;
document.getElementById('upload').onclick=async()=>{if(busy)return;const f=document.getElementById('file').files[0];if(!f){status.textContent='Choose firmware.bin first';return;}busy=true;status.textContent='Uploading and validating. Keep power connected.';try{const r=await fetch('/upload',{method:'POST',headers:{'X-OTA-Token':token,'Content-Type':'application/octet-stream'},body:f});status.textContent=await r.text();}catch(e){status.textContent='Connection interrupted. Check display status before retrying.';}finally{busy=false;}};
document.getElementById('reboot').onclick=async()=>{if(busy)return;try{const r=await fetch('/reboot',{method:'POST',headers:{'X-OTA-Token':token}});status.textContent=await r.text();}catch(e){status.textContent='Reconnect to the display after it restarts.';}};
</script></html>)HTML";
    char html[2600];
    const int len = std::snprintf(html,sizeof(html),page,g_upload_token);
    if (len < 0 || static_cast<size_t>(len) >= sizeof(html)) return httpd_resp_send_err(request,HTTPD_500_INTERNAL_SERVER_ERROR,"Page unavailable");
    httpd_resp_set_type(request,"text/html");
    httpd_resp_set_hdr(request,"Cache-Control","no-store");
    httpd_resp_set_hdr(request,"Content-Security-Policy","default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; frame-ancestors 'none'");
    return httpd_resp_send(request,html,len);
}

esp_err_t upload_handler(httpd_req_t *request) {
    if (!ap_request(request,true)) return ESP_OK;
    const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
    constexpr size_t PREFIX_SIZE = sizeof(esp_image_header_t)+sizeof(esp_image_segment_header_t)+sizeof(esp_app_desc_t);
    if (!target || !ota_partition_layout_valid() || request->content_len <= PREFIX_SIZE || request->content_len > target->size)
        return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"Invalid image size or OTA partition layout");
    char content_type[40]{};
    if (httpd_req_get_hdr_value_str(request,"Content-Type",content_type,sizeof(content_type)) != ESP_OK ||
        std::strcmp(content_type,"application/octet-stream") != 0)
        return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"Send raw firmware.bin, not a multipart archive");
    if (!claim_update("local-upload")) {
        httpd_resp_set_status(request,"409 Conflict");
        return httpd_resp_sendstr(request,"Update active or ready to reboot");
    }
    set_status(OtaState::Starting,0,ESP_OK,"Receiving firmware");
    esp_ota_handle_t handle = 0;
    bool handle_open = false;
    esp_err_t err = ESP_OK;
    char buffer[4096];
    size_t received = 0;
    // Accumulate the whole prefix even if TCP returns short reads.
    const int64_t deadline = esp_timer_get_time()+300000000LL;
    auto receive = [&](char *out,size_t bytes) {
        if (esp_timer_get_time() > deadline) return -1;
        int n = httpd_req_recv(request,out,bytes);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            vTaskDelay(pdMS_TO_TICKS(10));
            return 0;
        }
        return n > 0 ? n : -1;
    };
    while (received < PREFIX_SIZE) {
        const int n = receive(buffer+received,PREFIX_SIZE-received);
        if (n < 0) {err=ESP_ERR_TIMEOUT;break;}
        received += n;
    }
    esp_image_header_t header{};esp_app_desc_t candidate{};
    if (err == ESP_OK) {
        std::memcpy(&header,buffer,sizeof(header));
        std::memcpy(&candidate,buffer+sizeof(header)+sizeof(esp_image_segment_header_t),sizeof(candidate));
        const auto *running=esp_app_get_description();
        if (header.magic != ESP_IMAGE_HEADER_MAGIC || header.chip_id != ESP_CHIP_ID_ESP32S3 ||
            candidate.magic_word != ESP_APP_DESC_MAGIC_WORD ||
            std::strncmp(candidate.project_name,running->project_name,sizeof(candidate.project_name)) != 0 ||
            candidate.secure_version < running->secure_version) err=ESP_ERR_INVALID_VERSION;
    }
    if (err == ESP_OK) {
        err=esp_ota_begin(target,request->content_len,&handle);
        handle_open=err==ESP_OK;
    }
    if (err == ESP_OK) err=esp_ota_write(handle,buffer,received);
    while (err == ESP_OK && received < request->content_len) {
        const int n=receive(buffer,std::min(sizeof(buffer),request->content_len-received));
        if (n < 0) {err=ESP_ERR_TIMEOUT;break;}
        if (!n) continue;
        err=esp_ota_write(handle,buffer,n);
        received+=n;
        set_status(OtaState::Downloading,static_cast<int>(received*100/request->content_len),err,"Receiving firmware");
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (err == ESP_OK) {
        set_status(OtaState::Validating,100,ESP_OK,"Validating firmware");
        err=esp_ota_end(handle);handle_open=false;
    }
    if (handle_open) esp_ota_abort(handle);
    if (err == ESP_OK) err=esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        set_status(OtaState::Failed,0,err,"Upload failed; current firmware retained");
        set_active(false);
        char message[100];std::snprintf(message,sizeof(message),"Firmware rejected: %s. Current firmware retained.",esp_err_to_name(err));
        return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,message);
    }
    set_status(OtaState::ReadyToReboot,100,ESP_OK,"Update ready - reboot");
    // Keep the update claim until reboot, just like HTTPS OTA.
    return httpd_resp_sendstr(request,"Firmware validated. Click Reboot after validation to install it.");
}

void reboot_task(void *) {vTaskDelay(pdMS_TO_TICKS(1500));esp_restart();}
esp_err_t reboot_handler(httpd_req_t *request) {
    if (!ap_request(request,true)) return ESP_OK;
    if (ota_get_status().state != OtaState::ReadyToReboot)
        return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"No validated update ready");
    if (xTaskCreate(reboot_task,"ota_reboot",2048,nullptr,3,nullptr) != pdPASS)
        return httpd_resp_send_err(request,HTTPD_500_INTERNAL_SERVER_ERROR,"Use Reboot on the display");
    return httpd_resp_sendstr(request,"Restarting. Reconnect to the display after startup.");
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
    if (!wifi_service_is_connected() || !ota_partition_layout_valid()) return false;
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

bool ota_partition_layout_valid()
{
    const auto *running=esp_ota_get_running_partition();
    const auto *next=esp_ota_get_next_update_partition(nullptr);
    const auto *metadata=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_OTA,nullptr);
    const auto *slot0=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_0,nullptr);
    const auto *slot1=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_1,nullptr);
    return running && next && metadata && metadata->address==0xf000 && metadata->size==0x2000 && slot0 && slot1 &&
        slot0->address==0x20000 && slot1->address==0x620000 && slot0->size==0x600000 && slot1->size==0x600000 &&
        next->address!=running->address;
}

bool ota_start_local_server()
{
    ensure_mutex();
    if (!g_mutex || !ota_partition_layout_valid()) return false;
    if (g_server) return true;
    uint8_t random[16];esp_fill_random(random,sizeof(random));
    for(size_t i=0;i<sizeof(random);++i) std::snprintf(g_upload_token+i*2,3,"%02x",random[i]);
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();config.stack_size=10240;
    config.max_open_sockets=2;config.recv_wait_timeout=5;config.send_wait_timeout=5;
    if(httpd_start(&g_server,&config)!=ESP_OK) return false;
    httpd_uri_t page{};page.uri="/";page.method=HTTP_GET;page.handler=page_handler;
    httpd_uri_t upload{};upload.uri="/upload";upload.method=HTTP_POST;upload.handler=upload_handler;
    httpd_uri_t reboot{};reboot.uri="/reboot";reboot.method=HTTP_POST;reboot.handler=reboot_handler;
    if(httpd_register_uri_handler(g_server,&page)!=ESP_OK ||
       httpd_register_uri_handler(g_server,&upload)!=ESP_OK ||
       httpd_register_uri_handler(g_server,&reboot)!=ESP_OK) {
        httpd_stop(g_server);g_server=nullptr;return false;
    }
    return true;
}

void ota_prepare() {ensure_mutex();}

void ota_reject_running_image()
{
    const auto *running=esp_ota_get_running_partition();esp_ota_img_states_t state{};
    if(running && esp_ota_get_state_partition(running,&state)==ESP_OK && state==ESP_OTA_IMG_PENDING_VERIFY) {
        const esp_err_t err=esp_ota_mark_app_invalid_rollback_and_reboot();
        ESP_LOGE(TAG,"Unable to roll back unhealthy image: %s",esp_err_to_name(err));
    }
}

bool ota_update_in_progress() {
    ensure_mutex();
    if (!g_mutex || xSemaphoreTake(g_mutex,pdMS_TO_TICKS(100))!=pdTRUE) return true;
    const bool busy=g_active || g_network_change || g_status.state==OtaState::ReadyToReboot;
    xSemaphoreGive(g_mutex);return busy;
}
bool ota_begin_network_change() {
    ensure_mutex();
    if (!g_mutex || xSemaphoreTake(g_mutex,pdMS_TO_TICKS(100))!=pdTRUE) return false;
    const bool available=!g_active && !g_network_change && g_status.state!=OtaState::ReadyToReboot;
    if (available) g_network_change=true;
    xSemaphoreGive(g_mutex);return available;
}
void ota_end_network_change() {
    xSemaphoreTake(g_mutex,portMAX_DELAY);g_network_change=false;xSemaphoreGive(g_mutex);
}
