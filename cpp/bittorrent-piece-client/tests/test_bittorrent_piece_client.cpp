#include <array>
#include <iostream>
#include <string>

#include "../src/bencode.hpp"
#include "../src/peers.hpp"
#include "../src/sha1.hpp"
#include "../src/torrent_metadata.hpp"
#include "../src/wire_protocol.hpp"

namespace {

int g_failures = 0;

void expectTrue(bool condition, const std::string& testName) {
    if (!condition) {
        std::cerr << "FAIL: " << testName << "\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

template <typename T>
void expectEq(const T& actual, const T& expected, const std::string& testName) {
    if (!(actual == expected)) {
        std::cerr << "FAIL: " << testName << "\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

}

int main() {
    {
        expectEq<std::string>(sha1::toHex(sha1::hash("")), "da39a3ee5e6b4b0d3255bfef95601890afd80709",
                               "SHA1 of empty string matches known vector");
        expectEq<std::string>(sha1::toHex(sha1::hash("abc")), "a9993e364706816aba3e25717850c26c9cd0d89d",
                               "SHA1 of \"abc\" matches known vector");
    }

    {
        bencode::BValue v = bencode::BValue::makeInt(42);
        expectEq<std::string>(v.encode(), "i42e", "bencode int encode");
        bencode::BValue decoded = bencode::decode(v.encode());
        expectEq<int64_t>(decoded.asInt(), 42, "bencode int round-trip");
    }

    {
        bencode::BValue v = bencode::BValue::makeInt(-7);
        expectEq<std::string>(v.encode(), "i-7e", "bencode negative int encode");
    }

    {
        bencode::BValue v = bencode::BValue::makeString("spam");
        expectEq<std::string>(v.encode(), "4:spam", "bencode string encode");
        bencode::BValue decoded = bencode::decode(v.encode());
        expectEq<std::string>(decoded.asString(), "spam", "bencode string round-trip");
    }

    {
        bencode::BList list;
        list.push_back(bencode::BValue::makeString("spam"));
        list.push_back(bencode::BValue::makeInt(3));
        bencode::BValue v = bencode::BValue::makeList(list);
        expectEq<std::string>(v.encode(), "l4:spami3ee", "bencode list encode");
        bencode::BValue decoded = bencode::decode(v.encode());
        expectEq<std::size_t>(decoded.asList().size(), 2, "bencode list round-trip size");
        expectEq<int64_t>(decoded.asList()[1].asInt(), 3, "bencode list round-trip element");
    }

    {
        bencode::BDict dict;
        dict.emplace("cow", bencode::BValue::makeString("moo"));
        dict.emplace("spam", bencode::BValue::makeString("eggs"));
        bencode::BValue v = bencode::BValue::makeDict(dict);
        expectEq<std::string>(v.encode(), "d3:cow3:moo4:spam4:eggse", "bencode dict encode with sorted keys");
    }

    {
        bencode::BDict inner;
        inner.emplace("a", bencode::BValue::makeInt(1));
        bencode::BList outerList;
        outerList.push_back(bencode::BValue::makeDict(inner));
        outerList.push_back(bencode::BValue::makeString("tail"));
        bencode::BValue v = bencode::BValue::makeList(outerList);
        bencode::BValue decoded = bencode::decode(v.encode());
        expectEq<std::size_t>(decoded.asList().size(), 2, "nested bencode round-trip list size");
        expectEq<int64_t>(decoded.asList()[0].asDict().at("a").asInt(), 1, "nested bencode round-trip dict value");
        expectEq<std::string>(decoded.asList()[1].asString(), "tail", "nested bencode round-trip string tail");
    }

    {
        wire::Handshake hs;
        for (std::size_t i = 0; i < 20; ++i) {
            hs.infoHash[i] = static_cast<uint8_t>(i);
            hs.peerId[i] = static_cast<uint8_t>(i + 100);
        }
        wire::Bytes serialized = hs.serialize();
        wire::Handshake parsed = wire::Handshake::deserialize(serialized);
        expectTrue(parsed.infoHash == hs.infoHash, "handshake infoHash round-trip");
        expectTrue(parsed.peerId == hs.peerId, "handshake peerId round-trip");
    }

    {
        wire::BitfieldMessage msg;
        msg.bitfield = {0xFF, 0x80};
        wire::Bytes serialized = msg.serialize();
        wire::BitfieldMessage parsed = wire::BitfieldMessage::deserialize(serialized);
        expectTrue(parsed.bitfield == msg.bitfield, "bitfield message round-trip");
    }

    {
        wire::RequestMessage msg;
        msg.pieceIndex = 3;
        msg.begin = 16;
        msg.length = 32;
        wire::Bytes serialized = msg.serialize();
        wire::RequestMessage parsed = wire::RequestMessage::deserialize(serialized);
        expectEq<uint32_t>(parsed.pieceIndex, 3, "request pieceIndex round-trip");
        expectEq<uint32_t>(parsed.begin, 16, "request begin round-trip");
        expectEq<uint32_t>(parsed.length, 32, "request length round-trip");
    }

    {
        wire::PieceMessage msg;
        msg.pieceIndex = 1;
        msg.begin = 0;
        msg.block = {1, 2, 3, 4, 5};
        wire::Bytes serialized = msg.serialize();
        wire::PieceMessage parsed = wire::PieceMessage::deserialize(serialized);
        expectEq<uint32_t>(parsed.pieceIndex, 1, "piece pieceIndex round-trip");
        expectTrue(parsed.block == msg.block, "piece block round-trip");
    }

    {
        std::string fileData = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        int64_t pieceLength = 10;
        torrent::Metadata metadata = torrent::buildMetadata(fileData, pieceLength);

        bencode::BValue encoded = torrent::encodeMetadata(metadata);
        bencode::BValue decoded = bencode::decode(encoded.encode());
        torrent::Metadata roundTripped = torrent::decodeMetadata(decoded);
        expectEq<int64_t>(roundTripped.fileLength, metadata.fileLength, "torrent metadata fileLength round-trip");
        expectEq<int64_t>(roundTripped.pieceLength, metadata.pieceLength, "torrent metadata pieceLength round-trip");
        expectTrue(roundTripped.pieceHashes == metadata.pieceHashes, "torrent metadata piece hashes round-trip");

        peer::Seeder seeder(fileData, metadata);
        peer::Leecher leecher(metadata);

        for (std::size_t i = 0; i < metadata.pieceCount(); ++i) {
            wire::RequestMessage request = leecher.buildRequest(i);
            wire::PieceMessage response = seeder.handleRequest(request, false);
            bool accepted = leecher.acceptPiece(response);
            expectTrue(accepted, "leecher accepts uncorrupted piece " + std::to_string(i));
        }
        expectTrue(leecher.allPiecesVerified(), "all pieces verified after full download");
        expectTrue(leecher.assembledFile() == fileData, "assembled file matches original");
    }

    {
        std::string fileData = "0123456789abcdef0123456789abcdef0123456789abcdef";
        int64_t pieceLength = 16;
        torrent::Metadata metadata = torrent::buildMetadata(fileData, pieceLength);

        peer::Seeder seeder(fileData, metadata);
        peer::Leecher leecher(metadata);

        std::size_t targetPiece = 1;
        wire::RequestMessage request = leecher.buildRequest(targetPiece);
        wire::PieceMessage corrupted = seeder.handleRequest(request, true);
        bool acceptedCorrupted = leecher.acceptPiece(corrupted);
        expectTrue(!acceptedCorrupted, "corrupted piece is rejected");
        expectTrue(!leecher.allPiecesVerified(), "not all pieces verified after rejection");

        wire::PieceMessage retry = seeder.handleRequest(request, false);
        bool acceptedRetry = leecher.acceptPiece(retry);
        expectTrue(acceptedRetry, "retried piece is accepted after rejection");
    }

    std::cout << "\n" << g_failures << " failing test(s)\n";
    return g_failures == 0 ? 0 : 1;
}
