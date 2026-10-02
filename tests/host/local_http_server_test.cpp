#include "local_http_server.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <string>
httpd_config_t config{};
int starts=0,stops=0,fail_route=-1,start_error=0;
std::vector<std::string> routes;
esp_err_t httpd_start(httpd_handle_t *server,const httpd_config_t *value){
    ++starts;config=*value;if(start_error)return start_error;
    *server=reinterpret_cast<void *>(1);return ESP_OK;
}
esp_err_t httpd_register_uri_handler(httpd_handle_t server,const httpd_uri_t *route){
    assert(server);if(static_cast<int>(routes.size())==fail_route)return 123;
    routes.emplace_back(route->uri);return ESP_OK;
}
esp_err_t httpd_stop(httpd_handle_t server){assert(server);++stops;return ESP_OK;}
int main(){
    const httpd_uri_t expected[]={{"/"},{"/upload"},{"/reboot"}};
    httpd_handle_t server=nullptr;
    start_error=77;auto result=start_local_http_server(server,expected,3);
    assert(result.stage==LocalServerStage::StartFailed && result.error==77 && !server && stops==0 && routes.empty());
    start_error=0;
    for(int i=0;i<3;++i){
        fail_route=i;routes.clear();const int prior=stops;
        result=start_local_http_server(server,expected,3);
        assert(result.stage==LocalServerStage::RoutesFailed && result.error==123 && !server && stops==prior+1);
    }
    fail_route=-1;routes.clear();result=start_local_http_server(server,expected,3);
    assert(result.stage==LocalServerStage::Listening && result.error==0 && server);
    assert(routes==std::vector<std::string>({"/","/upload","/reboot"}));
    assert(config.max_open_sockets==4 && config.lru_purge_enable);
    assert(config.stack_size>=10240 && config.recv_wait_timeout==5 && config.send_wait_timeout==5);
    const int prior=starts;result=start_local_http_server(server,expected,3);
    assert(result.stage==LocalServerStage::Listening && starts==prior); // idempotent startup
    std::puts("PASS: actual listener configuration, idle eviction, startup/route errors, cleanup, retry and idempotence");
}
