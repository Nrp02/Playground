CXX      ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
LDFLAGS  ?=

SRC_DIR  := src
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/suffix-array
TEST_TARGET := $(BIN_DIR)/test_suffix_array

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(SRC_DIR)/main.cpp $(SRC_DIR)/suffix_array.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC_DIR)/main.cpp $(LDFLAGS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

run: $(TARGET)
	$(TARGET)

test: $(TEST_TARGET)
	$(TEST_TARGET)

$(TEST_TARGET): tests/test_suffix_array.cpp $(SRC_DIR)/suffix_array.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_suffix_array.cpp

clean:
	rm -rf $(BIN_DIR)
