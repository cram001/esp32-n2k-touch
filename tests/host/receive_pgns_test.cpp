#include "n2k_receive_pgns.hpp"
#include <cassert>
#include <cstdio>
int main(){
    // All instrument PGNs from main must survive the shared CAN/W2K filter.
    const unsigned long expected[]={126992,127245,127250,127251,127257,127258,
        127488,127489,127493,127497,127505,127506,127508,127751,128000,
        128259,128267,128275,129025,129026,129029,129283,129284,129285,130074,
        130306,130310,130311,130312,130313,130314,130316,130576,60928,126996};
    for(auto pgn:expected)assert(n2k_receive_pgn(pgn));
    assert(!n2k_receive_pgn(0) && !n2k_receive_pgn(131071));
    std::puts("PASS: expanded instrument and identity PGNs accepted; unrelated PGNs filtered");
}
