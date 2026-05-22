CXX      := g++
EMCC     := em++

IMGUI_DIR   := lib/imgui
IMPLOT_DIR  := lib/implot
LIB_DIR     := lib

INCLUDES := -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -I$(IMPLOT_DIR) -I$(LIB_DIR) -Isrc

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

APP_SRCS := $(shell find src -name '*.cpp')

ALL_SRCS := $(IMGUI_SRCS) $(IMPLOT_SRCS) $(APP_SRCS)

# ── Native ──────────────────────────────────────────────────────────────
CXXFLAGS_NATIVE := -std=c++17 -O2 $(INCLUDES)
LIBS_NATIVE     := -lSDL2 -lGL -ldl -lpthread -lcurl

.PHONY: native run web serve clean deps

native: $(ALL_SRCS)
	$(CXX) $(CXXFLAGS_NATIVE) $(ALL_SRCS) $(LIBS_NATIVE) -o finx

run: native
	./finx

# ── Web (Emscripten) ────────────────────────────────────────────────────
CXXFLAGS_WEB := \
    -std=c++17 -O2 -fno-exceptions \
    $(INCLUDES) \
    -DIMGUI_DISABLE_DEMO_WINDOWS \
    -s USE_SDL=2 \
    -s USE_WEBGL2=1 \
    -s FULL_ES3=1 \
    -s FETCH=1 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s EXPORTED_RUNTIME_METHODS='["ccall","UTF8ToString","stringToUTF8","lengthBytesUTF8"]' \
    -s EXPORTED_FUNCTIONS='["_main","_finx_csv_loaded","_finx_http_result"]' \
    --shell-file shell.html

web:
	mkdir -p docs
	$(EMCC) $(CXXFLAGS_WEB) $(ALL_SRCS) -o docs/index.html

serve: web
	python3 -m http.server 8000 --directory docs

clean:
	rm -f finx docs/index.html docs/finx.js docs/finx.wasm

deps:
	@mkdir -p lib
	@if [ ! -f lib/json.hpp ]; then \
	    echo "Downloading nlohmann/json..."; \
	    curl -sL "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" -o lib/json.hpp; \
	fi
