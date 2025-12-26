# --------------------------------------
# Project
# --------------------------------------
TARGET := StereoOpenCV2
SRCS   := main.cpp
OBJS   := $(SRCS:.cpp=.o)

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
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS) $(OPENCV_LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	./$(TARGET)

debug:
	$(MAKE) BUILD=debug

.PHONY: all clean run debug
