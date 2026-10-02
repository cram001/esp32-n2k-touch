#pragma once
#include <algorithm>
#include <cstdint>

struct TileNumberFit {
    int scale=256,x=0,y=0,width=0,height=0,unit_y=0;
};
// Coordinates are relative to the card's content origin. Use actual measured
// text and font line heights, reserving title, units and optional footer first.
inline TileNumberFit tile_number_fit(int width,int height,int title_height,
                                    int unit_height,int footer_height,int text_width,int text_height) {
    const int top=title_height+6;
    const int bottom=(unit_height?unit_height+6:0)+(footer_height?footer_height+6:0);
    const int available=std::max(1,height-top-bottom);
    TileNumberFit out{};out.unit_y=height-bottom+(unit_height?6:0);
    if(text_width<=0 || text_height<=0)return out;
    out.scale=std::max(1,std::min(width*256/text_width,available*256/text_height));
    out.width=(text_width*out.scale+255)/256;
    out.height=(text_height*out.scale+255)/256;
    out.x=(width-out.width)/2;
    out.y=top+(available-out.height)/2;
    return out;
}
