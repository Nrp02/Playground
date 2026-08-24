CXX      ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
LDFLAGS  ?=

SRC_DIR  := src
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/circuit-breaker
TEST_TARGET := $(BIN_DIR)/test_circuit_breaker

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(SRC_DIR)/main.cpp $(SRC_DIR)/circuit_breaker.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC_DIR)/main.cpp $(LDFLAGS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

run: $(TARGET)
	$(TARGET)

test: $(TEST_TARGET)
	$(TEST_TARGET)

$(TEST_TARGET): tests/test_circuit_breaker.cpp $(SRC_DIR)/circuit_breaker.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_circuit_breaker.cpp

clean:
	rm -rf $(BIN_DIR)
