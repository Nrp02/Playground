#pragma once

#include <cstdint>

namespace ems {

struct Record {
    std::int64_t key;
    std::int64_t payload;
};

inline bool operator<(const Record& a, const Record& b) {
    return a.key < b.key;
}

inline bool operator==(const Record& a, const Record& b) {
    return a.key == b.key && a.payload == b.payload;
}

}
