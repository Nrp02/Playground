#include "btree.hpp"

#include <algorithm>

namespace btree {

BTree::BTree(const std::string& path) : pm_(path) {
    if (pm_.root_id() == NO_PAGE) {
        uint64_t root = create_leaf();
        pm_.set_root_id(root);
    }
}

uint64_t BTree::create_leaf() {
    Node n;
    n.is_leaf = true;
    n.num_keys = 0;
    n.next_leaf = NO_PAGE;
    uint64_t id = pm_.allocate_page();
    pm_.write_node(id, n);
    return id;
}

static size_t child_index(const Node& node, const std::string& key) {
    size_t i = 0;
    while (i < node.num_keys && key >= key_to_string(node, i)) i++;
    return i;
}

void BTree::split_child(Node& parent, size_t idx, uint64_t child_id) {
    Node child = pm_.read_node(child_id);
    size_t mid = MAX_KEYS / 2;

    Node new_node;
    new_node.is_leaf = child.is_leaf;
    uint64_t new_id = pm_.allocate_page();

    std::string promote_key;

    if (child.is_leaf) {
        size_t count = child.num_keys - mid;
        for (size_t j = 0; j < count; j++) {
            set_key(new_node, j, key_to_string(child, mid + j));
            set_value(new_node, j, value_to_string(child, mid + j));
        }
        new_node.num_keys = static_cast<uint32_t>(count);
        new_node.next_leaf = child.next_leaf;
        child.next_leaf = new_id;
        child.num_keys = static_cast<uint32_t>(mid);
        promote_key = key_to_string(new_node, 0);
    } else {
        promote_key = key_to_string(child, mid);
        size_t count = child.num_keys - mid - 1;
        for (size_t j = 0; j < count; j++) {
            set_key(new_node, j, key_to_string(child, mid + 1 + j));
        }
        for (size_t j = 0; j <= count; j++) {
            new_node.children[j] = child.children[mid + 1 + j];
        }
        new_node.num_keys = static_cast<uint32_t>(count);
        child.num_keys = static_cast<uint32_t>(mid);
    }

    pm_.write_node(child_id, child);
    pm_.write_node(new_id, new_node);

    for (size_t j = parent.num_keys; j > idx; j--) {
        set_key(parent, j, key_to_string(parent, j - 1));
    }
    for (size_t j = parent.num_keys + 1; j > idx + 1; j--) {
        parent.children[j] = parent.children[j - 1];
    }
    set_key(parent, idx, promote_key);
    parent.children[idx + 1] = new_id;
    parent.num_keys++;
}

void BTree::insert_non_full(uint64_t node_id, const std::string& key,
                             const std::string& value) {
    Node node = pm_.read_node(node_id);

    if (node.is_leaf) {
        size_t i = 0;
        while (i < node.num_keys && key_to_string(node, i) < key) i++;

        if (i < node.num_keys && key_to_string(node, i) == key) {
            set_value(node, i, value);
            pm_.write_node(node_id, node);
            return;
        }

        for (size_t j = node.num_keys; j > i; j--) {
            set_key(node, j, key_to_string(node, j - 1));
            set_value(node, j, value_to_string(node, j - 1));
        }
        set_key(node, i, key);
        set_value(node, i, value);
        node.num_keys++;
        pm_.write_node(node_id, node);
        return;
    }

    size_t i = child_index(node, key);
    uint64_t child_id = node.children[i];
    Node child = pm_.read_node(child_id);

    if (child.num_keys == MAX_KEYS) {
        split_child(node, i, child_id);
        pm_.write_node(node_id, node);
        if (key >= key_to_string(node, i)) i++;
        child_id = node.children[i];
    }

    insert_non_full(child_id, key, value);
}

void BTree::put(const std::string& key, const std::string& value) {
    uint64_t root_id = pm_.root_id();
    Node root = pm_.read_node(root_id);

    if (root.num_keys == MAX_KEYS) {
        Node new_root;
        new_root.is_leaf = false;
        new_root.num_keys = 0;
        new_root.children[0] = root_id;
        uint64_t new_root_id = pm_.allocate_page();
        pm_.write_node(new_root_id, new_root);

        Node parent = pm_.read_node(new_root_id);
        split_child(parent, 0, root_id);
        pm_.write_node(new_root_id, parent);
        pm_.set_root_id(new_root_id);
        root_id = new_root_id;
    }

    insert_non_full(root_id, key, value);
}

uint64_t BTree::find_leaf_page(const std::string& key) {
    uint64_t id = pm_.root_id();
    Node node = pm_.read_node(id);
    while (!node.is_leaf) {
        size_t i = child_index(node, key);
        id = node.children[i];
        node = pm_.read_node(id);
    }
    return id;
}

std::optional<std::string> BTree::get(const std::string& key) {
    uint64_t leaf_id = find_leaf_page(key);
    Node leaf = pm_.read_node(leaf_id);
    for (size_t i = 0; i < leaf.num_keys; i++) {
        if (key_to_string(leaf, i) == key) {
            return value_to_string(leaf, i);
        }
    }
    return std::nullopt;
}

bool BTree::remove(const std::string& key) {
    uint64_t leaf_id = find_leaf_page(key);
    Node leaf = pm_.read_node(leaf_id);
    for (size_t i = 0; i < leaf.num_keys; i++) {
        if (key_to_string(leaf, i) == key) {
            for (size_t j = i; j + 1 < leaf.num_keys; j++) {
                set_key(leaf, j, key_to_string(leaf, j + 1));
                set_value(leaf, j, value_to_string(leaf, j + 1));
            }
            leaf.num_keys--;
            pm_.write_node(leaf_id, leaf);
            return true;
        }
    }
    return false;
}

std::vector<std::pair<std::string, std::string>> BTree::range_scan(
    const std::string& start, const std::string& end) {
    std::vector<std::pair<std::string, std::string>> result;
    uint64_t leaf_id = find_leaf_page(start);

    while (leaf_id != NO_PAGE) {
        Node leaf = pm_.read_node(leaf_id);
        for (size_t i = 0; i < leaf.num_keys; i++) {
            std::string k = key_to_string(leaf, i);
            if (k > end) return result;
            if (k >= start) {
                result.emplace_back(k, value_to_string(leaf, i));
            }
        }
        leaf_id = leaf.next_leaf;
    }
    return result;
}

}
