#include "nvs.h"
#include "nvs_fake.hpp"
#include <cstring>
namespace fake_nvs {
std::map<std::string,std::vector<unsigned char>> durable;
bool fail_commit=false;
std::map<std::string,std::vector<unsigned char>> pending;
void clear(){durable.clear();pending.clear();fail_commit=false;}
}
int nvs_open(const char *,int mode,nvs_handle_t *handle){
    if(mode==NVS_READONLY && fake_nvs::durable.empty())return ESP_ERR_NVS_NOT_FOUND;
    *handle=1;return ESP_OK;
}
int nvs_get_blob(nvs_handle_t,const char *key,void *out,size_t *size){
    const auto it=fake_nvs::durable.find(key);if(it==fake_nvs::durable.end())return ESP_ERR_NVS_NOT_FOUND;
    if(*size<it->second.size()){*size=it->second.size();return ESP_ERR_NVS_INVALID_LENGTH;}
    *size=it->second.size();std::memcpy(out,it->second.data(),*size);return ESP_OK;
}
int nvs_set_blob(nvs_handle_t,const char *key,const void *data,size_t size){
    const auto *p=static_cast<const unsigned char *>(data);fake_nvs::pending[key]={p,p+size};return ESP_OK;
}
int nvs_commit(nvs_handle_t){
    if(fake_nvs::fail_commit)return ESP_FAIL;
    for(const auto &item:fake_nvs::pending)fake_nvs::durable[item.first]=item.second;
    fake_nvs::pending.clear();return ESP_OK;
}
void nvs_close(nvs_handle_t){fake_nvs::pending.clear();}
