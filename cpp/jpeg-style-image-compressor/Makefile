CXX      ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
LDFLAGS  ?=

SRC_DIR  := src
BIN_DIR  := bin
HEADERS  := $(SRC_DIR)/bit_io.hpp $(SRC_DIR)/huffman.hpp $(SRC_DIR)/dct.hpp $(SRC_DIR)/image.hpp $(SRC_DIR)/codec.hpp
TARGET   := $(BIN_DIR)/jpeg-style-image-compressor
TEST_TARGET := $(BIN_DIR)/test_codec

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

$(TEST_TARGET): tests/test_codec.cpp $(HEADERS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_codec.cpp $(LDFLAGS)

clean:
	rm -rf $(BIN_DIR)
