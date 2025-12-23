# --------------------------------------
# Project
# --------------------------------------
TARGET := StereoOpenCV
SRCS   := main.cpp
OBJS   := $(SRCS:.cpp=.o)

# --------------------------------------
# Compiler & flags
# --------------------------------------
CXX      := g++

OPENCV_CFLAGS	:= $(shell pkg-config --cflags opencv4)
OPENCV_LIBS		:= $(shell pkg-config --libs opencv4)

CXXFLAGS := -std=gnu++17 -Wall -Wextra -Wpedantic -O2 -g -pthread $(OPENCV_CFLAGS)
LDFLAGS  := -pthread
# LDLIBS   := $(shell pkg-config --cflags --libs opencv4)

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

.PHONY: all clean run
