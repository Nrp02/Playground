#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <stdexcept>
#include <vector>

#include "bit_io.hpp"

namespace jic {

constexpr int kAlphabetSize = 256;
constexpr int kMaxCodeLength = 16;

using FrequencyTable = std::array<std::uint64_t, kAlphabetSize>;
using LengthTable = std::array<std::uint8_t, kAlphabetSize>;

struct HuffmanTable {
    LengthTable lengths{};
    std::array<std::uint32_t, kAlphabetSize> codes{};
    std::array<int, kMaxCodeLength + 1> countPerLength{};
    std::array<std::uint32_t, kMaxCodeLength + 1> firstCode{};
    std::array<int, kMaxCodeLength + 1> firstIndex{};
    std::vector<std::uint8_t> sortedSymbols;

    bool empty() const { return sortedSymbols.empty(); }
    bool hasSymbol(std::uint8_t symbol) const { return lengths[symbol] != 0; }
};

inline LengthTable computeCodeLengths(const FrequencyTable& frequencies) {
    FrequencyTable work = frequencies;
    LengthTable lengths{};
    for (;;) {
        lengths.fill(0);
        std::vector<int> used;
        for (int i = 0; i < kAlphabetSize; ++i) {
            if (work[i] > 0) used.push_back(i);
        }
        if (used.empty()) return lengths;
        if (used.size() == 1) {
            lengths[static_cast<std::size_t>(used[0])] = 1;
            return lengths;
        }

        struct Node {
            std::uint64_t weight = 0;
            int left = -1;
            int right = -1;
            int symbol = -1;
        };
        std::vector<Node> nodes;
        nodes.reserve(used.size() * 2);
        using Entry = std::pair<std::uint64_t, int>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;
        for (int symbol : used) {
            Node leaf;
            leaf.weight = work[static_cast<std::size_t>(symbol)];
            leaf.symbol = symbol;
            nodes.push_back(leaf);
            heap.emplace(leaf.weight, static_cast<int>(nodes.size()) - 1);
        }
        while (heap.size() > 1) {
            const Entry a = heap.top();
            heap.pop();
            const Entry b = heap.top();
            heap.pop();
            Node parent;
            parent.weight = a.first + b.first;
            parent.left = a.second;
            parent.right = b.second;
            nodes.push_back(parent);
            heap.emplace(parent.weight, static_cast<int>(nodes.size()) - 1);
        }

        int longest = 0;
        std::vector<std::pair<int, int>> stack;
        stack.emplace_back(heap.top().second, 0);
        while (!stack.empty()) {
            const std::pair<int, int> current = stack.back();
            stack.pop_back();
            const Node& node = nodes[static_cast<std::size_t>(current.first)];
            if (node.symbol >= 0) {
                const int depth = current.second > 0 ? current.second : 1;
                lengths[static_cast<std::size_t>(node.symbol)] = static_cast<std::uint8_t>(depth);
                longest = std::max(longest, depth);
                continue;
            }
            stack.emplace_back(node.left, current.second + 1);
            stack.emplace_back(node.right, current.second + 1);
        }

        if (longest <= kMaxCodeLength) return lengths;
        for (int i = 0; i < kAlphabetSize; ++i) {
            if (work[static_cast<std::size_t>(i)] > 0) {
                work[static_cast<std::size_t>(i)] = (work[static_cast<std::size_t>(i)] + 1) / 2;
            }
        }
    }
}

inline HuffmanTable buildCanonicalTable(const LengthTable& lengths) {
    HuffmanTable table;
    table.lengths = lengths;

    std::uint64_t kraft = 0;
    for (int symbol = 0; symbol < kAlphabetSize; ++symbol) {
        const int length = lengths[static_cast<std::size_t>(symbol)];
        if (length == 0) continue;
        if (length > kMaxCodeLength) throw std::runtime_error("huffman code length out of range");
        kraft += static_cast<std::uint64_t>(1) << (kMaxCodeLength - length);
        table.sortedSymbols.push_back(static_cast<std::uint8_t>(symbol));
        ++table.countPerLength[static_cast<std::size_t>(length)];
    }
    if (table.sortedSymbols.empty()) return table;
    if (kraft > (static_cast<std::uint64_t>(1) << kMaxCodeLength)) {
        throw std::runtime_error("huffman code lengths are not a prefix code");
    }

    std::stable_sort(table.sortedSymbols.begin(), table.sortedSymbols.end(),
                     [&lengths](std::uint8_t a, std::uint8_t b) {
                         if (lengths[a] != lengths[b]) return lengths[a] < lengths[b];
                         return a < b;
                     });

    std::uint32_t code = 0;
    int index = 0;
    for (int length = 1; length <= kMaxCodeLength; ++length) {
        table.firstCode[static_cast<std::size_t>(length)] = code;
        table.firstIndex[static_cast<std::size_t>(length)] = index;
        const int count = table.countPerLength[static_cast<std::size_t>(length)];
        for (int i = 0; i < count; ++i) {
            const std::uint8_t symbol = table.sortedSymbols[static_cast<std::size_t>(index + i)];
            table.codes[symbol] = code + static_cast<std::uint32_t>(i);
        }
        index += count;
        code = (code + static_cast<std::uint32_t>(count)) << 1;
    }
    return table;
}

inline HuffmanTable buildHuffmanTable(const FrequencyTable& frequencies) {
    return buildCanonicalTable(computeCodeLengths(frequencies));
}

inline void appendTable(std::vector<std::uint8_t>& out, const HuffmanTable& table) {
    const std::uint32_t count = static_cast<std::uint32_t>(table.sortedSymbols.size());
    out.push_back(static_cast<std::uint8_t>(count & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((count >> 8) & 0xFFu));
    for (std::uint8_t symbol : table.sortedSymbols) {
        out.push_back(symbol);
        out.push_back(table.lengths[symbol]);
    }
}

inline HuffmanTable readTable(const std::vector<std::uint8_t>& data, std::size_t& offset) {
    if (offset + 2 > data.size()) throw std::runtime_error("truncated huffman table header");
    const std::size_t count = static_cast<std::size_t>(data[offset]) |
                              (static_cast<std::size_t>(data[offset + 1]) << 8);
    offset += 2;
    if (count > kAlphabetSize) throw std::runtime_error("huffman table symbol count out of range");
    if (offset + count * 2 > data.size()) throw std::runtime_error("truncated huffman table body");

    LengthTable lengths{};
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint8_t symbol = data[offset];
        const std::uint8_t length = data[offset + 1];
        offset += 2;
        if (length == 0 || length > kMaxCodeLength) {
            throw std::runtime_error("invalid huffman code length");
        }
        if (lengths[symbol] != 0) throw std::runtime_error("duplicate huffman symbol");
        lengths[symbol] = length;
    }
    return buildCanonicalTable(lengths);
}

inline void encodeSymbol(BitWriter& writer, const HuffmanTable& table, std::uint8_t symbol) {
    const std::uint8_t length = table.lengths[symbol];
    if (length == 0) throw std::runtime_error("symbol missing from huffman table");
    writer.writeBits(table.codes[symbol], length);
}

inline std::uint8_t decodeSymbol(BitReader& reader, const HuffmanTable& table) {
    if (table.empty()) throw std::runtime_error("cannot decode with an empty huffman table");
    std::uint32_t code = 0;
    for (int length = 1; length <= kMaxCodeLength; ++length) {
        const int bit = reader.readBit();
        if (bit < 0) throw std::runtime_error("bit stream exhausted while decoding symbol");
        code = (code << 1) | static_cast<std::uint32_t>(bit);
        const int count = table.countPerLength[static_cast<std::size_t>(length)];
        if (count > 0) {
            const std::uint32_t first = table.firstCode[static_cast<std::size_t>(length)];
            if (code >= first && code - first < static_cast<std::uint32_t>(count)) {
                const std::size_t index =
                    static_cast<std::size_t>(table.firstIndex[static_cast<std::size_t>(length)]) +
                    static_cast<std::size_t>(code - first);
                return table.sortedSymbols[index];
            }
        }
    }
    throw std::runtime_error("invalid huffman code in bit stream");
}

}
