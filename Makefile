MAKEFLAGS += -j8

APP     := finx
SRC_DIR := src

IMGUI_DIR  := lib/imgui
IMPLOT_DIR := lib/implot
LIB_DIR    := lib

IMGUI_SRCS := \
    $(IMGUI_DIR)/imgui.cpp \
    $(IMGUI_DIR)/imgui_draw.cpp \
    $(IMGUI_DIR)/imgui_tables.cpp \
    $(IMGUI_DIR)/imgui_widgets.cpp \
    $(IMGUI_DIR)/backends/imgui_impl_sdl2.cpp \
    $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp

IMPLOT_SRCS := \
    $(IMPLOT_DIR)/implot.cpp \
    $(IMPLOT_DIR)/implot_items.cpp

APP_SRCS := $(shell find $(SRC_DIR) -name '*.cpp')

ALL_SRCS := $(IMGUI_SRCS) $(IMPLOT_SRCS) $(APP_SRCS)

INCLUDES := -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -I$(IMPLOT_DIR) -I$(LIB_DIR) -I$(SRC_DIR)

COMMON_CXXFLAGS := -std=c++17 -O2 -Wall -Wno-unused-function $(INCLUDES)

# ---------- native target ----------
NATIVE_BUILD_DIR := build
NATIVE_OBJ_DIR   := $(NATIVE_BUILD_DIR)/obj
NATIVE_BIN       := $(NATIVE_BUILD_DIR)/$(APP)

UNAME_S := $(shell uname -s)

NATIVE_CXX      := g++
NATIVE_CXXFLAGS := $(COMMON_CXXFLAGS)
NATIVE_LIBS     :=

ifeq ($(UNAME_S),Linux)
    NATIVE_LIBS     += -lGL -ldl -lpthread -lcurl `sdl2-config --libs`
    NATIVE_CXXFLAGS += `sdl2-config --cflags`
endif
ifeq ($(UNAME_S),Darwin)
    NATIVE_LIBS     += -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo \
                       `sdl2-config --libs` -lcurl
    NATIVE_CXXFLAGS += `sdl2-config --cflags` -I/usr/local/include -I/opt/local/include \
                       -DGL_SILENCE_DEPRECATION
endif
ifeq ($(OS),Windows_NT)
    NATIVE_BIN      := $(NATIVE_BUILD_DIR)/$(APP).exe
    NATIVE_LIBS     += -lgdi32 -lopengl32 -limm32 -lcurl `pkg-config --static --libs sdl2`
    NATIVE_CXXFLAGS += `pkg-config --cflags sdl2`
endif

NATIVE_OBJS := $(patsubst %.cpp,$(NATIVE_OBJ_DIR)/%.o,$(ALL_SRCS))

# ---------- web target (emscripten) ----------
WEB_DIR     := docs
WEB_OBJ_DIR := build/web-obj
WEB_OUT     := $(WEB_DIR)/index.html
WEB_CXX     := em++
WEB_EMS     := -s USE_SDL=2 -DIMGUI_IMPL_OPENGL_ES3

WEB_CXXFLAGS := $(COMMON_CXXFLAGS) -Os $(WEB_EMS) -fno-exceptions \
    -DIMGUI_DISABLE_DEMO_WINDOWS

WEB_LDFLAGS := \
    -s WASM=1 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s STACK_SIZE=1048576 \
    -s NO_EXIT_RUNTIME=0 \
    -s USE_WEBGL2=1 \
    -s MIN_WEBGL_VERSION=2 \
    -s FULL_ES3=1 \
    -s FETCH=1 \
    -s ASSERTIONS=1 \
    -s ERROR_ON_UNDEFINED_SYMBOLS=0 \
    -s EXPORTED_RUNTIME_METHODS='["ccall","UTF8ToString","stringToUTF8","lengthBytesUTF8"]' \
    -s EXPORTED_FUNCTIONS='["_main","_finx_csv_loaded"]' \
    $(WEB_EMS) \
    --preload-file assets \
    --shell-file shell.html

WEB_OBJS := $(patsubst %.cpp,$(WEB_OBJ_DIR)/%.o,$(ALL_SRCS))

# ---------- top-level rules ----------
.PHONY: all native web run rerun serve clean clean-native clean-web deps help

all: native

native: $(NATIVE_BIN)

web: $(WEB_OUT)

run: native
	./$(NATIVE_BIN)

rerun: clean-native run

serve: web
	@echo "Serving $(WEB_DIR) at http://localhost:8000"
	cd $(WEB_DIR) && python3 -m http.server

# Per-cpp build rules (native)
$(NATIVE_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(NATIVE_CXX) $(NATIVE_CXXFLAGS) -c -o $@ $<

$(NATIVE_BIN): $(NATIVE_OBJS)
	@mkdir -p $(dir $@)
	$(NATIVE_CXX) $(NATIVE_CXXFLAGS) -o $@ $^ $(NATIVE_LIBS)
	@echo "============ Native build complete: $@ ============"

# Per-cpp build rules (web)
$(WEB_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(WEB_CXX) $(WEB_CXXFLAGS) -c -o $@ $<

$(WEB_OUT): $(WEB_OBJS)
	@mkdir -p $(dir $@)
	$(WEB_CXX) -o $@ $(WEB_OBJS) $(WEB_LDFLAGS)
	@echo "============ Web build complete: $@ ============"

clean: clean-native clean-web

clean-native:
	rm -rf $(NATIVE_BUILD_DIR)

clean-web:
	rm -rf $(WEB_OBJ_DIR) $(WEB_DIR)/index.html $(WEB_DIR)/index.js $(WEB_DIR)/index.wasm

deps:
	@mkdir -p lib
	@if [ ! -f lib/json.hpp ]; then \
	    echo "Downloading nlohmann/json..."; \
	    curl -sL "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" -o lib/json.hpp; \
	fi

help:
	@echo "Targets:"
	@echo "  make            native build (default)"
	@echo "  make run        native build + run"
	@echo "  make web        emscripten build"
	@echo "  make serve      emscripten build + http server"
	@echo "  make clean      clean both"
	@echo "  make deps       download library dependencies"
