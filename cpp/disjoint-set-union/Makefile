CXX      ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
LDFLAGS  ?=

SRC_DIR  := src
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/disjoint-set-union
TEST_TARGET := $(BIN_DIR)/test_disjoint_set_union

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(SRC_DIR)/main.cpp $(SRC_DIR)/disjoint_set_union.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC_DIR)/main.cpp $(LDFLAGS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

run: $(TARGET)
	$(TARGET)

test: $(TEST_TARGET)
	$(TEST_TARGET)

$(TEST_TARGET): tests/test_disjoint_set_union.cpp $(SRC_DIR)/disjoint_set_union.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_disjoint_set_union.cpp

clean:
	rm -rf $(BIN_DIR)
