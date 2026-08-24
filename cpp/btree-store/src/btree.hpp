#pragma once

#include <optional>
#include <string>
#include <vector>

#include "page.hpp"

namespace btree {

class BTree {
public:
    explicit BTree(const std::string& path);

    void put(const std::string& key, const std::string& value);
    std::optional<std::string> get(const std::string& key);
    bool remove(const std::string& key);
    std::vector<std::pair<std::string, std::string>> range_scan(
        const std::string& start, const std::string& end);

private:
    PageManager pm_;

    uint64_t create_leaf();
    void split_child(Node& parent, size_t idx, uint64_t child_id);
    void insert_non_full(uint64_t node_id, const std::string& key,
                          const std::string& value);
    uint64_t find_leaf_page(const std::string& key);
};

}
