
# Makefile for the WebGPU scene graph example
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

CFLAGS := -std=c11 -Wall -Wextra -I. -I$(WGPU_DIR)/include/webgpu $(GLFW_CFLAGS)
CXXFLAGS := -std=c++20 -Wall -Wextra -I. -I$(WGPU_DIR)/include/webgpu $(GLFW_CFLAGS)

# wgpu-native GNU/MinGW import library
LDFLAGS := $(WGPU_DIR)/lib/libwgpu_native.a $(GLFW_LIBS)

# Windows libraries required by GLFW and wgpu-native
LDFLAGS += -ld3d12 -ldxgi -ldxguid -luser32 -lgdi32 -lshell32 -lole32 -luuid -lws2_32 -lntdll -luserenv

TARGET2 = t2
TARGET3 = t3
TARGET4 = solarsystem

all: $(TARGET2) $(TARGET3) $(TARGET4)

CORE_SRC = \
  wgpucontext.cpp \
  error.cpp \
  graphicsmath.cpp \
  wgslreflect.cpp \
  uniformbuffer.cpp \
  state.cpp \
  shader.cpp \
  pipeline.cpp \
  renderer.cpp \
  algorithm.cpp \
  node.cpp \
  scene.cpp \
  transform.cpp \
  camera2d.cpp \
  camera3d.cpp \
  arcball.cpp \
  light.cpp \
  pointlight.cpp \
  directionallight.cpp \
  material.cpp \
  colormaterial.cpp \
  phongmaterial.cpp \
  textureset.cpp \
  texture.cpp \
  texcube.cpp \
  texdepth.cpp \
  sampler.cpp \
  clipplane.cpp \
  grid.cpp \
  triangle.cpp \
  cube.cpp \
  quad.cpp \
  sphere.cpp \
  square.cpp \
  skybox.cpp \
  mesh.cpp \
  disk.cpp \
  orbitengine.cpp \

LUXOR_SRC = \
  luxor/linearinterpolator.cpp \
  luxor/cubicinterpolator.cpp \
  luxor/movement.cpp \
  luxor/animation.cpp \
  luxor/luxorengine.cpp \
  luxor/luxor.cpp \

CORE_OBJ = $(addsuffix .o, $(basename $(CORE_SRC))) glfw3webgpu.o
LUXOR_OBJ = $(addsuffix .o, $(basename $(LUXOR_SRC)))

.PRECIOUS: luxor/%.o

%.o: %.cpp $(WGPU_STAMP)
	$(CXX) $(CXXFLAGS) -c $< -o $@

glfw3webgpu.o: glfw3webgpu.c glfw3webgpu.h $(WGPU_STAMP)
	$(CC) $(CFLAGS) -D_GLFW_WIN32 -c $< -o $@

luxor/%.o: luxor/%.cpp $(WGPU_STAMP)
	$(CXX) $(CXXFLAGS) -c $< -o $@

main_2d.o: main_2d.cpp $(WGPU_STAMP)
	$(CXX) $(CXXFLAGS) -c $< -o $@

main_3d.o: main_3d.cpp $(WGPU_STAMP)
	$(CXX) $(CXXFLAGS) -c $< -o $@

solarsystem.o: solarsystem.cpp $(WGPU_STAMP)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET2): $(CORE_OBJ) main_2d.o Makefile
	$(CXX) -o $@ $(CORE_OBJ) main_2d.o $(LDFLAGS)

$(TARGET3): $(CORE_OBJ) main_3d.o Makefile
	$(CXX) -o $@ $(CORE_OBJ) main_3d.o $(LDFLAGS)

$(TARGET4): $(CORE_OBJ) solarsystem.o Makefile
	$(CXX) -o $@ $(CORE_OBJ) solarsystem.o $(LDFLAGS)

clean:
	rm -f $(TARGET2) $(TARGET3) $(TARGET4) $(CORE_OBJ) $(LUXOR_OBJ) main_2d.o main_3d.o solarsystem.o

deps: $(WGPU_STAMP)

# Download and extract wgpu-native
$(WGPU_STAMP):
	mkdir -p $(WGPU_DIR)
	curl -L -o $(DEPS_DIR)/wgpu.zip https://github.com/gfx-rs/wgpu-native/releases/download/$(WGPU_VERSION)/$(WGPU_ASSET)
	unzip -o -q $(DEPS_DIR)/wgpu.zip -d $(WGPU_DIR)
	rm -f $(DEPS_DIR)/wgpu.zip
	touch $@

distclean: clean
	rm -rf $(DEPS_DIR)

.PHONY: all clean distclean deps
