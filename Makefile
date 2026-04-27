# --------------------------------------
# Project
# --------------------------------------
TARGET := StereoOpenCV2
SRCS   := main.cpp

# Build directory
BUILD_DIR := build

# Objects live in build/
OBJS := $(SRCS:%.cpp=$(BUILD_DIR)/%.o)

# Final executable path
TARGET_PATH := $(BUILD_DIR)/$(TARGET)

# --------------------------------------
# Compiler
# --------------------------------------
CXX := g++

# --------------------------------------
# OpenCV
# --------------------------------------
OPENCV_CFLAGS := $(shell pkg-config --cflags opencv4)
OPENCV_LIBS   := $(shell pkg-config --libs opencv4)

# --------------------------------------
# Build modes
# --------------------------------------
BUILD ?= release

ifeq ($(BUILD),debug)
    CXXFLAGS := -std=gnu++17 -Wall -Wextra -Wpedantic -g -O0 -pthread $(OPENCV_CFLAGS)
else
    CXXFLAGS := -std=gnu++17 -Wall -Wextra -Wpedantic -O2 -g -pthread $(OPENCV_CFLAGS)
endif

LDFLAGS := -pthread

# --------------------------------------
# Build rules
# --------------------------------------
all: $(TARGET_PATH)

# Link step
$(TARGET_PATH): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS) $(OPENCV_LIBS)

# Compile step (puts .o in build/)
$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --------------------------------------
# Utilities
# --------------------------------------
clean:
	rm -rf $(BUILD_DIR)

run: $(TARGET_PATH)
	./$(TARGET_PATH)

debug:
	$(MAKE) BUILD=debug

.PHONY: all clean run debug