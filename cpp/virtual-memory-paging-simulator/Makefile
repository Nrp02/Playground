CXX      ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
LDFLAGS  ?=

SRC_DIR  := src
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/virtual-memory-paging-simulator
TEST_TARGET := $(BIN_DIR)/test_paging_simulator

HEADERS := $(SRC_DIR)/paging_types.hpp $(SRC_DIR)/backing_store.hpp $(SRC_DIR)/page_table.hpp \
           $(SRC_DIR)/tlb.hpp $(SRC_DIR)/replacement.hpp $(SRC_DIR)/working_set.hpp \
           $(SRC_DIR)/paging_simulator.hpp $(SRC_DIR)/workload.hpp $(SRC_DIR)/study.hpp

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

$(TEST_TARGET): tests/test_paging_simulator.cpp $(HEADERS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_paging_simulator.cpp $(LDFLAGS)

clean:
	rm -rf $(BIN_DIR)
