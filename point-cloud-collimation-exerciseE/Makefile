CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -pedantic -g
PKG_CONFIG ?= pkg-config
GST_CFLAGS := $(shell $(PKG_CONFIG) --cflags gstreamer-1.0 gstreamer-app-1.0)
GST_LIBS := $(shell $(PKG_CONFIG) --libs gstreamer-1.0 gstreamer-app-1.0)

TARGET := point_cloud_collimation
TARGET_OPT := point_cloud_collimation_optimized
SRC := point_cloud_collimation.cpp
SRC_OPT := point_cloud_collimation_optimized.cpp

.PHONY: all run viewer clean

all: $(TARGET) $(TARGET_OPT)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(GST_CFLAGS) $< -o $@ $(GST_LIBS)

$(TARGET_OPT): $(SRC_OPT)
	$(CXX) $(CXXFLAGS) $(GST_CFLAGS) $< -o $@ $(GST_LIBS)

run: $(TARGET)
	./$(TARGET)

viewer: $(TARGET)
	./$(TARGET) --viewer

clean:
	rm -f $(TARGET) $(TARGET_OPT)
