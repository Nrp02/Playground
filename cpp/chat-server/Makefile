CXX      ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -pthread
LDFLAGS  ?= -pthread

SRC_DIR  := src
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/chat-server

SOURCES  := $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS  := $(SOURCES:$(SRC_DIR)/%.cpp=$(BIN_DIR)/%.o)
DEPS     := $(OBJECTS:.o=.d)

TEST_TARGET := $(BIN_DIR)/test_protocol

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS) $(LDFLAGS)

$(BIN_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

run: $(TARGET)
	$(TARGET)

# Builds and runs the protocol.hpp/.cpp unit tests. Only depends on
# protocol.cpp (not server.cpp/client_handler.cpp/main.cpp), so it doesn't
# need a second `main()` to fight with the real binary's.
test: $(TEST_TARGET)
	$(TEST_TARGET)

$(TEST_TARGET): tests/test_protocol.cpp $(SRC_DIR)/protocol.cpp $(SRC_DIR)/protocol.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_protocol.cpp $(SRC_DIR)/protocol.cpp

clean:
	rm -rf $(BIN_DIR)

-include $(DEPS)
