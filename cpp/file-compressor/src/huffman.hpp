#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

namespace huff {

struct Node {
    std::uint8_t symbol = 0;
    std::uint64_t freq = 0;
    bool isLeaf = false;
    std::size_t id = 0;
    std::unique_ptr<Node> left;
    std::unique_ptr<Node> right;
};

using NodePtr = std::unique_ptr<Node>;

struct NodeCompare {
    bool operator()(const NodePtr& a, const NodePtr& b) const {
        if (a->freq != b->freq) return a->freq > b->freq;
        return a->id > b->id;
    }
};

inline std::array<std::uint64_t, 256> buildFrequencyTable(const std::vector<std::uint8_t>& data) {
    std::array<std::uint64_t, 256> freq{};
    for (std::uint8_t b : data) {
        ++freq[b];
    }
    return freq;
}

inline NodePtr buildHuffmanTree(const std::array<std::uint64_t, 256>& freq) {
    std::priority_queue<NodePtr, std::vector<NodePtr>, NodeCompare> pq;
    std::size_t nextId = 0;

    for (int sym = 0; sym < 256; ++sym) {
        std::uint64_t f = freq[static_cast<std::size_t>(sym)];
        if (f == 0) continue;
        auto node = std::make_unique<Node>();
        node->symbol = static_cast<std::uint8_t>(sym);
        node->freq = f;
        node->isLeaf = true;
        node->id = nextId++;
        pq.push(std::move(node));
    }

    if (pq.empty()) {
        return nullptr;
    }

    while (pq.size() > 1) {
        NodePtr a = std::move(const_cast<NodePtr&>(pq.top()));
        pq.pop();
        NodePtr b = std::move(const_cast<NodePtr&>(pq.top()));
        pq.pop();

        auto parent = std::make_unique<Node>();
        parent->freq = a->freq + b->freq;
        parent->isLeaf = false;
        parent->id = nextId++;
        parent->left = std::move(a);
        parent->right = std::move(b);
        pq.push(std::move(parent));
    }

    NodePtr root = std::move(const_cast<NodePtr&>(pq.top()));
    pq.pop();
    return root;
}

inline void generateCodes(const Node* node, std::string& prefix, std::array<std::string, 256>& codes) {
    if (node == nullptr) return;
    if (node->isLeaf) {
        codes[node->symbol] = prefix.empty() ? std::string("0") : prefix;
        return;
    }
    prefix.push_back('0');
    generateCodes(node->left.get(), prefix, codes);
    prefix.pop_back();
    prefix.push_back('1');
    generateCodes(node->right.get(), prefix, codes);
    prefix.pop_back();
}

class BitWriter {
public:
    void writeBit(int bit) {
        current_ = static_cast<std::uint8_t>((current_ << 1) | (bit & 1));
        ++count_;
        if (count_ == 8) {
            bytes_.push_back(current_);
            current_ = 0;
            count_ = 0;
        }
    }

    std::vector<std::uint8_t> finish() {
        if (count_ > 0) {
            current_ = static_cast<std::uint8_t>(current_ << (8 - count_));
            bytes_.push_back(current_);
            count_ = 0;
        }
        return bytes_;
    }

private:
    std::vector<std::uint8_t> bytes_;
    std::uint8_t current_ = 0;
    int count_ = 0;
};

class BitReader {
public:
    BitReader(const std::vector<std::uint8_t>& bytes, std::size_t startByte)
        : bytes_(bytes), byteIndex_(startByte) {}

    int readBit() {
        if (byteIndex_ >= bytes_.size()) {
            throw std::runtime_error("bitstream underflow");
        }
        std::uint8_t byte = bytes_[byteIndex_];
        int bit = (byte >> (7 - bitIndex_)) & 1;
        ++bitIndex_;
        if (bitIndex_ == 8) {
            bitIndex_ = 0;
            ++byteIndex_;
        }
        return bit;
    }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t byteIndex_;
    int bitIndex_ = 0;
};

inline void appendU64(std::vector<std::uint8_t>& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

inline void appendU16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

inline std::uint64_t readU64(const std::vector<std::uint8_t>& data, std::size_t& offset) {
    if (offset + 8 > data.size()) {
        throw std::runtime_error("truncated header");
    }
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= (static_cast<std::uint64_t>(data[offset + static_cast<std::size_t>(i)]) << (8 * i));
    }
    offset += 8;
    return v;
}

inline std::uint16_t readU16(const std::vector<std::uint8_t>& data, std::size_t& offset) {
    if (offset + 2 > data.size()) {
        throw std::runtime_error("truncated header");
    }
    std::uint16_t v = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[offset]) |
        (static_cast<std::uint16_t>(data[offset + 1]) << 8));
    offset += 2;
    return v;
}

inline std::uint8_t decodeOne(const Node* root, BitReader& reader) {
    if (root->isLeaf) {
        reader.readBit();
        return root->symbol;
    }
    const Node* cur = root;
    while (!cur->isLeaf) {
        int bit = reader.readBit();
        cur = (bit == 0) ? cur->left.get() : cur->right.get();
    }
    return cur->symbol;
}

inline std::vector<std::uint8_t> compress(const std::vector<std::uint8_t>& input) {
    std::array<std::uint64_t, 256> freq = buildFrequencyTable(input);

    std::vector<std::uint8_t> output;
    std::uint64_t originalSize = input.size();
    appendU64(output, originalSize);

    std::vector<std::pair<std::uint8_t, std::uint64_t>> present;
    for (int i = 0; i < 256; ++i) {
        if (freq[static_cast<std::size_t>(i)] > 0) {
            present.emplace_back(static_cast<std::uint8_t>(i), freq[static_cast<std::size_t>(i)]);
        }
    }
    appendU16(output, static_cast<std::uint16_t>(present.size()));
    for (const auto& entry : present) {
        output.push_back(entry.first);
        appendU64(output, entry.second);
    }

    if (originalSize == 0) {
        return output;
    }

    NodePtr root = buildHuffmanTree(freq);
    std::array<std::string, 256> codes{};
    std::string prefix;
    generateCodes(root.get(), prefix, codes);

    BitWriter writer;
    for (std::uint8_t b : input) {
        for (char c : codes[b]) {
            writer.writeBit(c == '1' ? 1 : 0);
        }
    }
    std::vector<std::uint8_t> bits = writer.finish();
    output.insert(output.end(), bits.begin(), bits.end());
    return output;
}

inline std::vector<std::uint8_t> decompress(const std::vector<std::uint8_t>& compressed) {
    std::size_t offset = 0;
    std::uint64_t originalSize = readU64(compressed, offset);
    std::uint16_t numDistinct = readU16(compressed, offset);

    std::array<std::uint64_t, 256> freq{};
    for (int i = 0; i < numDistinct; ++i) {
        if (offset >= compressed.size()) {
            throw std::runtime_error("truncated header");
        }
        std::uint8_t sym = compressed[offset++];
        std::uint64_t count = readU64(compressed, offset);
        freq[sym] = count;
    }

    std::vector<std::uint8_t> result;
    if (originalSize == 0) {
        return result;
    }
    result.reserve(static_cast<std::size_t>(originalSize));

    NodePtr root = buildHuffmanTree(freq);
    BitReader reader(compressed, offset);
    for (std::uint64_t i = 0; i < originalSize; ++i) {
        result.push_back(decodeOne(root.get(), reader));
    }
    return result;
}

inline double compressionRatio(std::size_t originalBytes, std::size_t compressedBytes) {
    if (compressedBytes == 0) return 0.0;
    return static_cast<double>(originalBytes) / static_cast<double>(compressedBytes);
}

}
