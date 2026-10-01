#include "actisense_ascii.hpp"
#include <cassert>
#include <cstdio>
#include <string>

unsigned feed(ActisenseAsciiDecoder &decoder, const std::string &bytes, ActisenseMessage &out) {
    unsigned count = 0;
    for (char c : bytes) if (decoder.feed(c, out)) ++count;
    return count;
}
int main() {
    ActisenseAsciiDecoder decoder; ActisenseMessage out;
    // Published Actisense example, fragmented across network reads.
    assert(feed(decoder, "A173321.107 23", out) == 0);
    assert(feed(decoder, "FF7 1F513 012F307000", out) == 0);
    assert(feed(decoder, "2F30709F  \r\n", out) == 1);
    assert(out.pgn == 0x1f513 && out.source == 0x23 && out.destination == 255 && out.priority == 7);
    assert(out.length == 9 && out.data[0] == 1 && out.data[8] == 0x9f);
    const std::string valid = "A173321 01FF2 1F112 0102030405060708\r\n";
    assert(feed(decoder, valid + valid, out) == 2);
    assert(out.pgn == 127250 && out.length == 8 && out.priority == 2);
    assert(feed(decoder, "A173321 01FF9 1f112 ab\n", out) == 1 && out.priority == 1 && out.data[0] == 0xab);
    for (const std::string &bad : {
        "A173321 01FF2 1F112 0\n", "A173321 01FF2 1F112 GG\n",
        "A173321 01FF2 FFFFF 01\n", "A173321 01FF2 00000 01\n",
        "A173321.1 01FF2 1F112 01\n", "A173321 1FF2 1F112 01\n",
        "B173321 01FF2 1F112 01\n", "A173321 01FF2 1F112\n"}) {
        const auto before = decoder.rejected();
        assert(feed(decoder, bad, out) == 0 && decoder.rejected() == before + 1);
        assert(feed(decoder, valid, out) == 1); // Recovery at next record.
    }
    std::string payload;
    for (unsigned i = 0; i < 223; ++i) payload += "AB";
    assert(feed(decoder, "A173321 01FF2 1F112 " + payload + "\n", out) == 1 && out.length == 223);
    assert(feed(decoder, "A173321 01FF2 1F112 " + payload + "AB\n", out) == 0);
    assert(feed(decoder, std::string(2000, 'X') + "\n" + valid, out) == 1);
    std::string nul = valid; nul.insert(20, 1, '\0');
    assert(feed(decoder, nul, out) == 0);
    decoder.reset(); assert(decoder.rejected() == 0);
    // Invalid IPs cannot silently connect to another endpoint or truncate a port.
    assert(n2k_endpoint_valid("192.168.4.1", 60001));
    assert(n2k_endpoint_valid("10.0.0.4", 65535));
    for (const char *ip : {"", "192.168.4", "256.0.0.1", "192.168.4.1extra", "0.0.0.0", "255.255.255.255", "224.0.0.1", "127.0.0.1", "192.168.004.1", "192..4.1"}) assert(!n2k_endpoint_valid(ip, 60001));
    assert(!n2k_endpoint_valid("192.168.4.1", 0));
    std::puts("PASS: W2K ASCII framing, PGN/header/payload decoding, bounds, recovery and endpoint validation");
}
