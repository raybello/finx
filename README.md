# finx

A native desktop and WebAssembly financial data visualization tool built on Dear ImGui and ImPlot. Load data from CSV files, HTTP APIs, or Yahoo Finance, combine streams with formula expressions, and explore them interactively in resizable plot windows.

## Features

- **Multiple data sources** — CSV upload, HTTP GET (JSON/CSV), Yahoo Finance tickers, and formula-derived streams
- **Interactive plots** — line, scatter, bar, and step chart types with dual Y-axes and time-scaled X-axis
- **Formula engine** — write arithmetic expressions over any loaded streams with cross-stream alignment
- **Dockable UI** — ImGui docking layout with persistent window positions via `imgui.ini`
- **Persistent config** — streams, plots, and field mappings saved to `finx.json` between sessions
- **Web build** — same codebase compiles to WebAssembly via Emscripten

## Prerequisites

**macOS**
```sh
brew install sdl2
# libcurl is provided by the system
```

**Linux**
```sh
sudo apt install build-essential libsdl2-dev libcurl4-openssl-dev
```

**Web (Emscripten)**
```sh
# Install and activate the Emscripten SDK — https://emscripten.org/docs/getting_started/downloads.html
source /path/to/emsdk/emsdk_env.sh
```

**Yahoo Finance (optional)**
```sh
pip install pybind11 yfinance
```
When `pybind11` is importable at build time the Makefile automatically enables the integration.

## Build

Download the single-header dependencies (only needed once):
```sh
make deps
```

**Native desktop app**
```sh
make          # build → build/finx
make run      # build + launch
```

**WebAssembly**
```sh
make web      # build → docs/index.{html,js,wasm}
make serve    # build + serve at http://localhost:8000
```

## Testing

```sh
make test             # unit tests (requires GoogleTest + libcurl)
make test-yfinance    # Yahoo Finance integration tests (requires pybind11 + yfinance)
```

## Keyboard shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+N` | Add new data stream |
| `Ctrl+P` | New plot window |
| `Ctrl+S` | Save config |
| `Ctrl+E` | Export PNG |

> On macOS `Ctrl` maps to `Cmd`.

## Data sources

| Source | Notes |
|--------|-------|
| **CSV** | Upload any delimiter-separated file; columns auto-typed |
| **HTTP GET** | JSON (dot-path extraction) or CSV response; configurable field map |
| **Yahoo Finance** | Ticker + period + interval; requires pybind11 integration |
| **Formula** | Arithmetic expression over bound stream fields (e.g. `(a / b) * 100`) |
