#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

struct ActisenseMessage {
    uint32_t pgn = 0;
    uint8_t source = 0, destination = 255, priority = 0;
    uint16_t length = 0;
    std::array<uint8_t, 223> data{};
};

// W2K N2K ASCII: bounded streaming decoder, including fragmented TCP reads.
class ActisenseAsciiDecoder {
public:
    bool feed(char byte, ActisenseMessage &message);
    void reset();
    uint32_t rejected() const { return rejected_; }
private:
    std::array<char, 512> line_{};
    size_t used_ = 0;
    bool overflow_ = false;
    uint32_t rejected_ = 0;
};
bool n2k_endpoint_valid(const char *ip, uint16_t port);
