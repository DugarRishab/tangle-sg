# Compiler
CXX = g++
PYTHON_CONFIG = python3-config
CXXFLAGS = -std=c++17 -Wall -Wextra -I src/headers 
LDFLAGS = -lssl -lcrypto -lpthread -lrt -lm


# Directories
SRC_DIR = src
MODULES_DIR = src/modules
HEADERS_DIR = src/headers
BUILD_DIR = build

# Source and object files
SRC = $(SRC_DIR)/main.cpp $(MODULES_DIR)/pow.cpp $(MODULES_DIR)/tsa.cpp $(MODULES_DIR)/network.cpp $(MODULES_DIR)/tangle.cpp
OBJ = $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(notdir $(SRC)))
EXEC = tangle_sg

# Default target
all: $(BUILD_DIR) $(EXEC)

# Build executable
$(EXEC): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(EXEC) $(OBJ) $(LDFLAGS)

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(MODULES_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Ensure build directory exists
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) $(EXEC)
