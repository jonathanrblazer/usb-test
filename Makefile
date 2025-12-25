# -----------------------------
# CM5 Stereo Vision App
# Debian 13 (trixie)
# -----------------------------

CXX       := g++
CXXFLAGS  := -std=c++17 -Wall -Wextra -O2 -pthread
CPPFLAGS  := -I./src
LDFLAGS   :=
LDLIBS    :=

# OpenCV via pkg-config
OPENCV_CFLAGS := $(shell pkg-config --cflags opencv4)
OPENCV_LIBS   := $(shell pkg-config --libs opencv4)

CXXFLAGS += $(OPENCV_CFLAGS)
LDLIBS   += $(OPENCV_LIBS)

# -----------------------------
# Sources / Objects
# -----------------------------

SRC := \
    src/main.cpp \
    src/spi_mock.cpp \
    src/serial_mock.cpp

OBJ := $(SRC:.cpp=.o)

TARGET := cm5_stereo

# -----------------------------
# Build rules
# -----------------------------

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
