#pragma once

#include <string>
#include <utility>
#include <vector>

#include "regex_engine.hpp"

namespace rex {

inline std::vector<std::pair<int, std::string>> grep(const std::string& pattern, const std::vector<std::string>& lines) {
    Regex regex(pattern);
    std::vector<std::pair<int, std::string>> matches;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (regex.search(lines[i])) {
            matches.push_back({static_cast<int>(i) + 1, lines[i]});
        }
    }
    return matches;
}

}
