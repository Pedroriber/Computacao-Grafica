# Makefile for the WebGPU triangle example
# Windows + MSYS2 UCRT64 + MinGW/GCC

WGPU_VERSION := v29.0.1.1
WGPU_ASSET := wgpu-windows-x86_64-gnu-release.zip

DEPS_DIR := deps
WGPU_DIR := $(DEPS_DIR)/wgpu
WGPU_STAMP := $(WGPU_DIR)/.stamp

CC := gcc
CXX := g++

GLFW_CFLAGS := $(shell pkg-config --cflags glfw3)
GLFW_LIBS := $(shell pkg-config --libs glfw3)

CFLAGS := -std=c11 -Wall -Wextra -I. -I$(WGPU_DIR)/include $(GLFW_CFLAGS)

CXXFLAGS := -std=c++20 -Wall -Wextra -I. -I$(WGPU_DIR)/include $(GLFW_CFLAGS)

# wgpu-native GNU/MinGW import library
LDFLAGS := $(WGPU_DIR)/lib/libwgpu_native.a $(GLFW_LIBS)

# Windows libraries required by GLFW and wgpu-native
LDFLAGS += -ld3d12 -ldxgi -ldxguid -luser32 -lgdi32 -lshell32 -lole32 -luuid -lws2_32 -lntdll -luserenv

OBJS := triangle.o glfw3webgpu.o

.PHONY: all run clean distclean deps

TARGET := $(strip $(filter-out run,$(MAKECMDGOALS)))
ifeq ($(TARGET),)
TARGET := triangle
endif

all: triangle

triangle: triangle.o glfw3webgpu.o
	$(CXX) triangle.o glfw3webgpu.o -o $@ $(LDFLAGS)

relogio: relogio.o glfw3webgpu.o
	$(CXX) relogio.o glfw3webgpu.o -o $@ $(LDFLAGS)

circulo: circulo.o glfw3webgpu.o
	$(CXX) circulo.o glfw3webgpu.o -o $@ $(LDFLAGS)

triangle.o: triangle.cpp $(WGPU_STAMP)
	$(CXX) $(CXXFLAGS) -c $< -o $@

relogio.o: relogio.cpp $(WGPU_STAMP)
	$(CXX) $(CXXFLAGS) -c $< -o $@

circulo.o: circulo.cpp $(WGPU_STAMP)
	$(CXX) $(CXXFLAGS) -c $< -o $@

glfw3webgpu.o: glfw3webgpu.c glfw3webgpu.h $(WGPU_STAMP)
	$(CC) $(CFLAGS) -D_GLFW_WIN32 -c $< -o $@

deps: $(WGPU_STAMP)

# Download and extract wgpu-native
$(WGPU_STAMP):
	mkdir -p $(WGPU_DIR)
	curl -L -o $(DEPS_DIR)/wgpu.zip https://github.com/gfx-rs/wgpu-native/releases/download/$(WGPU_VERSION)/$(WGPU_ASSET)
	unzip -o -q $(DEPS_DIR)/wgpu.zip -d $(WGPU_DIR)
	rm -f $(DEPS_DIR)/wgpu.zip
	touch $@

run: $(TARGET)
	./$(TARGET).exe

clean:
	rm -f triangle.exe relogio.exe circulo.exe triangle.o relogio.o circulo.o glfw3webgpu.o

distclean: clean
	rm -rf $(DEPS_DIR)