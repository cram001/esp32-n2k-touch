#include "tile_number_fit.hpp"
#include <cassert>
#include <cstdio>
int main(){
    // Measured Montserrat label boxes, including long signed values/time strings.
    for(int count:{1,2,4,6}){
        const int columns=count>=4?2:1,rows=count/columns;
        const int w=(columns==2?226:460)-20,h=(410-(rows-1)*8)/rows-20;
        for(int footer:{0,16})for(int unit:{0,22})for(int text_w:{30,90,150,240,440,700}){
            const auto f=tile_number_fit(w,h,22,unit,footer,text_w,52);
            assert(f.x>=0 && f.x+f.width<=w);
            assert(f.y>=28);
            assert(f.y+f.height<=h-(unit?unit+6:0)-(footer?footer+6:0));
            if(unit)assert(f.unit_y+unit<=h-(footer?footer+6:0));
            // One more scale step must violate a dimension: largest fitting size.
            assert((text_w*(f.scale+1)+255)/256>w ||
                (52*(f.scale+1)+255)/256>h-28-(unit?unit+6:0)-(footer?footer+6:0));
        }
    }
    const auto one=tile_number_fit(440,390,22,22,0,120,52);
    const auto four=tile_number_fit(206,181,22,22,0,120,52);
    const auto six=tile_number_fit(206,111,22,22,0,120,52);
    assert(one.scale>four.scale && four.scale>six.scale);
    assert(one.scale>256 && six.scale>256); // Larger than old 48/32px choices for a short value.
    assert(tile_number_fit(206,111,22,22,16,440,52).scale<256); // Long value shrinks without wrapping.
    assert(tile_number_fit(206,111,22,22,0,0,0).width==0);
    std::puts("PASS: largest fitting uniform scale in all grids; title/unit/footer bounds, signed/long values and shrinking");
}
