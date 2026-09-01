CXX      ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -pthread
LDFLAGS  ?= -pthread

SRC_DIR  := src
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/slab-allocator
TEST_TARGET := $(BIN_DIR)/test_slab_allocator

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(SRC_DIR)/main.cpp $(SRC_DIR)/slab_allocator.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC_DIR)/main.cpp $(LDFLAGS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

run: $(TARGET)
	$(TARGET)

test: $(TEST_TARGET)
	$(TEST_TARGET)

$(TEST_TARGET): tests/test_slab_allocator.cpp $(SRC_DIR)/slab_allocator.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_slab_allocator.cpp $(LDFLAGS)

clean:
	rm -rf $(BIN_DIR)
