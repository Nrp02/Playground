CXX      ?= c++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -O2
LDFLAGS  ?=

SRC_DIR  := src
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/coroutine-scheduler
TEST_TARGET := $(BIN_DIR)/test_coroutine_scheduler

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(SRC_DIR)/main.cpp $(SRC_DIR)/task.hpp $(SRC_DIR)/scheduler.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC_DIR)/main.cpp $(LDFLAGS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

run: $(TARGET)
	$(TARGET)

test: $(TEST_TARGET)
	$(TEST_TARGET)

$(TEST_TARGET): tests/test_coroutine_scheduler.cpp $(SRC_DIR)/task.hpp $(SRC_DIR)/scheduler.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_coroutine_scheduler.cpp

clean:
	rm -rf $(BIN_DIR)
