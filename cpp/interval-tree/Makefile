CXX      ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
LDFLAGS  ?=

SRC_DIR  := src
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/interval-tree
TEST_TARGET := $(BIN_DIR)/test_interval_tree

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(SRC_DIR)/main.cpp $(SRC_DIR)/interval_tree.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC_DIR)/main.cpp $(LDFLAGS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

run: $(TARGET)
	$(TARGET)

test: $(TEST_TARGET)
	$(TEST_TARGET)

$(TEST_TARGET): tests/test_interval_tree.cpp $(SRC_DIR)/interval_tree.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_interval_tree.cpp

clean:
	rm -rf $(BIN_DIR)
