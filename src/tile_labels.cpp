#include "tile_labels.hpp"
#include "nvs.h"
#include <cstring>
#include <cstdio>

bool TileLabels::load(){
    nvs_handle_t handle;
    const auto opened=nvs_open("app",NVS_READONLY,&handle);
    if(opened==ESP_ERR_NVS_NOT_FOUND)return true;
    if(opened!=ESP_OK)return false;
    size_t size=sizeof(*this);const auto result=nvs_get_blob(handle,"tile_labels_v1",this,&size);nvs_close(handle);
    if(result==ESP_ERR_NVS_NOT_FOUND)return true;
    if(result!=ESP_OK || size!=sizeof(*this) || schema!=1){schema=1;values={};return false;}
    for(auto &label:values)label.back()='\0';
    return true;
}
bool TileLabels::save(size_t field,const char *text){
    if(field>=Count || !text || std::strlen(text)>=Length)return false;
    const auto old=values[field];std::snprintf(values[field].data(),Length,"%s",text);
    nvs_handle_t handle;auto result=nvs_open("app",NVS_READWRITE,&handle);
    if(result==ESP_OK){result=nvs_set_blob(handle,"tile_labels_v1",this,sizeof(*this));if(result==ESP_OK)result=nvs_commit(handle);nvs_close(handle);}
    if(result!=ESP_OK){values[field]=old;return false;}
    return true;
}
