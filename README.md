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
5. [Feature: Derived Data Streams](#5-feature-derived-data-streams)
   - [PRD](#51-prd)
   - [Data Model Changes](#52-data-model-changes)
   - [Expression Language](#53-expression-language)
   - [GUI Design](#54-gui-design)
   - [Implementation Plan — Phase 6](#55-implementation-plan--phase-6)
6. [File Structure](#6-file-structure)
7. [Build Instructions](#7-build-instructions)
8. [HTTP Source Format](#8-http-source-format)
9. [CSV Format](#9-csv-format)

---

## 1. Product Overview

`finx` is a **single-page WebAssembly application** that lets a user:

- Load tabular data — either by uploading a CSV or by pointing the app at an HTTP endpoint (e.g. a stock market API).
- Define a **data stream**: a named source with a field schema mapping column names to data types.
- Configure one or more **plots** on a shared canvas, each binding any two fields as its X and Y axis.
- Layer multiple series on a single plot, zoom/pan interactively, and export the view as PNG.
- **Define derived streams** using formula expressions that reference fields from existing streams, enabling computed metrics like per-capita rates, ratios, moving differences, or trigonometric transforms.

The UI is rendered entirely via **Dear ImGui** and **ImPlot** compiled to WASM with Emscripten. No runtime JavaScript framework is needed; the `.html` shell is a thin loader.

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
| **Goal** | Allow users to define derived streams via formula expressions over existing stream fields. |
| **Goal** | Provide a native desktop build for development and testing. |
| **Non-goal** | Server-side storage or user accounts. |
| **Non-goal** | WebSocket / streaming real-time data. |
| **Non-goal** | Authentication or API key management UI (user pastes the full URL with key). |

### 2.2 User Stories

**US-01 — CSV upload**
> As a data analyst, I want to drag-and-drop a CSV file so that I can plot its columns without leaving the browser.

**US-02 — HTTP data source**
> As a developer, I want to paste a REST endpoint URL, define the JSON path to a data array, and map fields to X/Y so that I can visualise API responses instantly.

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

**US-08 — Derived streams (formula)**
> As an analyst, I want to define a new data stream whose values are computed from an expression over other streams' fields (e.g. `births_per_capita = (births / total_pop) * 100`) so that I can plot derived metrics without pre-processing the source data externally.

### 2.3 Functional Requirements

#### FR-1: Data Streams

| ID | Requirement |
|---|---|
| FR-1.1 | The app shall maintain a list of named **DataStream** objects. |
| FR-1.2 | A DataStream shall have a **source type**: `CSV_FILE`, `HTTP_GET`, or `FORMULA`. |
| FR-1.3 | A DataStream shall expose a typed **field schema**: a list of `(name, type)` pairs. |
| FR-1.4 | A DataStream shall hold the current data as a columnar store. |
| FR-1.5 | The user shall be able to **refresh** an HTTP stream on demand. |
| FR-1.6 | The user shall be able to **rename or delete** a stream. |

#### FR-2: CSV Ingestion

| ID | Requirement |
|---|---|
| FR-2.1 | The app shall open a browser file picker when the user clicks "Add CSV". |
| FR-2.2 | The parser shall auto-detect the delimiter (comma, semicolon, tab). |
| FR-2.3 | The first row shall be treated as the header, populating the field schema. |
| FR-2.4 | Each column shall be heuristically typed: NUMBER, TIMESTAMP, or STRING. |
| FR-2.5 | Parsing shall handle quoted fields containing the delimiter. |

#### FR-3: HTTP Sources

| ID | Requirement |
|---|---|
| FR-3.1 | The user shall supply a URL template with optional `{param}` placeholders. |
| FR-3.2 | The app shall issue a GET request via `emscripten_fetch` (web) or libcurl (native). |
| FR-3.3 | The user shall supply a **JSON path** to locate the array of records in the response. |
| FR-3.4 | The user shall define a **field map**: `(output_field_name, json_key, type)` tuples. |
| FR-3.5 | Unrecognised or extra JSON keys shall be silently ignored. |
| FR-3.6 | HTTP errors shall surface as dismissible error banners in the stream panel. |

#### FR-4: Plot Configuration

| ID | Requirement |
|---|---|
| FR-4.1 | The user shall be able to create a named **Plot** and associate it with streams. |
| FR-4.2 | Each Plot shall have exactly one **X-axis field** selector per series. |
| FR-4.3 | Each Plot shall support one or more **Y-axis series**, each bound to a field. |
| FR-4.4 | Each series shall have a configurable **plot type**: Line, Scatter, Bar, StepLine. |
| FR-4.5 | Each series shall have a configurable colour and label. |
| FR-4.6 | The user shall be able to add a **second Y axis** and bind series to it. |
| FR-4.7 | Axis labels, plot title, and legend visibility shall be configurable. |

#### FR-5: Canvas & Layout

| ID | Requirement |
|---|---|
| FR-5.1 | Plots shall be rendered as dockable ImGui windows. |
| FR-5.2 | A **Data Streams** panel shall list all streams with status indicators. |
| FR-5.3 | A **Plot Inspector** panel shall show the full configuration of the focused plot. |
| FR-5.4 | A menu bar shall provide: New Stream, New Plot, Save Config, Load Config, Export PNG. |

#### FR-6: Persistence

| ID | Requirement |
|---|---|
| FR-6.1 | On save, the app shall serialise all stream definitions and plot configs to JSON. |
| FR-6.2 | On load, the app shall restore the saved configuration. |
| FR-6.3 | CSV data is not persisted (too large); only the file path hint and schema are saved. |
| FR-6.4 | Formula stream definitions (expression text + variable bindings) shall be persisted. |

### 2.4 Non-Functional Requirements

| ID | Requirement |
|---|---|
| NFR-1 | The WASM bundle shall be ≤ 6 MB gzipped. |
| NFR-2 | Rendering at 60 fps for datasets up to 100 k data points per series. |
| NFR-3 | HTTP requests shall be async; the UI shall remain responsive during fetches. |
| NFR-4 | The native build shall compile and run on Linux and macOS with `make`. |
| NFR-5 | C++ standard: C++17. No exceptions in WASM build (`-fno-exceptions`). |
| NFR-6 | Formula re-evaluation shall complete within one frame (≤16 ms) for datasets ≤ 1M rows. |

---

## 3. Architecture

### 3.1 Tech Stack

| Layer | Technology |
|---|---|
| UI & Rendering | [Dear ImGui](https://github.com/ocornut/imgui) (docking branch) |
| Plotting | [ImPlot](https://github.com/epezent/implot) |
| Graphics backend (native) | OpenGL 3.3 + SDL2 |
| Graphics backend (web) | WebGL2 via Emscripten |
| HTTP (web) | `emscripten_fetch` API |
| HTTP (native) | libcurl |
| JSON parsing | nlohmann/json (single-header) |
| CSV parsing | hand-rolled parser in `src/io/csv_parser.cpp` |
| Expression evaluation | custom recursive-descent parser in `src/expr/` |
| Build system | GNU Make |

### 3.2 Data Model

```
DataStream
├── id          : uint32_t
├── name        : std::string
├── source_type : SourceType (CSV_FILE | HTTP_GET | FORMULA)
├── csv_source  : CsvSource
├── http_source : HttpSource
├── formula_source : FormulaSource         ← NEW
├── schema      : std::vector<FieldDef>
├── columns     : unordered_map<string, vector<double>>
├── str_columns : unordered_map<string, vector<string>>
├── status      : StreamStatus
└── error_msg   : std::string

FormulaSource                              ← NEW
├── expression  : std::string              ("(births / total_pop) * 100")
├── result_name : std::string              ("births_per_capita")
├── x_stream_id : uint32_t                 (stream supplying x-axis values)
├── x_field     : std::string              (field name in that stream)
└── bindings    : vector<FormulaBinding>
    └── FormulaBinding
        ├── alias      : std::string        (identifier used in the expression)
        ├── stream_id  : uint32_t
        └── field_name : std::string

PlotSeries, Plot — unchanged
AppState — unchanged (formula streams stored in stream_store alongside others)
```

### 3.3 Module Breakdown

```
src/
├── main.cpp
├── app.cpp / .h
├── io/
│   ├── csv_parser.cpp
│   └── http_client.cpp
├── expr/                          ← NEW
│   ├── expr_lexer.h / .cpp        tokenise expression string into tokens
│   ├── expr_parser.h / .cpp       recursive-descent → ExprNode AST
│   └── expr_evaluator.h / .cpp    eval(node, vars) → double; formula stream builder
├── ui/
│   ├── menu_bar.cpp
│   ├── stream_panel.cpp
│   ├── plot_window.cpp
│   ├── plot_inspector.cpp
│   ├── modals.cpp
│   └── formula_builder.h / .cpp   ← NEW  formula editor modal
├── data/
│   ├── stream_store.cpp           extended: add_formula(), evaluate_formula()
│   └── plot_store.cpp
└── persist/
    └── config.cpp                 extended: serialise/deserialise FormulaSource
```

---

## 4. Implementation Plan

### Phase 0 — Project Scaffold ✓

Repo compiles to both native and web with an empty ImGui window.

### Phase 1 — CSV Ingestion & Basic Plot ✓

User can upload a CSV, see its columns, and render a line plot.

### Phase 2 — HTTP Data Sources ✓

User can define an HTTP source, fetch JSON, and plot extracted fields.

### Phase 3 — Plot Configuration UI ✓

Full axis/series configuration via the Plot Inspector panel.

### Phase 4 — Multi-Series & Multi-Plot Canvas ✓

Multiple plots in a docked layout; each independently configured.

### Phase 5 — Polish & Web Deploy ✓

Persistence, PNG export, bundle size optimisation, deployed to GitHub Pages.

---

## 5. Feature: Derived Data Streams

### 5.1 PRD

#### Overview

Users often need to compute a new metric from one or more loaded data streams before they can plot it — for example:

- `births_per_capita = (births / total_population) * 100`
- `death_rate = deaths / total_population`
- `net_growth = births - deaths`
- `growth_rate = (total_population - lag_population) / lag_population`

Today this requires pre-processing the CSV externally. The **Derived Streams** feature lets users define these transformations directly in the app, using a GUI expression builder. The result is a new data stream that can be plotted just like any other.

#### Goals

| | |
|---|---|
| **Goal** | Let users define a new stream whose values are computed by an arithmetic formula over fields from any loaded stream. |
| **Goal** | Provide a button-based GUI to build the expression (no keyboard-only freeform typing required, though typing is supported). |
| **Goal** | Support operators `+`, `-`, `*`, `/`, `%` and functions `sqrt`, `cos`, `sin`, `tan`, `abs`, `log`, `exp`. |
| **Goal** | Provide a field picker to insert stream field references into the expression as named aliases. |
| **Goal** | Evaluate the formula at each x-axis point; only evaluate where data exists in all referenced streams. |
| **Goal** | The resulting stream is first-class: it can be plotted, inspected, and used as input to further derived streams. |
| **Goal** | Allow the user to re-open and edit the formula after creation; the stream recalculates immediately. |
| **Goal** | Persist formula definitions (expression + bindings) across save/load. |
| **Non-goal** | String operations or boolean logic (numeric expressions only in v1). |
| **Non-goal** | Multi-output formulas (one formula → one output column). |
| **Non-goal** | Auto-refresh of derived streams when the upstream source refreshes (manual recalculate button is sufficient for v1). |

#### User Stories

**US-F01 — Create a derived stream**
> As an analyst, I want to click "Add Derived Stream", type a name, build an expression using field references and operators, and have the result appear as a new plottable stream immediately.

**US-F02 — Use the expression builder GUI**
> As a user, I want to click operator buttons `(`, `)`, `+`, `-`, `*`, `/`, `%`, `sqrt(`, `cos(`, `sin(`, `tan(` to assemble my expression without having to remember syntax.

**US-F03 — Insert field references**
> As a user, I want to select a stream and a field from dropdowns and click "Insert" to add that field as a named variable into my expression, with an alias I can customise.

**US-F04 — Cross-stream expressions**
> As an analyst working with two CSV files that share a common time index, I want to combine fields from both in one expression and have the app align the data by x-axis value so that only rows present in all streams contribute to the output.

**US-F05 — Edit an existing formula**
> As a user, I want to right-click a derived stream and choose "Edit Formula" to re-open the builder, modify the expression, and see the plot update instantly.

**US-F06 — Derived stream appears in plot inspector**
> As a user, I want to add a derived stream as a Y-series in any plot exactly as I would a raw CSV stream, without any special steps.

#### Functional Requirements

| ID | Requirement |
|---|---|
| FR-F1 | The app shall support a third source type `FORMULA` for `DataStream`. |
| FR-F2 | A formula stream shall store: expression string, result field name, x-axis binding (stream + field), and a list of variable bindings `(alias, stream_id, field_name)`. |
| FR-F3 | The formula builder modal shall display an editable text input showing the current expression. |
| FR-F4 | The builder shall display operator buttons: `(`, `)`, `+`, `-`, `*`, `/`, `%`, and function buttons `sqrt(`, `cos(`, `sin(`, `tan(`, `abs(`, `log(`, `exp(`. Each click appends the token to the expression. |
| FR-F5 | The builder shall display a **Backspace** button (removes the last character) and a **Clear** button (clears the entire expression). |
| FR-F6 | The builder shall display a field picker: a stream dropdown and a field dropdown (showing NUMBER fields only). Clicking "Insert" appends the alias to the expression and records the binding. |
| FR-F7 | The alias shall default to the field name with non-identifier characters replaced by underscores. The user shall be able to rename the alias in the binding list before inserting. |
| FR-F8 | The builder shall list current bindings with a delete button per binding. |
| FR-F9 | The builder shall validate the expression on Create/Update: parse it and report any syntax errors inline (no modal dismiss on error). |
| FR-F10 | On successful create/update, the app shall evaluate the formula across the source data and populate the derived stream's columns. |
| FR-F11 | The derived stream's schema shall contain exactly two fields: the x-field (type inherited from source) and one NUMBER field named `result_name`. |
| FR-F12 | The evaluation shall use **x-value intersection**: only x-axis values present in all referenced streams' x-columns shall produce an output row. For single-stream formulas, all rows up to the minimum column length are used. |
| FR-F13 | The derived stream shall appear in the stream panel with a `[EXPR]` type tag and a status indicator. |
| FR-F14 | The stream panel's context menu for a formula stream shall include "Edit Formula" in addition to "Delete". |
| FR-F15 | Formula streams shall be serialised as `source_type: "formula"` in the config JSON, storing expression + bindings. On load, formula streams shall be re-evaluated against any available upstream data. |
| FR-F16 | A derived stream may reference another derived stream as an input (chained formulas), evaluated in dependency order. |

#### Non-Functional Requirements

| ID | Requirement |
|---|---|
| NFR-F1 | The expression parser shall handle expressions up to 1024 characters without stack overflow. |
| NFR-F2 | Evaluation of a formula over 1M rows shall complete in under 100 ms on a modern CPU. |
| NFR-F3 | Parse errors shall be reported with a column offset so the user can locate the problem in the expression string. |
| NFR-F4 | The formula builder modal shall be fully usable without a keyboard (button-only operation). |

---

### 5.2 Data Model Changes

#### New enum value

```cpp
// src/data/types.h
enum class SourceType { CSV_FILE, HTTP_GET, FORMULA };   // add FORMULA
```

#### New structs

```cpp
// src/data/types.h

struct FormulaBinding {
    std::string alias;       // identifier used in expression, e.g. "births"
    uint32_t    stream_id;   // source stream
    std::string field_name;  // source field within that stream
};

struct FormulaSource {
    std::string expression;       // arithmetic expression string, e.g. "(births / total_pop) * 100"
    std::string result_name;      // name of the output column, e.g. "births_per_capita"
    uint32_t    x_stream_id = 0;  // stream that supplies x-axis values
    std::string x_field;          // field within x_stream_id to use as x-axis
    std::vector<FormulaBinding> bindings;
};
```

#### Extended DataStream

```cpp
// src/data/types.h — add field to DataStream
struct DataStream {
    // ... existing fields ...
    FormulaSource formula_source;   // populated when source_type == FORMULA
};
```

#### StreamStore additions

```cpp
// src/data/stream_store.h
uint32_t add_formula(const std::string& name, const FormulaSource& src);
bool     evaluate_formula(uint32_t id);   // returns false if parse/eval fails
void     reevaluate_dependents(uint32_t changed_stream_id); // recalc downstream formulas
```

---

### 5.3 Expression Language

#### Grammar (EBNF)

```
expr        := add_expr
add_expr    := mul_expr ( ('+' | '-') mul_expr )*
mul_expr    := unary_expr ( ('*' | '/' | '%') unary_expr )*
unary_expr  := ('-' | '+') unary_expr | primary
primary     := NUMBER_LITERAL
             | IDENTIFIER
             | IDENTIFIER '(' expr_list ')'
             | '(' expr ')'
expr_list   := expr ( ',' expr )*
```

#### Supported literals

- Integer: `42`, `0`, `100`
- Floating-point: `3.14`, `1.5e-3`, `.25`

#### Supported identifiers / functions

| Token | Arity | Semantics |
|---|---|---|
| `sqrt` | 1 | `std::sqrt(x)` |
| `cos` | 1 | `std::cos(x)` |
| `sin` | 1 | `std::sin(x)` |
| `tan` | 1 | `std::tan(x)` |
| `abs` | 1 | `std::fabs(x)` |
| `log` | 1 | `std::log(x)` (natural log) |
| `exp` | 1 | `std::exp(x)` |
| `pow` | 2 | `std::pow(x, y)` |
| any alias | 0 | variable bound via `FormulaBinding` |

#### Lexer tokens

```cpp
// src/expr/expr_lexer.h
enum class TokenType {
    NUMBER, IDENT, PLUS, MINUS, STAR, SLASH, PERCENT,
    LPAREN, RPAREN, COMMA, END, ERROR
};

struct Token {
    TokenType   type;
    double      number = 0.0;   // populated for NUMBER
    std::string text;            // populated for IDENT and ERROR
    int         col  = 0;        // source column (for error reporting)
};
```

#### AST node

```cpp
// src/expr/expr_parser.h
enum class ExprNodeType { LITERAL, VARIABLE, UNARY, BINARY, CALL };

struct ExprNode {
    ExprNodeType              type;
    double                    literal  = 0.0;  // LITERAL
    std::string               name;             // VARIABLE name, BINARY/UNARY op, CALL func
    std::vector<ExprNode>     children;         // BINARY: [lhs, rhs]; UNARY: [operand]; CALL: [args...]
};

struct ParseResult {
    ExprNode    root;
    bool        ok   = false;
    std::string error;
    int         col  = 0;
};

ParseResult parse_expr(const std::string& text);
```

#### Evaluator

```cpp
// src/expr/expr_evaluator.h
using VarMap = std::unordered_map<std::string, double>;

double eval_node(const ExprNode& node, const VarMap& vars);
// Returns NaN on unknown variable or domain error; never throws.
```

#### Formula stream evaluation algorithm

```
function evaluate_formula(stream: DataStream) -> bool:
    src = stream.formula_source
    result = parse_expr(src.expression)
    if not result.ok: record error, return false

    // Resolve x-axis column
    x_stream = stream_store.find(src.x_stream_id)
    if not x_stream: record error, return false
    x_col = x_stream.columns[src.x_field]   // vector<double>

    // Resolve each binding's column
    binding_cols: map<alias, vector<double>>
    for each b in src.bindings:
        bs = stream_store.find(b.stream_id)
        if not bs: record error, return false
        binding_cols[b.alias] = bs.columns[b.field_name]

    // Determine intersection length
    // For single-stream: min of all column sizes
    // For cross-stream: build x-value index per stream, intersect
    if all bindings from same stream as x:
        n = min(x_col.size(), min over binding_cols of col.size())
        x_out = x_col[0..n-1]
        result_out = []
        for i in 0..n-1:
            vars = { alias: col[i] for alias, col in binding_cols }
            v = eval_node(result.root, vars)
            result_out.push_back(v)
    else:
        // cross-stream: align by x-value
        // build map: x_value -> index for each referenced stream
        x_index_maps: map<stream_id, map<double, size_t>>
        for each binding b: build x_index_maps[b.stream_id]
        x_out = [], result_out = []
        for each x_val in x_col:
            vars = {}
            ok = true
            for each b in src.bindings:
                idx_map = x_index_maps[b.stream_id]
                if x_val not in idx_map: ok = false; break
                vars[b.alias] = binding_cols[b.alias][idx_map[x_val]]
            if ok:
                v = eval_node(result.root, vars)
                x_out.push_back(x_val)
                result_out.push_back(v)

    // Populate stream columns
    stream.columns[src.x_field]      = x_out
    stream.columns[src.result_name]  = result_out
    stream.schema = [
        FieldDef{ src.x_field,     (type from x_stream schema) },
        FieldDef{ src.result_name, FieldType::NUMBER }
    ]
    stream.row_count = x_out.size()
    stream.status    = StreamStatus::OK
    return true
```

---

### 5.4 GUI Design

#### Formula Builder Modal

```
┌──────────────────────────────────────────────────────────────────┐
│  Formula Stream Builder                                    [×]   │
├──────────────────────────────────────────────────────────────────┤
│  Stream Name:  [ births_per_capita                          ]    │
│  Result Field: [ births_per_capita                          ]    │
│                                                                  │
│  Expression:                                                     │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ ( births / total_pop ) * 100                               │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
│  Operators:                                                      │
│  [ ( ]  [ ) ]  [ + ]  [ - ]  [ * ]  [ / ]  [ % ]               │
│                                                                  │
│  Functions:                                                      │
│  [ sqrt( ]  [ cos( ]  [ sin( ]  [ tan( ]                        │
│  [ abs(  ]  [ log( ]  [ exp( ]                                   │
│                                                                  │
│  [ ⌫ Backspace ]                        [ ✕ Clear ]             │
│                                                                  │
│  ──────────────────────────────────────────────────────────      │
│  Insert Field                                                    │
│  Stream: [ Population Data    ▼ ]  Field: [ births(count) ▼ ]   │
│  Alias:  [ births               ]                                │
│                                                 [ Insert Field ] │
│                                                                  │
│  ──────────────────────────────────────────────────────────      │
│  X Axis                                                          │
│  Stream: [ Population Data    ▼ ]  Field: [ month         ▼ ]   │
│                                                                  │
│  ──────────────────────────────────────────────────────────      │
│  Variable Bindings                                               │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ births     → Population Data : births(count)          [✕] │  │
│  │ total_pop  → Population Data : total population(count)[✕] │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ⚠ (error message shown here if expression is invalid)          │
│                                                                  │
│  [ Cancel ]                          [ Create Stream / Update ] │
└──────────────────────────────────────────────────────────────────┘
```

#### Interaction rules

- Every operator and function button **appends** to the expression string. Function buttons append `sqrt(` etc. so the user just needs to type/button the argument and `)`.
- **Backspace** removes one character from the end of the expression.
- **Clear** sets the expression to `""`.
- The expression input field is also directly editable via keyboard.
- **Insert Field**: clicking "Insert Field" does two things simultaneously — appends the alias to the expression at cursor position (or end), and records the binding in the list. If the alias already exists in the bindings list, only the expression token is inserted (no duplicate binding).
- **Alias auto-generation**: When a field is selected in the picker, the alias field pre-fills with the field name with spaces/special chars replaced by underscores and truncated to 32 chars. Duplicate aliases get a numeric suffix.
- **Create / Update** button: parses the expression. If valid, evaluates and closes the modal. If invalid, shows an inline error message (column offset) and keeps the modal open.
- The modal opens in **Create** mode when triggered from "+ Add Stream" → "Derived Stream (Formula)". It opens in **Edit** mode when "Edit Formula" is chosen from the stream panel context menu; it pre-fills all fields from the existing formula.

#### Stream Panel changes

- Each formula stream displays `[EXPR]` as its type tag (distinct from `[CSV]` and `[HTTP]`).
- The tree node expansion shows: expression preview (first 60 chars), row count, and binding list.
- Context menu gains "Edit Formula" item (only for FORMULA streams).

---

### 5.5 Implementation Plan — Phase 6

#### Step 6.1 — Extend `types.h`

**Files:** `src/data/types.h`

- Add `FORMULA` to `SourceType`.
- Add `FormulaBinding` struct.
- Add `FormulaSource` struct.
- Add `formula_source` field to `DataStream`.

No logic changes; pure data structure addition. Verify native build still compiles.

---

#### Step 6.2 — Implement expression lexer

**Files:** `src/expr/expr_lexer.h`, `src/expr/expr_lexer.cpp`

Implement a hand-rolled single-pass lexer. Input: `std::string_view`. Output: `std::vector<Token>`.

Rules:
- Skip whitespace.
- On digit or `.`: consume a floating-point literal (`strtod`).
- On letter or `_`: consume an identifier (letters, digits, underscores).
- On `+`, `-`, `*`, `/`, `%`, `(`, `)`, `,`: emit the corresponding token.
- On unknown character: emit `ERROR` token with the character in `text`.
- Emit `END` at string end.
- Track column offset (character index from string start) in every token.

Write a small inline unit-test block guarded by `#ifdef EXPR_LEXER_TEST` for isolated testing via `g++ -DEXPR_LEXER_TEST src/expr/expr_lexer.cpp`.

---

#### Step 6.3 — Implement expression parser

**Files:** `src/expr/expr_parser.h`, `src/expr/expr_parser.cpp`

Recursive descent parser consuming the token stream from Step 6.2. Returns a `ParseResult` containing an `ExprNode` AST or an error message + column offset.

Grammar matches [§5.3](#53-expression-language).

Key implementation notes:
- The parser holds a `size_t pos_` cursor into the token vector.
- Known function names (`sqrt`, `cos`, `sin`, `tan`, `abs`, `log`, `exp`, `pow`) are recognised during `IDENTIFIER` handling in `primary()`.
- Unknown identifiers become `VARIABLE` nodes; the evaluator resolves them at runtime.
- On parse error: record `error` and `col`, return `ok = false`. Do not throw.

---

#### Step 6.4 — Implement expression evaluator

**Files:** `src/expr/expr_evaluator.h`, `src/expr/expr_evaluator.cpp`

```cpp
double eval_node(const ExprNode& node, const VarMap& vars);
```

Walk the AST recursively. For `BINARY` nodes dispatch on `node.name` (`+`, `-`, `*`, `/`, `%`). Division by zero returns `NaN`. For `CALL` nodes dispatch on function name using `<cmath>`. For `VARIABLE` look up in `vars`; if not found return `NaN`. Never throw.

---

#### Step 6.5 — Add formula evaluation to `StreamStore`

**Files:** `src/data/stream_store.h`, `src/data/stream_store.cpp`

Add three methods:

```cpp
uint32_t add_formula(const std::string& name, const FormulaSource& src);
bool     evaluate_formula(uint32_t id);
void     reevaluate_dependents(uint32_t upstream_id);
```

`add_formula`:
1. Allocate a new `DataStream` with `source_type = FORMULA`.
2. Store `src` in `formula_source`.
3. Call `evaluate_formula(id)` immediately.
4. Return the new id.

`evaluate_formula`:
1. Find the stream; confirm it is a `FORMULA` stream.
2. Run the algorithm from [§5.3](#53-expression-language).
3. On error: set `status = ERROR_STATE`, `error_msg = parse/eval error`, return `false`.
4. On success: populate `columns`, `schema`, `row_count`, `status = OK`, return `true`.

`reevaluate_dependents`:
- Scan all `FORMULA` streams; for any whose `bindings` reference `upstream_id`, call `evaluate_formula`. Recurse for chained formulas using topological order (simple iterative pass; abort if a cycle is detected after N passes where N = number of formula streams).

---

#### Step 6.6 — Implement Formula Builder modal

**Files:** `src/ui/formula_builder.h`, `src/ui/formula_builder.cpp`

Public API:

```cpp
void formula_builder_open(App& app, uint32_t edit_stream_id = 0);
// edit_stream_id == 0 → Create mode; non-zero → Edit mode
void formula_builder_render(App& app);
// call every frame; the modal is ImGui-managed
```

Internal state (file-scope `static struct FormulaBuilderState`):
- `bool open`
- `uint32_t edit_id` (0 = create)
- `char name_buf[128]`
- `char result_buf[128]`
- `char expr_buf[512]`
- `char alias_buf[64]`
- `int picker_stream_idx`, `int picker_field_idx`
- `int x_stream_idx`, `int x_field_idx`
- `std::vector<FormulaBinding> bindings`
- `std::string error_msg`

Render layout (see [§5.4](#54-gui-design)):
1. Name + Result field inputs.
2. Expression `InputText` (full width, `CTRL+A` selects all).
3. Operator button grid (7 buttons per row, each calls `strncat(expr_buf, token, ...)` ).
4. Function button row.
5. Backspace + Clear buttons.
6. Field picker section: stream combo, field combo (NUMBER fields only), alias input, Insert button.
7. X-Axis section: stream combo, field combo (NUMBER + TIMESTAMP fields).
8. Binding list with per-row `[✕]` delete buttons.
9. Error message (red text, only when non-empty).
10. Cancel / Create/Update buttons.

On **Insert Field**:
- If alias already in bindings, just append alias to expr.
- Otherwise, append alias to expr and push new `FormulaBinding` to bindings list.
- Alias uniqueness: if alias exists in bindings with a different stream/field, append `_2`, `_3`, etc.

On **Create/Update**:
1. Parse `expr_buf` via `parse_expr`.
2. If `!result.ok`: set `error_msg = result.error + " (col " + col + ")"`, return.
3. If all ok: build `FormulaSource` from state.
4. If create: call `app.stream_store.add_formula(name, src)`.
5. If edit: update `stream->formula_source = src`; call `app.stream_store.evaluate_formula(edit_id)`.
6. Call `ImGui::CloseCurrentPopup()`.

---

#### Step 6.7 — Integrate formula builder into stream panel

**Files:** `src/ui/stream_panel.cpp`, `src/ui/modals.h`, `src/ui/modals.cpp`

Changes to `stream_panel.cpp`:
- Change the type tag for `FORMULA` streams to `[EXPR]`.
- Add "Edit Formula" to the context menu for `FORMULA` streams: calls `formula_builder_open(app, ds.id)`.
- In the tree-node expansion for a `FORMULA` stream, show:
  - Expression preview (first 60 chars of `formula_source.expression`).
  - Row count.
  - Binding summary: `alias → stream_name.field_name` per binding.

Changes to `modals.cpp` / `modals.h`:
- Add `modals_request_add_formula()` (sets a flag picked up in the Add Stream modal flow).
- In the "Add Stream" choice dialog, add a third option "Derived Stream (Formula)" that calls `formula_builder_open(app, 0)`.

Call `formula_builder_render(app)` from `app.cpp`'s `render()` every frame (alongside other modals).

---

#### Step 6.8 — Ensure formula streams appear in Plot Inspector

**Files:** `src/ui/plot_inspector.cpp`

Formula streams already populate `columns` and `schema` just like CSV/HTTP streams, so the Plot Inspector's stream combo and field dropdowns automatically include them. No structural change needed.

The only addition: when building `numeric_fields` for the y-field dropdown, include fields from formula streams (they are always `NUMBER`). This already works because the schema `FieldType::NUMBER` check is stream-type-agnostic.

Verify by creating a formula stream and confirming it appears in the stream selector of the Plot Inspector.

---

#### Step 6.9 — Extend persistence layer

**Files:** `src/persist/config.cpp`

In `serialise_app`:

```cpp
if (ds.source_type == SourceType::FORMULA) {
    json f;
    f["expression"]  = ds.formula_source.expression;
    f["result_name"] = ds.formula_source.result_name;
    f["x_stream_id"] = ds.formula_source.x_stream_id;
    f["x_field"]     = ds.formula_source.x_field;
    json jbindings = json::array();
    for (const auto& b : ds.formula_source.bindings) {
        jbindings.push_back({
            {"alias",      b.alias},
            {"stream_id",  b.stream_id},
            {"field_name", b.field_name}
        });
    }
    f["bindings"] = jbindings;
    s["formula"]  = f;
}
```

In `deserialise_app`:
- After all non-formula streams are loaded and id-remapped, process formula streams in a second pass.
- Remap `x_stream_id` and each `binding.stream_id` through `id_remap`.
- Call `app.stream_store.add_formula(name, src)` (this evaluates immediately).

**Important**: serialise formula streams after CSV/HTTP streams in the JSON array so that during deserialisation the upstream streams exist before formula evaluation is attempted.

---

#### Step 6.10 — Update Makefile

**Files:** `Makefile`

The Makefile uses `find $(SRC_DIR) -name '*.cpp'` for `APP_SRCS`, so new files in `src/expr/` and new `src/ui/formula_builder.cpp` are automatically picked up. No manual changes needed.

Verify with `make clean && make` — all new `.cpp` files should appear in the compilation output.

---

#### Step 6.11 — End-to-end test scenario

After all steps above, perform the following manual validation:

1. **Load** `assets/sample.csv` (population data with columns: `month`, `total_population`, `men`, `women`, `births`, `deaths`).
2. **Open** formula builder via "+ Add Stream" → "Derived Stream (Formula)".
3. **Name** the stream `birth_rate`. Set result field to `birth_rate`.
4. **Set X axis** to stream = "sample", field = "month".
5. **Insert field** `births` (alias: `births`) from stream "sample".
6. **Insert field** `total_pop` (alias: `total_pop`) from field `total_population`.
7. **Build expression** `( births / total_pop ) * 100` using buttons.
8. **Click Create**. The new stream appears in the stream panel as `[EXPR] birth_rate`.
9. **Add a new plot** and configure a series: stream = `birth_rate`, X = `month`, Y = `birth_rate`.
10. **Verify** the plot renders correctly with meaningful values.
11. **Edit the formula** via context menu: change expression to `births / total_pop * 1000` (per-mille). Verify plot updates.
12. **Save config** and reload. Verify formula stream is restored and plots correctly.

---

## 6. File Structure

```
finx/
├── Makefile
├── shell.html
├── README.md
├── .gitmodules
│
├── lib/
│   ├── imgui/
│   ├── implot/
│   └── json.hpp
│
├── src/
│   ├── main.cpp
│   ├── app.h / app.cpp
│   │
│   ├── io/
│   │   ├── csv_parser.h / .cpp
│   │   └── http_client.h / .cpp
│   │
│   ├── expr/                          ← NEW (Phase 6)
│   │   ├── expr_lexer.h / .cpp
│   │   ├── expr_parser.h / .cpp
│   │   └── expr_evaluator.h / .cpp
│   │
│   ├── data/
│   │   ├── stream_store.h / .cpp
│   │   └── plot_store.h / .cpp
│   │
│   ├── ui/
│   │   ├── menu_bar.h / .cpp
│   │   ├── stream_panel.h / .cpp
│   │   ├── plot_window.h / .cpp
│   │   ├── plot_inspector.h / .cpp
│   │   ├── modals.h / .cpp
│   │   └── formula_builder.h / .cpp   ← NEW (Phase 6)
│   │
│   └── persist/
│       └── config.h / .cpp
│
└── docs/
    ├── index.html
    ├── index.js
    └── index.wasm
```

---

## 7. Build Instructions

### Prerequisites

**Native:**
```bash
# Ubuntu/Debian
sudo apt install build-essential libsdl2-dev libcurl4-openssl-dev

# macOS (Homebrew)
brew install sdl2
```

**Web:**
```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source emsdk_env.sh
```

### Clone with submodules

```bash
git clone --recurse-submodules https://github.com/raybello/finx.git
cd finx
make deps   # download nlohmann/json
```

### Build commands

```bash
make           # native binary → build/finx
make run       # build + run native
make web       # WASM bundle → docs/
make serve     # build web + serve at http://localhost:8000
make clean     # remove build artefacts
```

---

## 8. HTTP Source Format

When adding an HTTP data source, the user provides:

| Field | Example | Description |
|---|---|---|
| **URL template** | `https://api.polygon.io/v2/aggs/ticker/{ticker}/...` | URL with `{placeholder}` tokens |
| **Params** | `ticker=AAPL`, `from=2024-01-01` | Filled into the template |
| **JSON path** | `results` | Dot-separated path to the records array |
| **Field map** | `time → t (TIMESTAMP)`, `close → c (NUMBER)` | `output_name → json_key (type)` |

---

## 9. CSV Format

- First row: header (column names).
- Delimiter: auto-detected (`,` `;` `\t`).
- Quoted fields: `"value with, comma"` supported.
- Type detection: per-column heuristic on first 100 rows.
- Timestamp formats recognised: Unix epoch ms/s, `YYYY-MM-DD`, `YYYY-MM-DDTHH:MM:SSZ`.

**Example:**
```csv
month,total_population,men,women,births,deaths
1,52341200,26100000,26241200,85000,41000
2,52385200,26122000,26263200,82000,39000
```

---

*finx is intentionally minimal — one Makefile, no CMake, no npm, no runtime JS framework.*
