#pragma once
#include "esp_err.h"
#include <cstddef>
using httpd_handle_t=void *;
struct httpd_uri_t { const char *uri=nullptr; };
struct httpd_config_t {
    size_t stack_size=4096;
    unsigned max_open_sockets=7;
    bool lru_purge_enable=false;
    unsigned recv_wait_timeout=5,send_wait_timeout=5;
};
#define HTTPD_DEFAULT_CONFIG() httpd_config_t{}
esp_err_t httpd_start(httpd_handle_t *,const httpd_config_t *);
esp_err_t httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t *);
esp_err_t httpd_stop(httpd_handle_t);

#include <map>
#include <string>
struct httpd_req_t { std::map<std::string,std::string> headers; };
constexpr int HTTPD_403_FORBIDDEN=403,HTTPD_500_INTERNAL_SERVER_ERROR=500;
int httpd_req_get_hdr_value_str(httpd_req_t *,const char *,char *,size_t);
int httpd_resp_send_err(httpd_req_t *,int,const char *);
int httpd_resp_set_type(httpd_req_t *,const char *);
int httpd_resp_set_hdr(httpd_req_t *,const char *,const char *);
int httpd_resp_send(httpd_req_t *,const char *,size_t);
