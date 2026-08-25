CXX      ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -pthread
LDFLAGS  ?= -pthread

SRC_DIR  := src
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/connection-pool
TEST_TARGET := $(BIN_DIR)/test_connection_pool

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(SRC_DIR)/main.cpp $(SRC_DIR)/connection_pool.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC_DIR)/main.cpp $(LDFLAGS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

run: $(TARGET)
	$(TARGET)

test: $(TEST_TARGET)
	$(TEST_TARGET)

$(TEST_TARGET): tests/test_connection_pool.cpp $(SRC_DIR)/connection_pool.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_connection_pool.cpp $(LDFLAGS)

clean:
	rm -rf $(BIN_DIR)
