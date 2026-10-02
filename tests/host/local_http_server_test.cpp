#include "local_http_server.hpp"
#include "local_server_retry.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <string>
httpd_config_t config{};
int starts=0,stops=0,fail_route=-1,start_error=0,start_errno=0;
std::vector<std::string> routes;
esp_err_t httpd_start(httpd_handle_t *server,const httpd_config_t *value){
    ++starts;config=*value;if(start_errno)errno=start_errno;if(start_error)return start_error;
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
    // Preserve the SDK socket failure, rather than just generic ESP_FAIL.
    start_error=ESP_FAIL;start_errno=EADDRINUSE;
    result=start_local_http_server(server,expected,3);
    assert(result.error==ESP_FAIL && result.socket_error==EADDRINUSE && !server);
    // A stale errno must not be attributed to an SDK failure without errno.
    start_errno=0;errno=ENOMEM;
    result=start_local_http_server(server,expected,3);
    assert(result.socket_error==0);
    start_error=77;start_errno=EADDRINUSE;
    result=start_local_http_server(server,expected,3);
    assert(result.error==77 && result.socket_error==0);
    start_errno=0;
    start_error=0;
    for(int i=0;i<3;++i){
        fail_route=i;routes.clear();const int prior=stops;
        result=start_local_http_server(server,expected,3);
        assert(result.stage==LocalServerStage::RoutesFailed && result.error==123 && result.socket_error==0 && !server && stops==prior+1);
    }
    fail_route=-1;routes.clear();result=start_local_http_server(server,expected,3);
    assert(result.stage==LocalServerStage::Listening && result.error==0 && result.socket_error==0 && server);
    assert(routes==std::vector<std::string>({"/","/upload","/reboot"}));
    assert(config.max_open_sockets==4 && config.lru_purge_enable);
    assert(config.stack_size>=10240 && config.recv_wait_timeout==5 && config.send_wait_timeout==5);
    const int prior=starts;result=start_local_http_server(server,expected,3);
    assert(result.stage==LocalServerStage::Listening && starts==prior); // idempotent startup
    LocalServerRetry retry;
    retry.request(0);assert(retry.due(0));
    retry.complete(false,100);retry.request(500); // AP event cannot bypass backoff
    assert(!retry.due(2000099) && retry.due(2000100) && retry.attempts==1);
    retry.complete(true,2000100);retry.request(9000000);
    assert(retry.listening && !retry.active && !retry.due(9000000));
    LocalServerRetry exhausted;exhausted.request(0);
    for(unsigned i=0;i<10;++i){assert(exhausted.due(i*2000000LL));exhausted.complete(false,i*2000000LL);}
    assert(!exhausted.active && exhausted.attempts==10 && !exhausted.due(30000000));
    exhausted.request(30000000);assert(exhausted.attempts==0 && exhausted.due(30000000));
    // Actual helper succeeds on the second scheduled attempt, without duplicates.
    server=nullptr;routes.clear();start_error=ESP_FAIL;start_errno=ENOBUFS;
    LocalServerRetry integrated;integrated.request(0);
    result=start_local_http_server(server,expected,3);integrated.complete(result.stage==LocalServerStage::Listening,0);
    assert(result.socket_error==ENOBUFS && integrated.active && !server);
    assert(!integrated.due(1999999));start_error=0;start_errno=0;
    result=start_local_http_server(server,expected,3);integrated.complete(result.stage==LocalServerStage::Listening,2000000);
    assert(server && integrated.listening && !integrated.active);
    using D=StartupHealthDecision;
    assert(startup_health_decision(false,true,true,5000000)==D::Reject);
    assert(startup_health_decision(true,true,true,4999999)==D::Wait);
    assert(startup_health_decision(true,false,true,5000000)==D::Wait);
    assert(startup_health_decision(true,true,true,18000000)==D::Confirm);
    assert(startup_health_decision(true,false,true,25000000)==D::Reject);
    assert(startup_health_decision(true,true,false,25000000)==D::Reject);
    std::puts("PASS: actual listener configuration, idle eviction, startup/route errors, cleanup, retry and idempotence");
}
