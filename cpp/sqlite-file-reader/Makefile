CXX      ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
LDFLAGS  ?=

SRC_DIR  := src
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/sqlite-file-reader
TEST_TARGET := $(BIN_DIR)/test_sqlite_file_reader

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(SRC_DIR)/main.cpp $(SRC_DIR)/sqlite_reader.hpp $(SRC_DIR)/varint.hpp tests/fixture_builder.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC_DIR)/main.cpp $(LDFLAGS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

run: $(TARGET)
	$(TARGET)

test: $(TEST_TARGET)
	$(TEST_TARGET)

$(TEST_TARGET): tests/test_sqlite_reader.cpp $(SRC_DIR)/sqlite_reader.hpp $(SRC_DIR)/varint.hpp tests/fixture_builder.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_sqlite_reader.cpp

clean:
	rm -rf $(BIN_DIR)
