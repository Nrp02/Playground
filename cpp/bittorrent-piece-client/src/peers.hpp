#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "sha1.hpp"
#include "torrent_metadata.hpp"
#include "wire_protocol.hpp"

namespace peer {

class Seeder {
public:
    Seeder(std::string fileData, torrent::Metadata metadata)
        : fileData_(std::move(fileData)), metadata_(std::move(metadata)) {}

    wire::Handshake handshake(const std::array<uint8_t, 20>& infoHash,
                               const std::array<uint8_t, 20>& peerId) const {
        wire::Handshake hs;
        hs.infoHash = infoHash;
        hs.peerId = peerId;
        return hs;
    }

    wire::BitfieldMessage bitfield() const {
        wire::BitfieldMessage msg;
        std::size_t numBytes = (metadata_.pieceCount() + 7) / 8;
        msg.bitfield.assign(numBytes, 0);
        for (std::size_t i = 0; i < metadata_.pieceCount(); ++i) {
            msg.bitfield[i / 8] |= static_cast<uint8_t>(0x80 >> (i % 8));
        }
        return msg;
    }

    wire::PieceMessage handleRequest(const wire::RequestMessage& request, bool corruptResponse) const {
        std::size_t offset = static_cast<std::size_t>(request.pieceIndex) *
                              static_cast<std::size_t>(metadata_.pieceLength) + request.begin;
        if (offset + request.length > fileData_.size()) {
            throw std::runtime_error("requested block out of bounds");
        }
        wire::PieceMessage msg;
        msg.pieceIndex = request.pieceIndex;
        msg.begin = request.begin;
        msg.block.assign(fileData_.begin() + static_cast<long>(offset),
                          fileData_.begin() + static_cast<long>(offset + request.length));
        if (corruptResponse && !msg.block.empty()) {
            msg.block[0] ^= 0xFF;
        }
        return msg;
    }

private:
    std::string fileData_;
    torrent::Metadata metadata_;
};

class Leecher {
public:
    explicit Leecher(torrent::Metadata metadata) : metadata_(std::move(metadata)) {
        assembled_.resize(static_cast<std::size_t>(metadata_.fileLength));
        pieceVerified_.assign(metadata_.pieceCount(), false);
    }

    wire::RequestMessage buildRequest(std::size_t pieceIndex) const {
        std::size_t offset = pieceIndex * static_cast<std::size_t>(metadata_.pieceLength);
        std::size_t len = std::min(static_cast<std::size_t>(metadata_.pieceLength),
                                    static_cast<std::size_t>(metadata_.fileLength) - offset);
        wire::RequestMessage req;
        req.pieceIndex = static_cast<uint32_t>(pieceIndex);
        req.begin = 0;
        req.length = static_cast<uint32_t>(len);
        return req;
    }

    bool acceptPiece(const wire::PieceMessage& piece) {
        if (piece.pieceIndex >= metadata_.pieceCount()) {
            throw std::runtime_error("piece index out of range");
        }
        std::string blockData(piece.block.begin(), piece.block.end());
        std::array<uint8_t, 20> actualHash = sha1::hash(blockData);
        const std::array<uint8_t, 20>& expectedHash = metadata_.pieceHashes[piece.pieceIndex];
        if (actualHash != expectedHash) {
            return false;
        }
        std::size_t offset = static_cast<std::size_t>(piece.pieceIndex) *
                              static_cast<std::size_t>(metadata_.pieceLength) + piece.begin;
        std::copy(piece.block.begin(), piece.block.end(), assembled_.begin() + static_cast<long>(offset));
        pieceVerified_[piece.pieceIndex] = true;
        return true;
    }

    bool allPiecesVerified() const {
        for (bool verified : pieceVerified_) {
            if (!verified) return false;
        }
        return true;
    }

    const std::string& assembledFile() const { return assembled_; }

private:
    torrent::Metadata metadata_;
    std::string assembled_;
    std::vector<bool> pieceVerified_;
};

}
