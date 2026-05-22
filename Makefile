NPROC     := $(shell nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
MAKEFLAGS += -j$(NPROC)

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
    PYBIND11_INC    := $(shell python3 -c "import pybind11; print(pybind11.get_include())" 2>/dev/null)
    PYTHON_INC      := $(shell python3-config --includes 2>/dev/null)
    PYTHON_LDFLAGS  := $(shell python3-config --ldflags --embed 2>/dev/null)
    ifneq ($(PYBIND11_INC),)
        NATIVE_CXXFLAGS += -DHAVE_PYBIND11 -I$(PYBIND11_INC) $(PYTHON_INC)
        NATIVE_LIBS     += $(PYTHON_LDFLAGS)
    endif
endif
ifeq ($(UNAME_S),Darwin)
    NATIVE_LIBS     += -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo \
                       `sdl2-config --libs` -lcurl
    NATIVE_CXXFLAGS += `sdl2-config --cflags` -I/usr/local/include -I/opt/local/include \
                       -DGL_SILENCE_DEPRECATION
    PYBIND11_INC    := $(shell python3 -c "import pybind11; print(pybind11.get_include())" 2>/dev/null)
    PYTHON_INC      := $(shell python3-config --includes 2>/dev/null)
    PYTHON_LDFLAGS  := $(shell python3-config --ldflags --embed 2>/dev/null)
    ifneq ($(PYBIND11_INC),)
        NATIVE_CXXFLAGS += -DHAVE_PYBIND11 -I$(PYBIND11_INC) $(PYTHON_INC)
        NATIVE_LIBS     += $(PYTHON_LDFLAGS)
    endif
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
.PHONY: all native web run rerun serve clean clean-native clean-web deps help test test-yfinance

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
	@echo "  make test       build and run unit tests (requires gtest + libcurl)"
	@echo "  make clean      clean both"
	@echo "  make deps       download library dependencies"

# ---------- test target ----------
TEST_DIR      := tests
TEST_BIN      := build/tests/finx_tests

# Exclude the pybind11 integration test from the main suite; it lives in its own binary.
YF_INTEG_TEST := $(TEST_DIR)/yfinance_integration_test.cpp
TEST_SRCS     := $(filter-out $(YF_INTEG_TEST),$(wildcard $(TEST_DIR)/*.cpp))

# Source files compiled into the test binary (no main.cpp, no UI, no persist)
TEST_APP_SRCS := \
    src/io/csv_parser.cpp \
    src/io/http_client.cpp \
    src/io/yfinance_client.cpp \
    src/expr/expr_lexer.cpp \
    src/expr/expr_parser.cpp \
    src/expr/expr_evaluator.cpp \
    src/data/stream_store.cpp

# Intentionally does NOT include -DHAVE_PYBIND11 so yfinance_client
# compiles to its no-op stubs — tests verify stub behaviour explicitly.
TEST_CXXFLAGS := -std=c++17 -O0 -g -Wall -Wno-unused-function $(INCLUDES)

GTEST_CFLAGS  := $(shell pkg-config --cflags gtest 2>/dev/null)
GTEST_LIBS    := $(shell pkg-config --libs   gtest_main 2>/dev/null)
ifeq ($(GTEST_LIBS),)
    GTEST_LIBS := -lgtest -lgtest_main -lpthread
endif

TEST_CXXFLAGS += $(GTEST_CFLAGS)
TEST_LDFLAGS  := -lcurl -lpthread $(GTEST_LIBS)

ifeq ($(UNAME_S),Darwin)
    TEST_LDFLAGS += -framework CoreFoundation
endif

$(TEST_BIN): $(TEST_SRCS) $(TEST_APP_SRCS)
	@mkdir -p $(dir $@)
	$(NATIVE_CXX) $(TEST_CXXFLAGS) -o $@ $^ $(TEST_LDFLAGS)

test: $(TEST_BIN)
	./$(TEST_BIN) --gtest_color=yes

# ---------- yfinance integration test (requires pybind11 + yfinance) ----------
YF_TEST_BIN   := build/tests/yfinance_integration_tests
YF_TEST_SRCS  := $(YF_INTEG_TEST) src/io/yfinance_client.cpp

ifneq ($(PYBIND11_INC),)

YF_TEST_CXXFLAGS := $(TEST_CXXFLAGS) -DHAVE_PYBIND11 -I$(PYBIND11_INC) $(PYTHON_INC)
# Uses a custom main() that registers the global environment, so link against
# gtest only (not gtest_main which provides its own main).
YF_GTEST_LIBS    := $(filter-out -lgtest_main,$(GTEST_LIBS)) -lgtest
YF_TEST_LDFLAGS  := -lpthread $(YF_GTEST_LIBS) $(PYTHON_LDFLAGS)
ifeq ($(UNAME_S),Darwin)
    YF_TEST_LDFLAGS += -framework CoreFoundation
endif

$(YF_TEST_BIN): $(YF_TEST_SRCS)
	@mkdir -p $(dir $@)
	$(NATIVE_CXX) $(YF_TEST_CXXFLAGS) -o $@ $^ $(YF_TEST_LDFLAGS)

test-yfinance: $(YF_TEST_BIN)
	./$(YF_TEST_BIN) --gtest_color=yes

else

test-yfinance:
	@echo "Skipping yfinance integration tests: pybind11 not found (install pybind11 Python package)"

endif
