#pragma once

#include <cstddef>
#include <cstdint>

namespace sqlitereader {

struct VarintResult {
    uint64_t value;
    size_t bytesRead;
};

inline VarintResult decodeVarint(const uint8_t* data) {
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) {
        uint8_t byte = data[i];
        result = (result << 7) | static_cast<uint64_t>(byte & 0x7f);
        if ((byte & 0x80) == 0) {
            return VarintResult{result, static_cast<size_t>(i + 1)};
        }
    }
    result = (result << 8) | static_cast<uint64_t>(data[8]);
    return VarintResult{result, 9};
}

inline size_t serialTypeValueSize(uint64_t serialType) {
    switch (serialType) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 2;
        case 3: return 3;
        case 4: return 4;
        case 5: return 6;
        case 6: return 8;
        case 7: return 8;
        case 8: return 0;
        case 9: return 0;
        default:
            if (serialType >= 12 && serialType % 2 == 0) return static_cast<size_t>((serialType - 12) / 2);
            if (serialType >= 13 && serialType % 2 == 1) return static_cast<size_t>((serialType - 13) / 2);
            return 0;
    }
}

inline int64_t decodeBigEndianSigned(const uint8_t* data, size_t len) {
    uint64_t unsignedValue = 0;
    for (size_t i = 0; i < len; ++i) {
        unsignedValue = (unsignedValue << 8) | data[i];
    }
    if (len > 0 && len < 8 && (data[0] & 0x80) != 0) {
        uint64_t signExtendMask = ~((uint64_t(1) << (len * 8)) - 1);
        unsignedValue |= signExtendMask;
    }
    return static_cast<int64_t>(unsignedValue);
}

}
