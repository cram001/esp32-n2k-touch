#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "esp_http_server.h"
#include "wifi_service.hpp"
#define ESP_LOGI(...) ((void)0)
char g_upload_token[33]="abcdefabcdefabcdefabcdefabcdefab";
char g_upload_pin[7]="928473";
std::string response;
WifiStatus wifi_service_get_status(){WifiStatus s;s.state=WifiState::Connected;return s;}
bool ota_partition_layout_valid(){return true;}
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
void *heap_caps_malloc(size_t size,int){return std::malloc(size);} void heap_caps_free(void *p){std::free(p);}
int httpd_req_get_hdr_value_str(httpd_req_t *r,const char *key,char *out,size_t size){auto i=r->headers.find(key);if(i==r->headers.end()||i->second.size()>=size)return ESP_FAIL;std::strcpy(out,i->second.c_str());return ESP_OK;}
int httpd_resp_send_err(httpd_req_t *,int,const char *){return ESP_OK;}
int httpd_resp_set_type(httpd_req_t *,const char *){return ESP_OK;} int httpd_resp_set_hdr(httpd_req_t *,const char *,const char *){return ESP_OK;}
int httpd_resp_send(httpd_req_t *,const char *data,size_t size){response.assign(data,size);return ESP_OK;}
#include "ota_http_auth.inc"
int main(){
 httpd_req_t r;r.headers["X-OTA-Token"]=g_upload_token;
 assert(!network_request(&r,true));
 r.headers["X-OTA-PIN"]="928474";assert(!network_request(&r,true));
 r.headers["X-OTA-PIN"]=g_upload_pin;assert(network_request(&r,true));
 r.headers["X-OTA-Token"]="bad";assert(!network_request(&r,true));
 assert(page_handler(&r)==ESP_OK && response.find(g_upload_pin)==std::string::npos);
 assert(response.find("X-OTA-PIN")!=std::string::npos);
 std::puts("PASS: token plus display PIN required; PIN absent from served page");
}
