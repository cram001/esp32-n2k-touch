#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
struct N2kWaypoint { uint16_t id = 0; std::array<char,33> name{}; };
// Decode complete PGN 129285 or 130074 lists; bounds checked before publishing.
bool n2k_decode_waypoints(uint32_t pgn,const uint8_t *data,size_t length,
                         N2kWaypoint *out,size_t capacity,size_t &count);
