CXX      ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
LDFLAGS  ?=

SRC_DIR  := src
BIN_DIR  := bin
HEADERS  := $(SRC_DIR)/vec2.hpp $(SRC_DIR)/body.hpp $(SRC_DIR)/quadtree.hpp $(SRC_DIR)/nbody.hpp
TARGET   := $(BIN_DIR)/barnes-hut-nbody
TEST_TARGET := $(BIN_DIR)/test_barnes_hut

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

$(TEST_TARGET): tests/test_barnes_hut.cpp $(HEADERS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_barnes_hut.cpp $(LDFLAGS)

clean:
	rm -rf $(BIN_DIR)
