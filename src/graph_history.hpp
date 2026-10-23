#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

// Owned exclusively by LVGL. Allocate whole object in PSRAM, never on task stack.
struct GraphHistory {
    static constexpr size_t MaxPoints=300;
    static constexpr int32_t Missing=INT32_MAX;
    std::array<float,MaxPoints> samples{};
    std::array<int32_t,MaxPoints> plot{};
    uint64_t context=0;
    int64_t bucket=-1;
    size_t head=0,count=0,capacity=300;
    uint8_t window=1;
    double minimum=0,maximum=1;
    bool has_range=false,initialized=false;
    static int64_t interval_us(uint8_t w){return w==2?6000000:1000000;}
    static const char *window_name(uint8_t w){return w==0?"1 min":w==2?"30 min":"5 min";}
    void reset(uint64_t identity,uint8_t w) {
        context=identity;window=w;capacity=w==0?60:300;head=count=0;bucket=-1;
        samples.fill(std::numeric_limits<float>::quiet_NaN());plot.fill(Missing);
        has_range=false;initialized=true;
    }
    void push(float value){samples[head]=value;head=(head+1)%capacity;if(count<capacity)++count;}
    bool sample(int64_t now,uint64_t identity,uint8_t w,bool valid,double value) {
        if(now<0 || w>2)return false;
        const int64_t next=now/interval_us(w);
        if(!initialized || context!=identity || window!=w || next<bucket)reset(identity,w);
        if(next==bucket)return false;
        const auto missing=std::numeric_limits<float>::quiet_NaN();
        if(bucket>=0){
            const int64_t gaps=std::min<int64_t>(capacity,next-bucket-1);
            for(int64_t i=0;i<gaps;++i)push(missing);
        }
        const float converted=static_cast<float>(value);
        push(valid&&std::isfinite(converted)?converted:missing);bucket=next;return true;
    }
    void prepare_plot(){
        plot.fill(Missing);has_range=false;
        for(size_t i=0;i<count;++i){
            const float v=samples[(head+capacity-count+i)%capacity];
            if(!std::isfinite(v))continue;
            if(!has_range){minimum=maximum=v;has_range=true;}
            else{minimum=std::min<double>(minimum,v);maximum=std::max<double>(maximum,v);}
        }
        if(!has_range)return;
        const double span=maximum-minimum;
        const double pad=span>0?span*0.05:std::max(0.1,std::abs(minimum)*0.02);
        minimum-=pad;maximum+=pad;
        for(size_t i=0;i<count;++i){
            const float v=samples[(head+capacity-count+i)%capacity];
            if(std::isfinite(v))plot[capacity-count+i]=static_cast<int32_t>(std::lround((v-minimum)/(maximum-minimum)*1000.0));
        }
    }
};
