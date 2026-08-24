#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

namespace btree {

constexpr size_t PAGE_SIZE = 4096;
constexpr size_t KEY_MAX = 32;
constexpr size_t VALUE_MAX = 100;
constexpr size_t MAX_KEYS = 15;
constexpr uint64_t NO_PAGE = 0;

struct Node {
    bool is_leaf = true;
    uint32_t num_keys = 0;
    uint64_t next_leaf = NO_PAGE;

    char keys[MAX_KEYS][KEY_MAX] = {};
    uint8_t key_len[MAX_KEYS] = {};

    char values[MAX_KEYS][VALUE_MAX] = {};
    uint16_t value_len[MAX_KEYS] = {};

    uint64_t children[MAX_KEYS + 1] = {};
};

struct Header {
    uint32_t magic = 0xB7EE5713;
    uint64_t root_id = NO_PAGE;
    uint64_t next_page_id = 1;
};

class PageManager {
public:
    explicit PageManager(const std::string& path);
    ~PageManager();

    Node read_node(uint64_t page_id);
    void write_node(uint64_t page_id, const Node& node);

    uint64_t allocate_page();

    uint64_t root_id() const { return header_.root_id; }
    void set_root_id(uint64_t id);

private:
    void read_header();
    void write_header();
    void ensure_capacity(uint64_t page_id);

    std::fstream file_;
    Header header_;
};

std::string key_to_string(const Node& n, size_t i);
std::string value_to_string(const Node& n, size_t i);
void set_key(Node& n, size_t i, const std::string& key);
void set_value(Node& n, size_t i, const std::string& value);

}
