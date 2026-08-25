#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sha1 {

inline uint32_t rotl(uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

inline std::array<uint8_t, 20> hash(const std::string& input) {
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    std::vector<uint8_t> message(input.begin(), input.end());
    uint64_t originalBitLen = static_cast<uint64_t>(message.size()) * 8;

    message.push_back(0x80);
    while (message.size() % 64 != 56) {
        message.push_back(0x00);
    }
    for (int i = 7; i >= 0; --i) {
        message.push_back(static_cast<uint8_t>((originalBitLen >> (i * 8)) & 0xFF));
    }

    for (std::size_t chunkStart = 0; chunkStart < message.size(); chunkStart += 64) {
        std::array<uint32_t, 80> w{};
        for (int i = 0; i < 16; ++i) {
            std::size_t base = chunkStart + static_cast<std::size_t>(i) * 4;
            w[static_cast<std::size_t>(i)] = (static_cast<uint32_t>(message[base]) << 24) |
                                              (static_cast<uint32_t>(message[base + 1]) << 16) |
                                              (static_cast<uint32_t>(message[base + 2]) << 8) |
                                              static_cast<uint32_t>(message[base + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[static_cast<std::size_t>(i)] = rotl(w[static_cast<std::size_t>(i - 3)] ^
                                                        w[static_cast<std::size_t>(i - 8)] ^
                                                        w[static_cast<std::size_t>(i - 14)] ^
                                                        w[static_cast<std::size_t>(i - 16)],
                                                    1);
        }

        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;

        for (int i = 0; i < 80; ++i) {
            uint32_t f;
            uint32_t k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = rotl(a, 5) + f + e + k + w[static_cast<std::size_t>(i)];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::array<uint32_t, 5> parts{h0, h1, h2, h3, h4};
    std::array<uint8_t, 20> digest{};
    for (std::size_t i = 0; i < 5; ++i) {
        digest[i * 4 + 0] = static_cast<uint8_t>((parts[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = static_cast<uint8_t>((parts[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = static_cast<uint8_t>((parts[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = static_cast<uint8_t>(parts[i] & 0xFF);
    }
    return digest;
}

inline std::string toHex(const std::array<uint8_t, 20>& digest) {
    static const char* hexChars = "0123456789abcdef";
    std::string out;
    out.reserve(40);
    for (uint8_t byte : digest) {
        out.push_back(hexChars[(byte >> 4) & 0xF]);
        out.push_back(hexChars[byte & 0xF]);
    }
    return out;
}

}
