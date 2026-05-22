# finx

**Web-based, WASM/C-accelerated graphing application.**  
Plot data from CSV uploads or live HTTP endpoints. Configure multi-series charts with per-field axis bindings — all in the browser, no server required.

---

## Table of Contents

1. [Product Overview](#1-product-overview)
2. [Product Requirements Document (PRD)](#2-product-requirements-document-prd)
   - [Goals & Non-Goals](#21-goals--non-goals)
   - [User Stories](#22-user-stories)
   - [Functional Requirements](#23-functional-requirements)
   - [Non-Functional Requirements](#24-non-functional-requirements)
3. [Architecture](#3-architecture)
   - [Tech Stack](#31-tech-stack)
   - [Data Model](#32-data-model)
   - [Module Breakdown](#33-module-breakdown)
4. [Implementation Plan](#4-implementation-plan)
   - [Phase 0 — Project Scaffold](#phase-0--project-scaffold)
   - [Phase 1 — CSV Ingestion & Basic Plot](#phase-1--csv-ingestion--basic-plot)
   - [Phase 2 — HTTP Data Sources](#phase-2--http-data-sources)
   - [Phase 3 — Plot Configuration UI](#phase-3--plot-configuration-ui)
   - [Phase 4 — Multi-Series & Multi-Plot Canvas](#phase-4--multi-series--multi-plot-canvas)
   - [Phase 5 — Polish & Web Deploy](#phase-5--polish--web-deploy)
5. [File Structure](#5-file-structure)
6. [Build Instructions](#6-build-instructions)
7. [HTTP Source Format](#7-http-source-format)
8. [CSV Format](#8-csv-format)

---

## 1. Product Overview

`finx` is a **single-page WebAssembly application** that lets a user:

- Load tabular data — either by uploading a CSV or by pointing the app at an HTTP endpoint (e.g. a stock market API).
- Define a **data stream**: a named source with a field schema mapping column names to data types.
- Configure one or more **plots** on a shared canvas, each binding any two fields as its X and Y axis.
- Layer multiple series on a single plot, zoom/pan interactively, and export the view as PNG.

The UI is rendered entirely via **Dear ImGui** and **ImPlot** compiled to WASM with Emscripten — the same architecture used in [CaRPG](https://raybello.github.io/CaRPG/). No runtime JavaScript framework is needed; the `.html` shell is a thin loader.

---

## 2. Product Requirements Document (PRD)

### 2.1 Goals & Non-Goals

| | |
|---|---|
| **Goal** | Run fully in the browser as a static WASM bundle (no backend). |
| **Goal** | Accept CSV files via a browser file-picker. |
| **Goal** | Fetch JSON data from user-specified HTTP endpoints (CORS-permissive APIs). |
| **Goal** | Let users define a field schema: which columns are X candidates vs. Y series. |
| **Goal** | Support multiple named plots on a single canvas with independent axis configs. |
| **Goal** | Support multiple overlaid series per plot (multi-line, multi-scatter). |
| **Goal** | Provide a native desktop build for development and testing. |
| **Non-goal** | Server-side storage or user accounts. |
| **Non-goal** | WebSocket / streaming real-time data (stretch goal only). |
| **Non-goal** | Authentication or API key management UI (user pastes the full URL with key). |

### 2.2 User Stories

**US-01 — CSV upload**  
> As a data analyst, I want to drag-and-drop a CSV file so that I can plot its columns without leaving the browser.

**US-02 — HTTP data source**  
> As a developer, I want to paste a REST endpoint URL, define the JSON path to a data array, and map fields to X/Y so that I can visualise API responses (e.g. stock OHLCV) instantly.

**US-03 — Field mapping**  
> As a user, I want to pick any column as the X axis and one or more columns as Y series so that I can compare multiple metrics on one chart.

**US-04 — Multi-plot canvas**  
> As a user, I want to open several plots side-by-side on the same canvas so that I can correlate data streams from different sources.

**US-05 — Interactive exploration**  
> As a user, I want to zoom, pan, and box-select regions of a plot so that I can drill into dense datasets.

**US-06 — Plot export**  
> As a user, I want to save the current plot view as a PNG so that I can include it in a report.

**US-07 — Persistent configuration**  
> As a user, I want my stream definitions and plot configs to survive a page reload via localStorage so that I don't have to reconfigure after refresh.

### 2.3 Functional Requirements

#### FR-1: Data Streams

| ID | Requirement |
|---|---|
| FR-1.1 | The app shall maintain a list of named **DataStream** objects. |
| FR-1.2 | A DataStream shall have a **source type**: `CSV_FILE` or `HTTP_GET`. |
| FR-1.3 | A DataStream shall expose a typed **field schema**: a list of `(name, type)` pairs where type is one of `NUMBER`, `TIMESTAMP`, `STRING`. |
| FR-1.4 | A DataStream shall hold the current data as a columnar store: a map from field name to `std::vector<double>` (numbers/timestamps) or `std::vector<std::string>` (strings). |
| FR-1.5 | The user shall be able to **refresh** an HTTP stream on demand. |
| FR-1.6 | The user shall be able to **rename or delete** a stream. |

#### FR-2: CSV Ingestion

| ID | Requirement |
|---|---|
| FR-2.1 | The app shall open a browser file picker when the user clicks "Add CSV". |
| FR-2.2 | The parser shall auto-detect the delimiter (comma, semicolon, tab). |
| FR-2.3 | The first row shall be treated as the header, populating the field schema. |
| FR-2.4 | Each column shall be heuristically typed: if every non-empty cell parses as a double it is `NUMBER`; if it parses as ISO-8601 it is `TIMESTAMP`; otherwise `STRING`. |
| FR-2.5 | Parsing shall handle quoted fields containing the delimiter. |

#### FR-3: HTTP Sources

| ID | Requirement |
|---|---|
| FR-3.1 | The user shall supply a URL template with optional `{param}` placeholders that are filled from a key-value table in the UI. |
| FR-3.2 | The app shall issue a GET request via `emscripten_fetch` (web) or libcurl (native). |
| FR-3.3 | The user shall supply a **JSON path** to locate the array of records in the response (e.g. `results`, `data.candles`). |
| FR-3.4 | The user shall define a **field map**: a list of `(output_field_name, json_key, type)` tuples to extract from each record. |
| FR-3.5 | Unrecognised or extra JSON keys shall be silently ignored. |
| FR-3.6 | HTTP errors and malformed JSON shall surface as dismissible error banners inside the stream panel. |

#### FR-4: Plot Configuration

| ID | Requirement |
|---|---|
| FR-4.1 | The user shall be able to create a named **Plot** and associate it with one DataStream. |
| FR-4.2 | Each Plot shall have exactly one **X-axis field** selector. |
| FR-4.3 | Each Plot shall support one or more **Y-axis series**, each bound to a field from the same stream. |
| FR-4.4 | Each series shall have a configurable **plot type**: Line, Scatter, Bar, StepLine. |
| FR-4.5 | Each series shall have a configurable colour (colour picker) and label. |
| FR-4.6 | The user shall be able to add a **second Y axis** (right-hand side) and bind series to it independently. |
| FR-4.7 | Axis labels, plot title, and legend visibility shall be configurable. |

#### FR-5: Canvas & Layout

| ID | Requirement |
|---|---|
| FR-5.1 | Plots shall be rendered as dockable **ImGui windows** using ImGui's docking API. |
| FR-5.2 | The user shall be able to resize, float, and re-dock plots freely. |
| FR-5.3 | A **Data Streams** panel shall list all streams with status indicators (OK / Loading / Error). |
| FR-5.4 | A **Plot Inspector** panel shall show the full configuration of the currently focused plot. |
| FR-5.5 | A toolbar / menu bar shall provide: New Stream, New Plot, Save Config, Load Config, Export PNG. |

#### FR-6: Persistence

| ID | Requirement |
|---|---|
| FR-6.1 | On save, the app shall serialise all stream definitions and plot configs to JSON and write to `localStorage` (web) or a `.finx` file (native). |
| FR-6.2 | On load / startup, the app shall restore the saved configuration. |
| FR-6.3 | CSV data is **not** persisted (too large); only the file path hint and schema are saved. |

### 2.4 Non-Functional Requirements

| ID | Requirement |
|---|---|
| NFR-1 | The WASM bundle (`finx.wasm` + `finx.js`) shall be ≤ 6 MB gzipped. |
| NFR-2 | Rendering at 60 fps for datasets up to 100 k data points per series. |
| NFR-3 | HTTP requests shall be fire-and-forget async; the UI shall remain responsive during fetches. |
| NFR-4 | The native build shall compile and run on Linux and macOS with `make`. |
| NFR-5 | The web build shall compile with `make web` and serve with `make serve`. |
| NFR-6 | C++ standard: C++17. No exceptions in WASM build (`-fno-exceptions`). |

---

## 3. Architecture

### 3.1 Tech Stack

| Layer | Technology |
|---|---|
| UI & Rendering | [Dear ImGui](https://github.com/ocornut/imgui) (docking branch) |
| Plotting | [ImPlot](https://github.com/epezent/implot) (git submodule at `lib/implot`) |
| Graphics backend (native) | OpenGL 3.3 + SDL2 |
| Graphics backend (web) | WebGL2 via Emscripten (`-s USE_SDL=2 -s USE_WEBGL2=1`) |
| HTTP (web) | `emscripten_fetch` API |
| HTTP (native) | libcurl |
| JSON parsing | [nlohmann/json](https://github.com/nlohmann/json) (single-header, `lib/json.hpp`) |
| CSV parsing | hand-rolled parser in `src/io/csv_parser.cpp` |
| Build system | GNU Make — unified `Makefile` with `native` and `web` targets |
| Web shell | Custom `shell.html` (adapted from Emscripten minimal shell) |

### 3.2 Data Model

```
DataStream
├── id          : uint32_t          (unique handle)
├── name        : std::string
├── source      : SourceConfig (variant: CsvSource | HttpSource)
├── schema      : std::vector<FieldDef>
│   └── FieldDef { name, type (NUMBER|TIMESTAMP|STRING) }
├── columns     : std::unordered_map<std::string, std::vector<double>>
├── str_columns : std::unordered_map<std::string, std::vector<std::string>>
├── status      : StreamStatus (IDLE|LOADING|OK|ERROR)
└── error_msg   : std::string

CsvSource
└── raw_text    : std::string       (contents of uploaded file)

HttpSource
├── url_template : std::string      (e.g. "https://api.example.com/v1/bars?ticker={ticker}&limit={limit}")
├── params       : std::map<std::string, std::string>
├── json_path    : std::string      (dot-separated, e.g. "results")
└── field_map    : std::vector<FieldMapEntry>
    └── FieldMapEntry { output_name, json_key, type }

PlotSeries
├── stream_id   : uint32_t
├── x_field     : std::string
├── y_field     : std::string
├── y_axis      : int (0=left, 1=right)
├── plot_type   : PlotType (LINE|SCATTER|BAR|STEP)
├── color       : ImVec4
└── label       : std::string

Plot
├── id          : uint32_t
├── name        : std::string
├── series      : std::vector<PlotSeries>
├── x_label     : std::string
├── y_label     : std::string
├── y2_label    : std::string
├── title_vis   : bool
└── legend_vis  : bool

AppState
├── streams     : std::vector<DataStream>
├── plots       : std::vector<Plot>
└── next_id     : uint32_t
```

### 3.3 Module Breakdown

```
src/
├── main.cpp            Entry point: SDL2 init, ImGui/ImPlot context, main loop.
├── app.cpp / .h        AppState owner; top-level render() dispatches to panels.
├── io/
│   ├── csv_parser.cpp  Parse raw text → columnar store, auto-detect delimiter/types.
│   └── http_client.cpp Thin wrapper: emscripten_fetch (web) / libcurl (native).
├── ui/
│   ├── menu_bar.cpp    File menu, New Stream, New Plot, Save/Load, Export PNG.
│   ├── stream_panel.cpp  Left panel — list of streams, add/edit/delete, status badges.
│   ├── plot_window.cpp   Dockable ImGui window wrapping one ImPlot canvas.
│   ├── plot_inspector.cpp  Right panel — series list, axis config, type/colour pickers.
│   └── modals.cpp      "Add HTTP Source" modal, "Add CSV" trigger, error dialog.
├── data/
│   ├── stream_store.cpp  CRUD for DataStream; triggers fetch/parse on create/refresh.
│   └── plot_store.cpp    CRUD for Plot; resolves series data pointers for ImPlot.
└── persist/
    └── config.cpp      Serialise/deserialise AppState to JSON; localStorage bridge.
```

---

## 4. Implementation Plan

### Phase 0 — Project Scaffold

**Goal**: repo compiles to both native and web with an empty ImGui window.

| Step | Task | Files |
|---|---|---|
| 0.1 | Add ImPlot as a git submodule at `lib/implot` | `.gitmodules` |
| 0.2 | Vendor `lib/imgui` (docking branch) as a submodule | `.gitmodules` |
| 0.3 | Vendor `lib/json.hpp` (nlohmann single-header) | `lib/json.hpp` |
| 0.4 | Write `Makefile` with `native` and `web` targets | `Makefile` |
| 0.5 | Write `shell.html` (Emscripten minimal shell, canvas fullscreen) | `shell.html` |
| 0.6 | Write `src/main.cpp`: SDL2 init, ImGui+ImPlot context, empty loop | `src/main.cpp` |
| 0.7 | Confirm `make` and `make web` both produce runnable output | — |

**Makefile targets:**

```makefile
# Native
CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Ilib/imgui -Ilib/imgui/backends -Ilib/implot -Ilib/
SRCS     = $(wildcard src/**/*.cpp src/*.cpp) \
           lib/imgui/imgui.cpp lib/imgui/imgui_draw.cpp \
           lib/imgui/imgui_tables.cpp lib/imgui/imgui_widgets.cpp \
           lib/imgui/backends/imgui_impl_sdl2.cpp \
           lib/imgui/backends/imgui_impl_opengl3.cpp \
           lib/implot/implot.cpp lib/implot/implot_items.cpp
LIBS     = -lSDL2 -lGL -ldl

# Web (Emscripten)
EMCC     = em++
EMFLAGS  = -std=c++17 -O2 -fno-exceptions \
           -s USE_SDL=2 -s USE_WEBGL2=1 -s FULL_ES3=1 \
           -s FETCH=1 \
           -s ALLOW_MEMORY_GROWTH=1 \
           -s EXPORTED_RUNTIME_METHODS=['ccall'] \
           --shell-file shell.html \
           -o docs/index.html
```

---

### Phase 1 — CSV Ingestion & Basic Plot

**Goal**: user can upload a CSV, see its columns, and render a line plot.

| Step | Task | Files |
|---|---|---|
| 1.1 | Implement `CsvParser::parse(text)` → `ParsedTable {headers, columns}` | `src/io/csv_parser.cpp` |
| 1.2 | Add heuristic type detection (number / ISO timestamp / string) | `src/io/csv_parser.cpp` |
| 1.3 | Wire Emscripten file-picker: `EM_ASM` JS snippet invokes `<input type=file>`, reads as text, passes pointer to C++ | `src/ui/modals.cpp` |
| 1.4 | Native fallback: `ImGuiFileDialog` or hard-coded path arg | `src/ui/modals.cpp` |
| 1.5 | `StreamStore::add_csv(name, raw_text)` parses and populates `DataStream` | `src/data/stream_store.cpp` |
| 1.6 | Render `StreamPanel` — list streams, show field names and row count | `src/ui/stream_panel.cpp` |
| 1.7 | Implement `PlotWindow::render(plot)` — call `ImPlot::BeginPlot` / `PlotLine` / `EndPlot` | `src/ui/plot_window.cpp` |
| 1.8 | Hard-code one series (col 0 = X, col 1 = Y) to prove end-to-end flow | `src/ui/plot_window.cpp` |

**CSV parser algorithm (pseudo-code):**

```
split text into lines
header = parse_row(lines[0])
for each line[1..]:
    row = parse_row(line)      # respects quoted fields
    for each col i:
        raw_columns[i].push_back(row[i])
for each col i:
    if all cells parse as double  → NUMBER column
    elif all cells parse as ISO8601 → TIMESTAMP (store as unix epoch double)
    else                          → STRING column
```

---

### Phase 2 — HTTP Data Sources

**Goal**: user can define an HTTP source, fetch JSON, and plot extracted fields.

| Step | Task | Files |
|---|---|---|
| 2.1 | Design `HttpSource` struct and `FieldMapEntry` (json_key → output name) | `src/data/stream_store.h` |
| 2.2 | `HttpClient::get_async(url, callback)` — Emscripten fetch path | `src/io/http_client.cpp` |
| 2.3 | `HttpClient::get_async(url, callback)` — libcurl path (native, same interface) | `src/io/http_client.cpp` |
| 2.4 | `JsonExtractor::extract(json_text, json_path, field_map)` → `ParsedTable` | `src/io/http_client.cpp` |
| 2.5 | `StreamStore::add_http(config)` and `::refresh(id)` | `src/data/stream_store.cpp` |
| 2.6 | "Add HTTP Source" modal: URL field, params table, json_path field, field map table | `src/ui/modals.cpp` |
| 2.7 | Status badge in `StreamPanel`: spinner while loading, green dot OK, red dot error | `src/ui/stream_panel.cpp` |
| 2.8 | Example: Polygon.io aggregates endpoint for AAPL OHLCV | docs/examples/ |

**JSON extraction algorithm:**

```
parse json_text with nlohmann::json
traverse dot-separated json_path to locate the records array
for each record in array:
    for each FieldMapEntry(output, key, type):
        value = record[key]
        if type == NUMBER:   columns[output].push_back(value.get<double>())
        if type == TIMESTAMP: columns[output].push_back(parse_epoch(value))
        if type == STRING:   str_cols[output].push_back(value.get<string>())
```

---

### Phase 3 — Plot Configuration UI

**Goal**: full axis/series configuration via the Plot Inspector panel.

| Step | Task | Files |
|---|---|---|
| 3.1 | `PlotInspector` panel: series list with add/remove buttons | `src/ui/plot_inspector.cpp` |
| 3.2 | Per-series dropdowns: stream selector, x_field, y_field (filtered to NUMBER/TIMESTAMP) | `src/ui/plot_inspector.cpp` |
| 3.3 | Per-series type selector: Line / Scatter / Bar / Step | `src/ui/plot_inspector.cpp` |
| 3.4 | Per-series colour picker (`ImGui::ColorEdit4`) and label input | `src/ui/plot_inspector.cpp` |
| 3.5 | Second Y-axis toggle: bind series to Y2 and configure `ImPlot::SetupAxisLinks` | `src/ui/plot_inspector.cpp` |
| 3.6 | Axis label inputs, title toggle, legend toggle | `src/ui/plot_inspector.cpp` |
| 3.7 | `PlotWindow::render` dispatches series by type: `PlotLine` / `PlotScatter` / `PlotBars` / `PlotStairs` | `src/ui/plot_window.cpp` |
| 3.8 | Timestamp X-axis: use `ImPlotAxisFlags_Time` and `ImPlot::SetupAxisScale` | `src/ui/plot_window.cpp` |

---

### Phase 4 — Multi-Series & Multi-Plot Canvas

**Goal**: multiple plots in a docked layout; each independently configured.

| Step | Task | Files |
|---|---|---|
| 4.1 | Enable ImGui docking: `ImGuiConfigFlags_DockingEnable` in `main.cpp` | `src/main.cpp` |
| 4.2 | `PlotStore::add_plot(name)` → new `Plot` with empty series list | `src/data/plot_store.cpp` |
| 4.3 | Each `PlotWindow` rendered as a separate `ImGui::Begin / End` window — dockable | `src/ui/plot_window.cpp` |
| 4.4 | "New Plot" button in menu bar; "Close" button in each plot's title bar | `src/ui/menu_bar.cpp` |
| 4.5 | Click on plot window → sets `AppState::focused_plot_id` → `PlotInspector` shows its config | `src/app.cpp` |
| 4.6 | Cross-stream overlay: a single Plot can hold series from different streams | `src/data/plot_store.cpp` |
| 4.7 | Validate that X-axis field lengths match across overlaid series; show warning if not | `src/ui/plot_window.cpp` |

---

### Phase 5 — Polish & Web Deploy

**Goal**: persistence, PNG export, bundle size optimisation, deployed to GitHub Pages.

| Step | Task | Files |
|---|---|---|
| 5.1 | `Config::save()` — serialise `AppState` to JSON string, write to `localStorage` via `EM_ASM` (web) or `.finx` file (native) | `src/persist/config.cpp` |
| 5.2 | `Config::load()` — deserialise on startup; re-fetch HTTP streams automatically | `src/persist/config.cpp` |
| 5.3 | "Save / Load" menu items; auto-save on close via Emscripten `beforeunload` hook | `src/ui/menu_bar.cpp` |
| 5.4 | PNG export: `ImPlot::ShowPlotContextMenu` already includes save; wire up custom save path that uses `stb_image_write` to memory then `EM_ASM` `URL.createObjectURL` download | `src/ui/plot_window.cpp` |
| 5.5 | Link-time optimisation: add `-flto` and `-Os` to web build | `Makefile` |
| 5.6 | Strip ImGui demo / debug code from release build (`IMGUI_DISABLE_DEMO_WINDOWS`) | `Makefile` |
| 5.7 | `make serve` — `python3 -m http.server 8000 --directory docs` | `Makefile` |
| 5.8 | GitHub Actions CI: build web target on push → deploy `docs/` to GitHub Pages | `.github/workflows/deploy.yml` |

---

## 5. File Structure

```
finx/
├── Makefile
├── shell.html                    # Emscripten HTML shell (fullscreen canvas)
├── README.md
├── .gitmodules
│
├── lib/
│   ├── imgui/                    # submodule — Dear ImGui (docking branch)
│   ├── implot/                   # submodule — ImPlot
│   └── json.hpp                  # nlohmann/json single-header
│
├── src/
│   ├── main.cpp                  # SDL2 init, contexts, main loop
│   ├── app.h / app.cpp           # AppState, top-level render()
│   │
│   ├── io/
│   │   ├── csv_parser.h / .cpp   # CSV → columnar store
│   │   └── http_client.h / .cpp  # async GET, JSON extraction
│   │
│   ├── data/
│   │   ├── stream_store.h / .cpp # DataStream CRUD + parse/fetch lifecycle
│   │   └── plot_store.h / .cpp   # Plot CRUD
│   │
│   ├── ui/
│   │   ├── menu_bar.h / .cpp
│   │   ├── stream_panel.h / .cpp
│   │   ├── plot_window.h / .cpp
│   │   ├── plot_inspector.h / .cpp
│   │   └── modals.h / .cpp
│   │
│   └── persist/
│       └── config.h / .cpp       # JSON serialise/deserialise, localStorage bridge
│
└── docs/                         # Web build output (GitHub Pages root)
    ├── index.html
    ├── finx.js
    └── finx.wasm
```

---

## 6. Build Instructions

### Prerequisites

**Native:**
```bash
# Ubuntu/Debian
sudo apt install build-essential libsdl2-dev libcurl4-openssl-dev
```

**Web:**
```bash
# Install Emscripten SDK
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source emsdk_env.sh
```

### Clone with submodules

```bash
git clone --recurse-submodules https://github.com/raybello/finx.git
cd finx
```

### Build commands

```bash
make           # native binary → ./finx
make run       # build + run native
make web       # WASM bundle → docs/
make serve     # build web + serve at http://localhost:8000
make clean     # remove build artefacts
```

---

## 7. HTTP Source Format

When adding an HTTP data source, the user provides:

| Field | Example | Description |
|---|---|---|
| **URL template** | `https://api.polygon.io/v2/aggs/ticker/{ticker}/range/1/day/{from}/{to}?apiKey={key}` | URL with `{placeholder}` tokens |
| **Params** | `ticker=AAPL`, `from=2024-01-01`, `to=2024-12-31`, `key=YOUR_KEY` | Filled into the template before the GET request |
| **JSON path** | `results` | Dot-separated path to the array of records in the response |
| **Field map** | `time → t (TIMESTAMP)`, `open → o (NUMBER)`, `close → c (NUMBER)`, `volume → v (NUMBER)` | `output_name → json_key (type)` for each field to extract |

**Example response fragment (Polygon.io):**
```json
{
  "results": [
    { "t": 1704067200000, "o": 185.12, "c": 184.50, "v": 52341200 },
    { "t": 1704153600000, "o": 184.20, "c": 186.01, "v": 48120000 }
  ]
}
```

With the field map above, this produces a stream with columns: `time`, `open`, `close`, `volume`.  
Set X axis = `time`, add series: Y = `close` (Line), Y = `open` (Scatter) on the same plot.

---

## 8. CSV Format

- First row: header (column names).
- Delimiter: auto-detected (`,` `;` `\t`).
- Quoted fields: `"value with, comma"` supported.
- Type detection: per-column heuristic on first 100 rows.
- Timestamp formats recognised: Unix epoch (integer ms or s), `YYYY-MM-DD`, `YYYY-MM-DDTHH:MM:SSZ`.

**Example:**
```csv
date,open,high,low,close,volume
2024-01-01,185.12,187.00,184.30,184.50,52341200
2024-01-02,184.20,186.90,183.80,186.01,48120000
```

---

*finx is intentionally minimal — one Makefile, no CMake, no npm, no runtime JS framework. The entire app is C++ compiled to WASM.*
