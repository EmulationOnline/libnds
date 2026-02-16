# Makefile for PicoDS - melonDS corelib interface
# Builds libds.so with melonDS statically linked
# External toolchains can set CC, CXX, LD, CFLAGS, CXXFLAGS, LDFLAGS

# Compiler defaults (overridden by environment / toolchain)
CXX ?= clang++
CC ?= clang
LD ?= lld

default: build/libds.so
	cp build/libds.so libds.so
	cp libds.so libapu.so

# Linker selection: set LD to override (e.g. LD=lld for clang LTO)
# Converted to -fuse-ld= flag for the compiler driver
LDSELECT = $(if $(LD),-fuse-ld=$(LD),)

# Our compile flags (appended to any toolchain CXXFLAGS)
LOCAL_CXXFLAGS = -std=c++20 -Wall -Wextra -O2 -DNDEBUG -fPIC -D__STDC_CONSTANT_MACROS

# Directories
BUILDDIR = build
CBUILD = cbuild

# melonDS paths (built via CMake in cbuild/)
MELONDS_SRC = $(CBUILD)/_deps/melonds-src/src
MELONDS_BUILD = $(CBUILD)/_deps/melonds-build/src

# Include directories
INCLUDES = \
	-I. \
	-isystem $(MELONDS_SRC)

# Static libraries produced by cmake
MELONDS_LIBS = \
	$(MELONDS_BUILD)/libcore.a \
	$(MELONDS_BUILD)/teakra/src/libteakra.a

# Our sources
CORELIB_SOURCES = libds.c
MAIN_SOURCES = main.cpp

# Build objects
CORELIB_OBJECTS = $(patsubst %.c,$(BUILDDIR)/%.o,$(CORELIB_SOURCES))
MAIN_OBJECTS = $(patsubst %.cpp,$(BUILDDIR)/%.o,$(MAIN_SOURCES))

# Output targets
CORELIB_TARGET = libds.so
CORELIB_PATH = $(BUILDDIR)/$(CORELIB_TARGET)
MAIN_TARGET = ds_emulator
MAIN_PATH = $(BUILDDIR)/$(MAIN_TARGET)

# ======================================================================
.PHONY: all clean main cmake help

all: $(CORELIB_PATH)

main: $(MAIN_PATH)

# --- Drive cmake to build melonDS core libs ---
cmake: $(MELONDS_LIBS)

$(MELONDS_LIBS):
	cmake -B $(CBUILD) \
		-DCMAKE_C_COMPILER="$(CC)" \
		-DCMAKE_CXX_COMPILER="$(CXX)" \
		-DCMAKE_C_FLAGS="$(CFLAGS)" \
		-DCMAKE_CXX_FLAGS="$(CXXFLAGS)" \
		-DCMAKE_EXE_LINKER_FLAGS="$(LDSELECT) $(LDFLAGS)" \
		-DCMAKE_SHARED_LINKER_FLAGS="$(LDSELECT) $(LDFLAGS)" \
		-DCMAKE_MODULE_LINKER_FLAGS="$(LDSELECT) $(LDFLAGS)" \
		-DCMAKE_BUILD_TYPE=Release \
		-DENABLE_JIT=OFF \
		-DENABLE_OPENGL=OFF
	cmake --build $(CBUILD) --target core teakra

# --- Build directory ---
$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

# --- Compile corelib interface (C++ despite .c extension) ---
$(BUILDDIR)/%.o: %.c $(MELONDS_LIBS) | $(BUILDDIR)
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CXX) $(LOCAL_CXXFLAGS) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# --- Compile main ---
$(BUILDDIR)/%.o: %.cpp $(MELONDS_LIBS) | $(BUILDDIR)
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CXX) $(LOCAL_CXXFLAGS) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# --- Link corelib library ---
$(CORELIB_PATH): $(CORELIB_OBJECTS)
	@echo "Linking $(CORELIB_TARGET)..."
	@mkdir -p $(dir $(CORELIB_PATH))
	$(CXX) -shared $(LDSELECT) -o $@ $(CORELIB_OBJECTS) \
		$(MELONDS_LIBS) \
		$(LDFLAGS) -lm -ldl
# $(LDFLAGS) -lm -lpthread -ldl

# --- Link main executable ---
$(MAIN_PATH): $(MAIN_OBJECTS) $(CORELIB_PATH)
	@echo "Linking $(MAIN_TARGET)..."
	@mkdir -p $(dir $(MAIN_PATH))
	$(CXX) $(LDSELECT) -o $@ $(MAIN_OBJECTS) -L$(BUILDDIR) -lds $(LDFLAGS) -lm -lpthread -ldl `sdl2-config --cflags --libs`

# --- Clean ---
clean:
	@echo "Cleaning..."
	rm -rf $(BUILDDIR) $(CBUILD) *.so

# --- Help ---
help:
	@echo "Available targets:"
	@echo "  all       - Build libds.so (default)"
	@echo "  main      - Build ds_emulator executable"
	@echo "  cmake     - Build melonDS core libs only"
	@echo "  clean     - Remove build + cbuild directories"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Toolchain variables (set by environment):"
	@echo "  CC, CXX, LD, CFLAGS, CXXFLAGS, LDFLAGS"
