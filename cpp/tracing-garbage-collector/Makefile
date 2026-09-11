CXX      ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
LDFLAGS  ?=

SRC_DIR  := src
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/tracing-garbage-collector
TEST_TARGET := $(BIN_DIR)/test_tracing_garbage_collector
HEADERS  := $(SRC_DIR)/object_model.hpp $(SRC_DIR)/mark_sweep.hpp $(SRC_DIR)/copying.hpp $(SRC_DIR)/generational.hpp

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

$(TEST_TARGET): tests/test_tracing_garbage_collector.cpp $(HEADERS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_tracing_garbage_collector.cpp $(LDFLAGS)

clean:
	rm -rf $(BIN_DIR)
