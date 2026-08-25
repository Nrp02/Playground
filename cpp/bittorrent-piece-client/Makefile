CXX      ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
LDFLAGS  ?=

SRC_DIR  := src
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/bittorrent-piece-client
TEST_TARGET := $(BIN_DIR)/test_bittorrent_piece_client

HEADERS := $(SRC_DIR)/bencode.hpp $(SRC_DIR)/sha1.hpp $(SRC_DIR)/wire_protocol.hpp \
           $(SRC_DIR)/torrent_metadata.hpp $(SRC_DIR)/peers.hpp

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(SRC_DIR)/main.cpp $(HEADERS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC_DIR)/main.cpp $(LDFLAGS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

run: $(TARGET)
	$(TARGET)

test: $(TEST_TARGET)
	$(TEST_TARGET)

$(TEST_TARGET): tests/test_bittorrent_piece_client.cpp $(HEADERS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_bittorrent_piece_client.cpp

clean:
	rm -rf $(BIN_DIR)
