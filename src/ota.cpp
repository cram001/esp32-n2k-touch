#include "ota.hpp"
#include "app_settings.hpp"

#include <algorithm>
#include <atomic>
#include "local_http_server.hpp"
#include "local_server_retry.hpp"
#include <cstdio>
#include <cstring>
#include <ctime>
#include "esp_netif_sntp.h"

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "wifi_service.hpp"
#include "smartshunt_ble.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "Dual-slot release firmware requires bootloader rollback"
#endif
#if !CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP
#error "HTTP memory profile missing: regenerate sdkconfig.waveshare-touch-4 from sdkconfig.defaults"
#endif
#if CONFIG_LWIP_IPV6
#error "IPv6 is intentionally disabled for the local AP/OTA profile"
#endif
static_assert(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL==256 && CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM==6 &&
              CONFIG_ESP_WIFI_RX_BA_WIN==6 &&
              CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM==16 && CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM==6 &&
              CONFIG_ESP_WIFI_CACHE_TX_BUFFER_NUM==8 && CONFIG_LWIP_MAX_SOCKETS==10 &&
              CONFIG_LWIP_TCP_SND_BUF_DEFAULT==2880 && CONFIG_LWIP_TCP_WND_DEFAULT==2880 &&
              CONFIG_LWIP_TCP_OOSEQ_MAX_PBUFS==2,
              "Stale low-internal-RAM profile: regenerate sdkconfig.waveshare-touch-4 before building");

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
    if(!settings_restore_normal_mode_after_update())
        ESP_LOGW(TAG,"Update installed but normal operating mode could not be restored");
    set_status(OtaState::ReadyToReboot, 100, ESP_OK, "Update ready - reboot");
    vTaskDelete(nullptr);
}
// HTTP runs in its own server task. It never touches LVGL objects.
httpd_handle_t g_server = nullptr;
LocalServerStatus g_server_status{}; // protected by g_mutex
TaskHandle_t g_server_worker=nullptr;
std::atomic<int64_t> g_server_deadline_us{0};
constexpr int64_t LOCAL_SERVER_WINDOW_US = 120000000LL;
struct ServerHeap {
    size_t internal_free=0,internal_largest=0,internal_minimum=0;
    size_t psram_free=0,psram_largest=0;
    size_t internal_8bit_free=0,internal_8bit_largest=0;
};
ServerHeap g_server_heap{}; // last startup snapshot, protected by g_mutex
char g_upload_token[33]{};

ServerHeap server_heap() {
    return {heap_caps_get_free_size(MALLOC_CAP_INTERNAL),heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
            heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
            heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)};
}
void log_server_heap(const char *phase,const ServerHeap &heap) {
    ESP_LOGI(TAG,"HTTP heap %s: internal free=%u largest=%u minimum=%u; PSRAM free=%u largest=%u",phase,
             static_cast<unsigned>(heap.internal_free),static_cast<unsigned>(heap.internal_largest),
             static_cast<unsigned>(heap.internal_minimum),static_cast<unsigned>(heap.psram_free),
             static_cast<unsigned>(heap.psram_largest));
    ESP_LOGI(TAG,"HTTP heap %s: internal 8-bit free=%u largest=%u",phase,
             static_cast<unsigned>(heap.internal_8bit_free),static_cast<unsigned>(heap.internal_8bit_largest));
}
void publish_server_status(const LocalServerStatus &status,const ServerHeap &heap) {
    xSemaphoreTake(g_mutex,portMAX_DELAY);g_server_status=status;g_server_heap=heap;xSemaphoreGive(g_mutex);
}

bool network_request(httpd_req_t *request, bool token_required) {
    const auto wifi=wifi_service_get_status();
    if (wifi.state != WifiState::AccessPoint && wifi.state != WifiState::Connected) {
        httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Firmware Update mode requires active Wi-Fi");
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
    ESP_LOGI(TAG,"Local HTTP GET /");
    if (!network_request(request,false)) return ESP_OK;
    // The token prevents cross-origin update/reboot requests. No CORS is enabled.
    const char *page = R"HTML(<!doctype html><html><meta name="viewport" content="width=device-width,initial-scale=1">
<title>N2K Display firmware update</title><style>body{font:18px system-ui;max-width:600px;margin:30px auto;padding:20px}button,input{font:inherit;margin:12px 0}pre{white-space:pre-wrap}</style>
<h1>Firmware update</h1><p>%s</p><p>Select the application <b>firmware.bin</b>. Keep the display powered until validation finishes. A full-flash or bootloader image cannot be used here.</p>
<input id="file" type="file" accept=".bin"><br><button id="upload">Upload firmware</button><br><button id="reboot">Reboot after validation</button><pre id="status">Ready</pre>
<script>const token='%s';const status=document.getElementById('status');let busy=false;
document.getElementById('upload').onclick=async()=>{if(busy)return;const f=document.getElementById('file').files[0];if(!f){status.textContent='Choose firmware.bin first';return;}busy=true;status.textContent='Uploading and validating. Keep power connected.';try{const r=await fetch('/upload',{method:'POST',headers:{'X-OTA-Token':token,'Content-Type':'application/octet-stream'},body:f});status.textContent=await r.text();}catch(e){status.textContent='Connection interrupted. Check display status before retrying.';}finally{busy=false;}};
document.getElementById('reboot').onclick=async()=>{if(busy)return;try{const r=await fetch('/reboot',{method:'POST',headers:{'X-OTA-Token':token}});status.textContent=await r.text();}catch(e){status.textContent='Reconnect to the display after it restarts.';}};
</script></html>)HTML";
    constexpr size_t HTML_SIZE=2600;
    char *html=static_cast<char *>(heap_caps_malloc(HTML_SIZE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!html) return httpd_resp_send_err(request,HTTPD_500_INTERNAL_SERVER_ERROR,"Page memory unavailable");
    const bool layout_ok=ota_partition_layout_valid();
    const int len = std::snprintf(html,HTML_SIZE,page,layout_ok?"Ready for firmware upload.":"Upload unavailable: incompatible flash layout. Install compatible firmware using USB.",g_upload_token);
    if (len < 0 || static_cast<size_t>(len) >= HTML_SIZE) {
        heap_caps_free(html);
        return httpd_resp_send_err(request,HTTPD_500_INTERNAL_SERVER_ERROR,"Page unavailable");
    }
    httpd_resp_set_type(request,"text/html");
    httpd_resp_set_hdr(request,"Cache-Control","no-store");
    httpd_resp_set_hdr(request,"Content-Security-Policy","default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; frame-ancestors 'none'");
    const esp_err_t sent=httpd_resp_send(request,html,len);
    heap_caps_free(html);
    return sent;
}

esp_err_t upload_handler(httpd_req_t *request) {
    if (!network_request(request,true)) return ESP_OK;
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
    constexpr size_t UPLOAD_BUFFER_SIZE=4096;
    char *buffer=static_cast<char *>(heap_caps_malloc(UPLOAD_BUFFER_SIZE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!buffer) {
        set_status(OtaState::Failed,0,ESP_ERR_NO_MEM,"Upload buffer allocation failed");
        set_active(false);
        return httpd_resp_send_err(request,HTTPD_500_INTERNAL_SERVER_ERROR,"Upload memory unavailable");
    }
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
        const int n=receive(buffer,std::min(UPLOAD_BUFFER_SIZE,request->content_len-received));
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
    heap_caps_free(buffer);
    if (err != ESP_OK) {
        set_status(OtaState::Failed,0,err,"Upload failed; current firmware retained");
        set_active(false);
        char message[100];std::snprintf(message,sizeof(message),"Firmware rejected: %s. Current firmware retained.",esp_err_to_name(err));
        return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,message);
    }
    if(!settings_restore_normal_mode_after_update())
        ESP_LOGW(TAG,"Update validated but normal operating mode could not be restored");
    set_status(OtaState::ReadyToReboot,100,ESP_OK,"Update ready - reboot");
    // Keep the update claim until reboot, just like HTTPS OTA.
    return httpd_resp_sendstr(request,"Firmware validated. Click Reboot after validation to install it.");
}

void reboot_task(void *) {vTaskDelay(pdMS_TO_TICKS(1500));esp_restart();}
esp_err_t reboot_handler(httpd_req_t *request) {
    if (!network_request(request,true)) return ESP_OK;
    if (ota_get_status().state != OtaState::ReadyToReboot)
        return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"No validated update ready");
    if (xTaskCreate(reboot_task,"ota_reboot",2048,nullptr,3,nullptr) != pdPASS)
        return httpd_resp_send_err(request,HTTPD_500_INTERNAL_SERVER_ERROR,"Use Reboot on the display");
    return httpd_resp_sendstr(request,"Restarting. Reconnect to the display after startup.");
}

void local_server_worker(void *) {
    const bool restore_ble=smartshunt_ble_is_running();
    if(restore_ble && !smartshunt_ble_pause()) {
        publish_server_status({LocalServerStage::StartFailed,ESP_FAIL,0,1,false},server_heap());
        ESP_LOGE(TAG,"Unable to pause SmartShunt BLE for web update");
        if(!smartshunt_ble_resume()) ESP_LOGE(TAG,"SmartShunt BLE recovery also failed");
        g_server_worker=nullptr;
        vTaskDelete(nullptr);
        return;
    }
    // Give controller/Bluedroid deinit a short interval to return heap before
    // creating the HTTP task stack. Large request buffers live in PSRAM.
    vTaskDelay(pdMS_TO_TICKS(100));
    uint8_t random[16];esp_fill_random(random,sizeof(random));
    for(size_t i=0;i<sizeof(random);++i)std::snprintf(g_upload_token+i*2,3,"%02x",random[i]);
    httpd_uri_t page{};page.uri="/";page.method=HTTP_GET;page.handler=page_handler;
    httpd_uri_t upload{};upload.uri="/upload";upload.method=HTTP_POST;upload.handler=upload_handler;
    httpd_uri_t reboot{};reboot.uri="/reboot";reboot.method=HTTP_POST;reboot.handler=reboot_handler;
    const httpd_uri_t routes[]={page,upload,reboot};

    const auto before=server_heap();
    publish_server_status({LocalServerStage::Starting,0,0,1,false},before);
    log_server_heap("before on-demand httpd_start",before);
    auto result=start_local_http_server(g_server,routes,3);
    const auto after=server_heap();
    result.attempts=1;
    publish_server_status(result,after);
    ota_log_local_server_status();
    if(result.stage!=LocalServerStage::Listening) {
        log_server_heap("after failed on-demand startup",after);
        if(restore_ble) smartshunt_ble_resume();
        g_server_deadline_us=0;
        g_server_worker=nullptr;
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG,"Firmware web server enabled for 120 seconds; SmartShunt BLE paused");
    for(;;) {
        const int64_t now=esp_timer_get_time();
        bool busy=false;
        ensure_mutex();
        if(g_mutex && xSemaphoreTake(g_mutex,pdMS_TO_TICKS(20))==pdTRUE) {
            busy=g_active || g_status.state==OtaState::ReadyToReboot;
            xSemaphoreGive(g_mutex);
        }
        // Never expire while receiving/validating an image or while waiting for
        // the explicit reboot after a successful local upload.
        if(busy) {
            g_server_deadline_us=now+LOCAL_SERVER_WINDOW_US;
        } else {
            const int64_t deadline=g_server_deadline_us.load();
            if(deadline<=now) break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if(g_server) {
        httpd_stop(g_server);
        g_server=nullptr;
    }
    const auto stopped=server_heap();
    publish_server_status({LocalServerStage::NotStarted,ESP_OK,0,0,false},stopped);
    log_server_heap("after timed web server stop",stopped);
    if(restore_ble && !smartshunt_ble_resume()) ESP_LOGE(TAG,"SmartShunt BLE restore failed after web update window");
    g_server_deadline_us=0;
    g_server_worker=nullptr;
    ESP_LOGI(TAG,"Firmware web server disabled; normal BLE operation restored");
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

bool ota_enable_local_server(uint32_t seconds)
{
    ensure_mutex();
    const auto wifi=wifi_service_get_status();
    if(!g_mutex || seconds==0 || (wifi.state!=WifiState::AccessPoint && wifi.state!=WifiState::Connected)) return false;
    const int64_t duration=static_cast<int64_t>(seconds)*1000000LL;
    g_server_deadline_us=esp_timer_get_time()+duration;
    if(g_server_worker) return true; // Extend the active window.
    publish_server_status({LocalServerStage::Starting,ESP_OK,0,0,false},server_heap());
    if(xTaskCreate(local_server_worker,"http_window",2048,nullptr,1,&g_server_worker)!=pdPASS) {
        g_server_deadline_us=0;
        g_server_worker=nullptr;
        publish_server_status({LocalServerStage::StartFailed,ESP_ERR_NO_MEM,0,0,false},server_heap());
        ota_log_local_server_status();
        return false;
    }
    return true;
}

bool ota_disable_local_server()
{
    ensure_mutex();
    if(!g_server_worker) return true;
    if(!g_mutex || xSemaphoreTake(g_mutex,pdMS_TO_TICKS(50))!=pdTRUE) return false;
    const bool busy=g_active || g_status.state==OtaState::ReadyToReboot;
    xSemaphoreGive(g_mutex);
    if(busy) return false;
    g_server_deadline_us=esp_timer_get_time();
    return true;
}

bool ota_local_server_window_active()
{
    return g_server_worker!=nullptr;
}

uint32_t ota_local_server_seconds_remaining()
{
    if(!g_server_worker) return 0;
    const int64_t remaining=g_server_deadline_us.load()-esp_timer_get_time();
    return remaining>0?static_cast<uint32_t>((remaining+999999)/1000000):0;
}
LocalServerStatus ota_local_server_status(){
    if(!g_mutex)return {LocalServerStage::StartFailed,ESP_ERR_NO_MEM};
    xSemaphoreTake(g_mutex,portMAX_DELAY);const auto status=g_server_status;xSemaphoreGive(g_mutex);return status;
}
const char *ota_local_server_stage_name(LocalServerStage stage){
    switch(stage){
    case LocalServerStage::NotStarted:return "Not started";
    case LocalServerStage::Starting:return "Starting";
    case LocalServerStage::Listening:return "Listening on port 80";
    case LocalServerStage::StartFailed:return "Listener startup failed";
    case LocalServerStage::RoutesFailed:return "Page registration failed";
    }
    return "Unknown";
}
void ota_log_local_server_status(){
    const auto status=ota_local_server_status();
    ESP_LOGI(TAG,"HTTP server: %s; error %s (0x%x); socket errno %d (%s); attempt %u; window %us; OTA layout: %s",ota_local_server_stage_name(status.stage),
             esp_err_to_name(status.error),status.error,status.socket_error,status.socket_error?std::strerror(status.socket_error):"none captured",
             status.attempts,ota_local_server_seconds_remaining(),
             ota_partition_layout_valid()?"valid":"incompatible (uploads disabled)");
    if(g_mutex){xSemaphoreTake(g_mutex,portMAX_DELAY);const auto heap=g_server_heap;xSemaphoreGive(g_mutex);log_server_heap("last startup snapshot",heap);}
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
    const bool busy=g_active || g_network_change || g_status.state==OtaState::ReadyToReboot || g_server_worker!=nullptr;
    xSemaphoreGive(g_mutex);return busy;
}
bool ota_begin_network_change() {
    ensure_mutex();
    if (!g_mutex || xSemaphoreTake(g_mutex,pdMS_TO_TICKS(100))!=pdTRUE) return false;
    const bool available=!g_active && !g_network_change && g_status.state!=OtaState::ReadyToReboot && g_server_worker==nullptr;
    if (available) g_network_change=true;
    xSemaphoreGive(g_mutex);return available;
}
void ota_end_network_change() {
    xSemaphoreTake(g_mutex,portMAX_DELAY);g_network_change=false;xSemaphoreGive(g_mutex);
}
