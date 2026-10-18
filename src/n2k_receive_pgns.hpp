#pragma once
#include <cstdint>
inline constexpr unsigned long RECEIVE_PGNS[] = {
    127250,127258,127488,127489,127505,128259,128267,128275,
    129025,129026,129029,129283,129284,129285,130074,
    130306,130310,130311,130312,130313,130314,130316,0
};
inline bool n2k_receive_pgn(uint32_t pgn) {
    if(pgn==60928 || pgn==126996)return true;
    for(auto supported:RECEIVE_PGNS)if(supported && pgn==supported)return true;
    return false;
}
