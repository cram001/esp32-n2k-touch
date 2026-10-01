#include "actisense_ascii.hpp"
#include <cstring>

namespace {
int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
bool number(const char *p, size_t n, uint32_t &v) {
    v = 0;
    for (size_t i = 0; i < n; ++i) {
        const int h = hex(p[i]); if (h < 0) return false;
        v = (v << 4) | static_cast<uint32_t>(h);
    }
    return true;
}
bool parse(char *line, ActisenseMessage &out) {
    // Exactly four mandatory fields; optional trailing fields are ignored.
    char *fields[4]{}; size_t sizes[4]{}; char *p = line;
    for (size_t i = 0; i < 4; ++i) {
        while (*p == ' ' || *p == '\t') ++p;
        fields[i] = p;
        while (*p && *p != ' ' && *p != '\t') ++p;
        sizes[i] = static_cast<size_t>(p - fields[i]);
        if (!sizes[i]) return false;
        if (*p) *p++ = '\0';
    }
    const size_t ts = sizes[0];
    if (fields[0][0] != 'A' || (ts != 7 && ts != 11)) return false;
    for (size_t i = 1; i < ts; ++i) {
        if (i == 7) { if (fields[0][i] != '.') return false; }
        else if (fields[0][i] < '0' || fields[0][i] > '9') return false;
    }
    if (sizes[1] != 5 || sizes[2] != 5 || sizes[3] % 2 || sizes[3] > 446) return false;
    uint32_t address, pgn;
    if (!number(fields[1], 5, address) || !number(fields[2], 5, pgn) || !pgn || pgn > 0x3ffff) return false;
    ActisenseMessage decoded;
    decoded.source = address >> 12;
    decoded.destination = (address >> 4) & 255;
    decoded.priority = address & 7; // Actisense reserves the high priority bit.
    decoded.pgn = pgn;
    decoded.length = sizes[3] / 2;
    for (size_t i = 0; i < decoded.length; ++i) {
        uint32_t b;
        if (!number(fields[3] + i * 2, 2, b)) return false;
        decoded.data[i] = static_cast<uint8_t>(b);
    }
    out = decoded;
    return true;
}
}
void ActisenseAsciiDecoder::reset() { used_ = 0; overflow_ = false; rejected_ = 0; }
bool ActisenseAsciiDecoder::feed(char byte, ActisenseMessage &message) {
    if (byte == '\r') return false;
    if (byte != '\n') {
        if ((static_cast<unsigned char>(byte) < 32 && byte != '\t') || static_cast<unsigned char>(byte) > 126) overflow_ = true;
        if (used_ + 1 < line_.size() && !overflow_) line_[used_++] = byte;
        else overflow_ = true;
        return false;
    }
    if (!used_ && !overflow_) return false;
    line_[used_] = '\0';
    const bool valid = !overflow_ && parse(line_.data(), message);
    used_ = 0; overflow_ = false;
    if (!valid) ++rejected_;
    return valid;
}
bool n2k_endpoint_valid(const char *ip, uint16_t port) {
    if (!ip || !port) return false;
    unsigned octets[4]{};
    for (unsigned i = 0; i < 4; ++i) {
        unsigned digits = 0;
        const bool starts_zero = *ip == '0';
        while (*ip >= '0' && *ip <= '9') {
            if (++digits > 3) return false;
            octets[i] = octets[i] * 10 + static_cast<unsigned>(*ip++ - '0');
        }
        if (!digits || octets[i] > 255 || (starts_zero && digits > 1)) return false;
        if (i < 3) { if (*ip++ != '.') return false; }
        else if (*ip) return false;
    }
    return octets[0] > 0 && octets[0] < 224 && octets[0] != 127 &&
           (octets[0] != 255) && (octets[3] != 255);
}
