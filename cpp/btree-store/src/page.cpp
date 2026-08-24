#include "page.hpp"

#include <stdexcept>

namespace btree {

std::string key_to_string(const Node& n, size_t i) {
    return std::string(n.keys[i], n.key_len[i]);
}

std::string value_to_string(const Node& n, size_t i) {
    return std::string(n.values[i], n.value_len[i]);
}

void set_key(Node& n, size_t i, const std::string& key) {
    size_t len = std::min(key.size(), KEY_MAX);
    std::memcpy(n.keys[i], key.data(), len);
    if (len < KEY_MAX) std::memset(n.keys[i] + len, 0, KEY_MAX - len);
    n.key_len[i] = static_cast<uint8_t>(len);
}

void set_value(Node& n, size_t i, const std::string& value) {
    size_t len = std::min(value.size(), VALUE_MAX);
    std::memcpy(n.values[i], value.data(), len);
    if (len < VALUE_MAX) std::memset(n.values[i] + len, 0, VALUE_MAX - len);
    n.value_len[i] = static_cast<uint16_t>(len);
}

PageManager::PageManager(const std::string& path) {
    file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_.is_open()) {
        std::fstream create(path, std::ios::out | std::ios::binary);
        create.close();
        file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
        if (!file_.is_open()) {
            throw std::runtime_error("failed to open db file: " + path);
        }
        write_header();
    } else {
        file_.seekg(0, std::ios::end);
        std::streampos size = file_.tellg();
        if (size < static_cast<std::streamoff>(PAGE_SIZE)) {
            write_header();
        } else {
            read_header();
        }
    }
}

PageManager::~PageManager() {
    write_header();
    if (file_.is_open()) file_.close();
}

void PageManager::read_header() {
    file_.seekg(0, std::ios::beg);
    file_.read(reinterpret_cast<char*>(&header_), sizeof(Header));
}

void PageManager::write_header() {
    file_.seekp(0, std::ios::beg);
    file_.write(reinterpret_cast<const char*>(&header_), sizeof(Header));
    file_.flush();
}

void PageManager::ensure_capacity(uint64_t page_id) {
    std::streamoff needed = static_cast<std::streamoff>((page_id + 1) * PAGE_SIZE);
    file_.seekg(0, std::ios::end);
    std::streamoff cur = file_.tellg();
    if (cur < needed) {
        file_.seekp(needed - 1, std::ios::beg);
        char zero = 0;
        file_.write(&zero, 1);
        file_.flush();
    }
}

uint64_t PageManager::allocate_page() {
    uint64_t id = header_.next_page_id++;
    ensure_capacity(id);
    write_header();
    return id;
}

void PageManager::set_root_id(uint64_t id) {
    header_.root_id = id;
    write_header();
}

Node PageManager::read_node(uint64_t page_id) {
    ensure_capacity(page_id);
    Node node;
    file_.seekg(static_cast<std::streamoff>(page_id * PAGE_SIZE), std::ios::beg);
    file_.read(reinterpret_cast<char*>(&node), sizeof(Node));
    return node;
}

void PageManager::write_node(uint64_t page_id, const Node& node) {
    ensure_capacity(page_id);
    file_.seekp(static_cast<std::streamoff>(page_id * PAGE_SIZE), std::ios::beg);
    file_.write(reinterpret_cast<const char*>(&node), sizeof(Node));
    file_.flush();
}

}
