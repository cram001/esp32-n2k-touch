#pragma once
#include "esp_err.h"
#include <cstddef>
using httpd_handle_t=void *;
struct httpd_uri_t { const char *uri=nullptr; };
#define ESP_HTTPD_DEF_CTRL_PORT 32768
struct httpd_config_t {
    size_t stack_size=4096;
    unsigned short server_port=80;
    unsigned short ctrl_port=ESP_HTTPD_DEF_CTRL_PORT;
    unsigned max_open_sockets=7;
    bool lru_purge_enable=false;
    unsigned recv_wait_timeout=5,send_wait_timeout=5;
};
#define HTTPD_DEFAULT_CONFIG() httpd_config_t{}
esp_err_t httpd_start(httpd_handle_t *,const httpd_config_t *);
esp_err_t httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t *);
esp_err_t httpd_stop(httpd_handle_t);
