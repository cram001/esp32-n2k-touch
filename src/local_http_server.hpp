#pragma once
#include "ota.hpp"
#include "esp_http_server.h"
#include <cerrno>

// Called by the startup owner only. Partition validity belongs to the upload
// handler, so the listener and diagnostic page remain available independently.
inline LocalServerStatus start_local_http_server(httpd_handle_t &server,
                                                 const httpd_uri_t *routes,size_t count) {
    if(server)return {LocalServerStage::Listening,ESP_OK};
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();
    config.stack_size=10240;
    config.max_open_sockets=4;
    config.lru_purge_enable=true;
    config.recv_wait_timeout=5;config.send_wait_timeout=5;
    httpd_handle_t candidate=nullptr;
    // ESP-IDF collapses socket/create/bind/listen failures into ESP_FAIL.
    // Save surviving errno immediately, before caller logging can replace it. Clearing
    // it first prevents an unrelated earlier syscall from becoming diagnostic.
    errno=0;
    const esp_err_t error=httpd_start(&candidate,&config);
    const int socket_error=errno;
    if(error!=ESP_OK)return {LocalServerStage::StartFailed,error,error==ESP_FAIL?socket_error:0};
    for(size_t i=0;i<count;++i){
        const esp_err_t registered=httpd_register_uri_handler(candidate,&routes[i]);
        if(registered!=ESP_OK){httpd_stop(candidate);return {LocalServerStage::RoutesFailed,registered};}
    }
    server=candidate;
    return {LocalServerStage::Listening,ESP_OK};
}
