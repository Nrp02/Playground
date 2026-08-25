#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "bencode.hpp"
#include "sha1.hpp"

namespace torrent {

struct Metadata {
    int64_t fileLength = 0;
    int64_t pieceLength = 0;
    std::vector<std::array<uint8_t, 20>> pieceHashes;

    std::size_t pieceCount() const { return pieceHashes.size(); }
};

inline Metadata buildMetadata(const std::string& fileData, int64_t pieceLength) {
    Metadata meta;
    meta.fileLength = static_cast<int64_t>(fileData.size());
    meta.pieceLength = pieceLength;
    for (std::size_t offset = 0; offset < fileData.size(); offset += static_cast<std::size_t>(pieceLength)) {
        std::size_t len = std::min(static_cast<std::size_t>(pieceLength), fileData.size() - offset);
        meta.pieceHashes.push_back(sha1::hash(fileData.substr(offset, len)));
    }
    return meta;
}

inline bencode::BValue encodeMetadata(const Metadata& meta) {
    bencode::BDict dict;
    dict.emplace("length", bencode::BValue::makeInt(meta.fileLength));
    dict.emplace("piece length", bencode::BValue::makeInt(meta.pieceLength));

    std::string concatenatedHashes;
    concatenatedHashes.reserve(meta.pieceHashes.size() * 20);
    for (const auto& hash : meta.pieceHashes) {
        concatenatedHashes.append(reinterpret_cast<const char*>(hash.data()), hash.size());
    }
    dict.emplace("pieces", bencode::BValue::makeString(concatenatedHashes));

    return bencode::BValue::makeDict(std::move(dict));
}

inline Metadata decodeMetadata(const bencode::BValue& value) {
    const bencode::BDict& dict = value.asDict();
    Metadata meta;
    meta.fileLength = dict.at("length").asInt();
    meta.pieceLength = dict.at("piece length").asInt();

    const std::string& pieces = dict.at("pieces").asString();
    if (pieces.size() % 20 != 0) {
        throw std::runtime_error("pieces field length is not a multiple of 20");
    }
    for (std::size_t offset = 0; offset < pieces.size(); offset += 20) {
        std::array<uint8_t, 20> hash{};
        for (std::size_t i = 0; i < 20; ++i) {
            hash[i] = static_cast<uint8_t>(pieces[offset + i]);
        }
        meta.pieceHashes.push_back(hash);
    }
    return meta;
}

}
