#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace wire {

using Bytes = std::vector<uint8_t>;

inline void putU32(Bytes& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

inline uint32_t getU32(const Bytes& data, std::size_t offset) {
    if (offset + 4 > data.size()) throw std::runtime_error("buffer too short for u32");
    return (static_cast<uint32_t>(data[offset]) << 24) | (static_cast<uint32_t>(data[offset + 1]) << 16) |
           (static_cast<uint32_t>(data[offset + 2]) << 8) | static_cast<uint32_t>(data[offset + 3]);
}

constexpr const char* kProtocolString = "BitTorrent protocol";

struct Handshake {
    std::array<uint8_t, 20> infoHash{};
    std::array<uint8_t, 20> peerId{};

    Bytes serialize() const {
        Bytes out;
        out.push_back(static_cast<uint8_t>(std::string(kProtocolString).size()));
        for (char c : std::string(kProtocolString)) {
            out.push_back(static_cast<uint8_t>(c));
        }
        for (int i = 0; i < 8; ++i) {
            out.push_back(0);
        }
        out.insert(out.end(), infoHash.begin(), infoHash.end());
        out.insert(out.end(), peerId.begin(), peerId.end());
        return out;
    }

    static Handshake deserialize(const Bytes& data) {
        if (data.empty()) throw std::runtime_error("empty handshake");
        uint8_t pstrlen = data[0];
        std::size_t expectedSize = 1 + pstrlen + 8 + 20 + 20;
        if (data.size() != expectedSize) throw std::runtime_error("malformed handshake length");
        std::size_t offset = 1 + pstrlen + 8;
        Handshake hs;
        std::copy(data.begin() + static_cast<long>(offset), data.begin() + static_cast<long>(offset) + 20,
                  hs.infoHash.begin());
        offset += 20;
        std::copy(data.begin() + static_cast<long>(offset), data.begin() + static_cast<long>(offset) + 20,
                  hs.peerId.begin());
        return hs;
    }
};

enum class MessageId : uint8_t {
    Bitfield = 5,
    Request = 6,
    Piece = 7,
};

struct BitfieldMessage {
    Bytes bitfield;

    Bytes serialize() const {
        Bytes out;
        putU32(out, static_cast<uint32_t>(1 + bitfield.size()));
        out.push_back(static_cast<uint8_t>(MessageId::Bitfield));
        out.insert(out.end(), bitfield.begin(), bitfield.end());
        return out;
    }

    static BitfieldMessage deserialize(const Bytes& frame) {
        uint32_t length = getU32(frame, 0);
        if (frame.size() != 4 + length) throw std::runtime_error("malformed bitfield frame length");
        if (frame[4] != static_cast<uint8_t>(MessageId::Bitfield)) throw std::runtime_error("not a bitfield message");
        BitfieldMessage msg;
        msg.bitfield.assign(frame.begin() + 5, frame.end());
        return msg;
    }
};

struct RequestMessage {
    uint32_t pieceIndex = 0;
    uint32_t begin = 0;
    uint32_t length = 0;

    Bytes serialize() const {
        Bytes out;
        putU32(out, 13);
        out.push_back(static_cast<uint8_t>(MessageId::Request));
        putU32(out, pieceIndex);
        putU32(out, begin);
        putU32(out, length);
        return out;
    }

    static RequestMessage deserialize(const Bytes& frame) {
        uint32_t frameLength = getU32(frame, 0);
        if (frame.size() != 4 + frameLength) throw std::runtime_error("malformed request frame length");
        if (frame[4] != static_cast<uint8_t>(MessageId::Request)) throw std::runtime_error("not a request message");
        RequestMessage msg;
        msg.pieceIndex = getU32(frame, 5);
        msg.begin = getU32(frame, 9);
        msg.length = getU32(frame, 13);
        return msg;
    }
};

struct PieceMessage {
    uint32_t pieceIndex = 0;
    uint32_t begin = 0;
    Bytes block;

    Bytes serialize() const {
        Bytes out;
        putU32(out, static_cast<uint32_t>(9 + block.size()));
        out.push_back(static_cast<uint8_t>(MessageId::Piece));
        putU32(out, pieceIndex);
        putU32(out, begin);
        out.insert(out.end(), block.begin(), block.end());
        return out;
    }

    static PieceMessage deserialize(const Bytes& frame) {
        uint32_t frameLength = getU32(frame, 0);
        if (frame.size() != 4 + frameLength) throw std::runtime_error("malformed piece frame length");
        if (frame[4] != static_cast<uint8_t>(MessageId::Piece)) throw std::runtime_error("not a piece message");
        PieceMessage msg;
        msg.pieceIndex = getU32(frame, 5);
        msg.begin = getU32(frame, 9);
        msg.block.assign(frame.begin() + 13, frame.end());
        return msg;
    }
};

}
