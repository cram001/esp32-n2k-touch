#pragma once
#include "nvs.h"
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using Bytes=std::vector<unsigned char>;
std::map<std::string,Bytes> durable,pending;
bool namespace_exists=false,fail_commit=false;
int init_error=ESP_OK;unsigned erase_calls=0;
unsigned commits=0;
int nvs_flash_init(){return init_error;}
int nvs_flash_erase(){++erase_calls;durable.clear();namespace_exists=false;return ESP_OK;}
int nvs_open(const char *name,int mode,nvs_handle_t *handle){
    assert(std::strcmp(name,"app")==0);
    if(!namespace_exists && mode==NVS_READONLY)return ESP_ERR_NVS_NOT_FOUND;
    namespace_exists=true;*handle=1;return ESP_OK;
}
int nvs_get_blob(nvs_handle_t,const char *key,void *out,size_t *size){
    const auto it=durable.find(key);if(it==durable.end())return ESP_ERR_NVS_NOT_FOUND;
    if(*size<it->second.size()){*size=it->second.size();return ESP_ERR_NVS_INVALID_LENGTH;}
    *size=it->second.size();std::memcpy(out,it->second.data(),*size);return ESP_OK;
}
int nvs_set_blob(nvs_handle_t,const char *key,const void *data,size_t size){
    const auto *bytes=static_cast<const unsigned char *>(data);pending[key]=Bytes(bytes,bytes+size);return ESP_OK;
}
int nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *out){size_t size=1;return nvs_get_blob(h,key,out,&size);}
int nvs_set_u8(nvs_handle_t h,const char *key,uint8_t value){return nvs_set_blob(h,key,&value,1);}
int nvs_commit(nvs_handle_t){
    if(fail_commit)return ESP_FAIL;
    for(const auto &item:pending)durable[item.first]=item.second;
    pending.clear();++commits;return ESP_OK;
}
void nvs_close(nvs_handle_t){pending.clear();}
void reset(){durable.clear();pending.clear();namespace_exists=false;commits=0;fail_commit=false;init_error=ESP_OK;erase_calls=0;}
template<class T> void fixture(const char *key,const T &value){
    const auto *bytes=reinterpret_cast<const unsigned char *>(&value);durable[key]=Bytes(bytes,bytes+sizeof(T));namespace_exists=true;
}
