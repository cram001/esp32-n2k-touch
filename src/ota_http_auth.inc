template<size_t N>
bool constant_time_equal(const char (&a)[N],const char (&b)[N]) {
    volatile unsigned diff=0;
    for(size_t i=0;i<N;++i)diff|=static_cast<unsigned char>(a[i])^static_cast<unsigned char>(b[i]);
    return diff==0;
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
            !constant_time_equal(token,g_upload_token)) {
            httpd_resp_send_err(request,HTTPD_403_FORBIDDEN,"Reload the update page");
            return false;
        }
        char pin[7]{};
        if(httpd_req_get_hdr_value_str(request,"X-OTA-PIN",pin,sizeof(pin))!=ESP_OK ||
           !constant_time_equal(pin,g_upload_pin)) {
            httpd_resp_send_err(request,HTTPD_403_FORBIDDEN,"Enter the PIN shown on the display");
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
<label>PIN shown on display <input id="pin" type="password" inputmode="numeric" maxlength="6" autocomplete="off"></label><br>
<input id="file" type="file" accept=".bin"><br><button id="upload">Upload firmware</button><br><button id="reboot">Reboot after validation</button><pre id="status">Ready</pre>
<script>const token='%s';const status=document.getElementById('status');let busy=false;
document.getElementById('upload').onclick=async()=>{if(busy)return;const f=document.getElementById('file').files[0];if(!f){status.textContent='Choose firmware.bin first';return;}busy=true;status.textContent='Uploading and validating. Keep power connected.';try{const r=await fetch('/upload',{method:'POST',headers:{'X-OTA-PIN':document.getElementById('pin').value,'X-OTA-Token':token,'Content-Type':'application/octet-stream'},body:f});status.textContent=await r.text();}catch(e){status.textContent='Connection interrupted. Check display status before retrying.';}finally{busy=false;}};
document.getElementById('reboot').onclick=async()=>{if(busy)return;try{const r=await fetch('/reboot',{method:'POST',headers:{'X-OTA-PIN':document.getElementById('pin').value,'X-OTA-Token':token}});status.textContent=await r.text();}catch(e){status.textContent='Reconnect to the display after it restarts.';}};
</script></html>)HTML";
    constexpr size_t HTML_SIZE=4096;
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
