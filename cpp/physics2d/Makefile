CXX      ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
LDFLAGS  ?=

SRC_DIR  := src
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/physics2d
TEST_TARGET := $(BIN_DIR)/test_physics2d

CORE_SRCS := $(SRC_DIR)/world.cpp

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(SRC_DIR)/main.cpp $(CORE_SRCS) $(SRC_DIR)/vec2.hpp $(SRC_DIR)/shapes.hpp $(SRC_DIR)/broadphase.hpp $(SRC_DIR)/collision.hpp $(SRC_DIR)/world.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC_DIR)/main.cpp $(CORE_SRCS) $(LDFLAGS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

run: $(TARGET)
	$(TARGET)

test: $(TEST_TARGET)
	$(TEST_TARGET)

$(TEST_TARGET): tests/test_physics2d.cpp $(CORE_SRCS) $(SRC_DIR)/vec2.hpp $(SRC_DIR)/shapes.hpp $(SRC_DIR)/broadphase.hpp $(SRC_DIR)/collision.hpp $(SRC_DIR)/world.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_physics2d.cpp $(CORE_SRCS)

clean:
	rm -rf $(BIN_DIR)
