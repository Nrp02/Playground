#include <iostream>
#include <string>

#include "bencode.hpp"
#include "peers.hpp"
#include "sha1.hpp"
#include "torrent_metadata.hpp"
#include "wire_protocol.hpp"

namespace {

std::string buildSampleFile() {
    std::string data;
    for (int i = 0; i < 5; ++i) {
        data += "The quick brown fox jumps over the lazy dog. Piece #" + std::to_string(i) + " content padding. ";
    }
    return data;
}

}

int main() {
    std::cout << "=== SHA-1 sanity check ===\n";
    std::cout << "SHA1(\"\")    = " << sha1::toHex(sha1::hash("")) << "\n";
    std::cout << "SHA1(\"abc\") = " << sha1::toHex(sha1::hash("abc")) << "\n";

    std::string fileData = buildSampleFile();
    constexpr int64_t kPieceLength = 32;
    torrent::Metadata metadata = torrent::buildMetadata(fileData, kPieceLength);

    std::cout << "\n=== torrent metadata ===\n";
    std::cout << "file length=" << metadata.fileLength << " piece length=" << metadata.pieceLength
              << " piece count=" << metadata.pieceCount() << "\n";

    bencode::BValue encoded = torrent::encodeMetadata(metadata);
    std::string bencodedBytes = encoded.encode();
    std::cout << "bencoded metadata size=" << bencodedBytes.size() << " bytes\n";

    bencode::BValue decoded = bencode::decode(bencodedBytes);
    torrent::Metadata roundTripped = torrent::decodeMetadata(decoded);
    bool metadataRoundTripOk = roundTripped.fileLength == metadata.fileLength &&
                               roundTripped.pieceLength == metadata.pieceLength &&
                               roundTripped.pieceHashes == metadata.pieceHashes;
    std::cout << "bencode round-trip matches original metadata: " << std::boolalpha << metadataRoundTripOk << "\n";

    std::array<uint8_t, 20> infoHash = sha1::hash(bencodedBytes);
    std::array<uint8_t, 20> seederPeerId{};
    std::array<uint8_t, 20> leecherPeerId{};
    for (std::size_t i = 0; i < 20; ++i) {
        seederPeerId[i] = static_cast<uint8_t>('S');
        leecherPeerId[i] = static_cast<uint8_t>('L');
    }

    peer::Seeder seeder(fileData, metadata);
    peer::Leecher leecher(metadata);

    std::cout << "\n=== handshake ===\n";
    wire::Handshake handshake = seeder.handshake(infoHash, seederPeerId);
    wire::Bytes handshakeBytes = handshake.serialize();
    wire::Handshake parsedHandshake = wire::Handshake::deserialize(handshakeBytes);
    bool handshakeOk = parsedHandshake.infoHash == infoHash && parsedHandshake.peerId == seederPeerId;
    std::cout << "handshake serialize/deserialize round-trip ok: " << handshakeOk << "\n";

    std::cout << "\n=== bitfield exchange ===\n";
    wire::BitfieldMessage bitfieldMsg = seeder.bitfield();
    wire::Bytes bitfieldBytes = bitfieldMsg.serialize();
    wire::BitfieldMessage parsedBitfield = wire::BitfieldMessage::deserialize(bitfieldBytes);
    std::cout << "seeder advertises " << parsedBitfield.bitfield.size() << " bitfield bytes for "
              << metadata.pieceCount() << " pieces\n";

    std::cout << "\n=== piece exchange with one corrupted response ===\n";
    std::size_t corruptedPieceIndex = 2;
    for (std::size_t i = 0; i < metadata.pieceCount(); ++i) {
        wire::RequestMessage request = leecher.buildRequest(i);
        wire::Bytes requestBytes = request.serialize();
        wire::RequestMessage parsedRequest = wire::RequestMessage::deserialize(requestBytes);

        bool corruptThisResponse = (i == corruptedPieceIndex);
        wire::PieceMessage response = seeder.handleRequest(parsedRequest, corruptThisResponse);
        wire::Bytes responseBytes = response.serialize();
        wire::PieceMessage parsedResponse = wire::PieceMessage::deserialize(responseBytes);

        bool accepted = leecher.acceptPiece(parsedResponse);
        std::cout << "piece " << i << (corruptThisResponse ? " (deliberately corrupted)" : "")
                  << " accepted on first try: " << accepted << "\n";

        if (!accepted) {
            wire::PieceMessage retryResponse = seeder.handleRequest(parsedRequest, false);
            wire::Bytes retryBytes = retryResponse.serialize();
            wire::PieceMessage parsedRetry = wire::PieceMessage::deserialize(retryBytes);
            bool retryAccepted = leecher.acceptPiece(parsedRetry);
            std::cout << "piece " << i << " accepted after retry: " << retryAccepted << "\n";
        }
    }

    std::cout << "\n=== assembly result ===\n";
    std::cout << "all pieces verified: " << leecher.allPiecesVerified() << "\n";
    bool matches = leecher.assembledFile() == fileData;
    std::cout << "assembled file matches original byte-for-byte: " << matches << "\n";

    return matches && metadataRoundTripOk && handshakeOk ? 0 : 1;
}
