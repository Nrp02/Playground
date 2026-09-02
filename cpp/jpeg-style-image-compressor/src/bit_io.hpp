#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jic {

class BitWriter {
public:
    void writeBit(int bit) {
        accumulator_ = static_cast<std::uint32_t>((accumulator_ << 1) | static_cast<std::uint32_t>(bit & 1));
        ++pending_;
        ++bitCount_;
        if (pending_ == 8) {
            bytes_.push_back(static_cast<std::uint8_t>(accumulator_ & 0xFFu));
            accumulator_ = 0;
            pending_ = 0;
        }
    }

    void writeBits(std::uint32_t value, int count) {
        for (int i = count - 1; i >= 0; --i) {
            writeBit(static_cast<int>((value >> i) & 1u));
        }
    }

    std::vector<std::uint8_t> finish() const {
        std::vector<std::uint8_t> out = bytes_;
        if (pending_ > 0) {
            out.push_back(static_cast<std::uint8_t>((accumulator_ << (8 - pending_)) & 0xFFu));
        }
        return out;
    }

    std::size_t bitCount() const { return bitCount_; }

    std::size_t byteCount() const { return (bitCount_ + 7) / 8; }

private:
    std::vector<std::uint8_t> bytes_;
    std::uint32_t accumulator_ = 0;
    int pending_ = 0;
    std::size_t bitCount_ = 0;
};

class BitReader {
public:
    BitReader(const std::uint8_t* data, std::size_t byteSize, std::size_t bitLimit)
        : data_(data), byteSize_(byteSize), bitLimit_(bitLimit) {
        const std::size_t available = byteSize_ * 8;
        if (bitLimit_ > available) bitLimit_ = available;
    }

    int readBit() {
        if (position_ >= bitLimit_) return -1;
        const std::uint8_t byte = data_[position_ >> 3];
        const int bit = (byte >> (7 - (position_ & 7))) & 1;
        ++position_;
        return bit;
    }

    bool readBits(int count, std::uint32_t& out) {
        std::uint32_t value = 0;
        for (int i = 0; i < count; ++i) {
            const int bit = readBit();
            if (bit < 0) return false;
            value = (value << 1) | static_cast<std::uint32_t>(bit);
        }
        out = value;
        return true;
    }

    std::size_t position() const { return position_; }

    std::size_t remaining() const { return bitLimit_ - position_; }

    bool exhausted() const { return position_ >= bitLimit_; }

private:
    const std::uint8_t* data_;
    std::size_t byteSize_;
    std::size_t bitLimit_;
    std::size_t position_ = 0;
};

}
