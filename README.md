# finx

A native desktop and WebAssembly financial data visualization tool built on Dear ImGui and ImPlot. Load data from CSV files, HTTP APIs, or Yahoo Finance, combine streams with formula expressions, and explore them interactively in resizable plot windows.

## Features

- **Multiple data sources** — CSV upload, HTTP GET (JSON/CSV), Yahoo Finance tickers, and formula-derived streams
- **Interactive plots** — line, scatter, bar, and step chart types with dual Y-axes and time-scaled X-axis
- **Formula engine** — arithmetic expressions with scalar and rolling window functions over any loaded streams
- **Crosshair tooltip** — hover over any plot to see exact Y values for each series at the nearest data point
- **Data table view** — right-click any stream to inspect raw data in a scrollable table with virtual rendering
- **CSV export** — export any loaded or derived stream to a CSV file (native builds)
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

## Formula engine

Formula streams let you derive new data from any loaded stream. Open the **Formula Builder** from the Data Streams panel, bind field aliases to columns in a source stream, then write an expression using those aliases.

### Operators

| Operator | Description |
|----------|-------------|
| `+ - * /` | Add, subtract, multiply, divide |
| `%` | Modulo (remainder) |
| `( )` | Parentheses for grouping |

### Scalar functions

Evaluate independently per row. Arguments can be any expression.

| Function | Description |
|----------|-------------|
| `abs(x)` | Absolute value |
| `sqrt(x)` | Square root |
| `pow(x, n)` | x raised to the power n |
| `log(x)` | Natural logarithm (ln) |
| `exp(x)` | e raised to the power x |
| `sin(x)` | Sine (radians) |
| `cos(x)` | Cosine (radians) |
| `tan(x)` | Tangent (radians) |

### Window functions

Operate over a rolling window of N rows. Syntax: `fn(alias, N)` — `alias` must be a bound field name and `N` must be a plain integer. Window functions produce `NaN` for the first N−1 warmup rows; these appear as natural gaps in the plot rather than spurious zeros.

| Function | Description | Notes |
|----------|-------------|-------|
| `sma(alias, N)` | Simple moving average over N periods | NaN for first N−1 rows |
| `ema(alias, N)` | Exponential moving average | Seeded with SMA; k = 2/(N+1) |
| `stddev(alias, N)` | Rolling population standard deviation | NaN for first N−1 rows |
| `rmin(alias, N)` | Rolling minimum over N periods | NaN for first N−1 rows |
| `rmax(alias, N)` | Rolling maximum over N periods | NaN for first N−1 rows |
| `roc(alias, N)` | Rate of change vs N periods ago | (v[i]−v[i−N]) / v[i−N] × 100 |

### Example expressions

| Expression | What it computes |
|------------|-----------------|
| `(a / b) * 100` | a as a percentage of b |
| `close - sma(close, 20)` | Distance from 20-day moving average |
| `(close - sma(close, 20)) / stddev(close, 20)` | Z-score vs 20-day window |
| `ema(close, 12) - ema(close, 26)` | MACD line |
| `close / sma(close, 200) - 1` | % above/below 200-day moving average |
| `roc(close, 1)` | Daily return % |
| `rmax(high, 52) - rmin(low, 52)` | 52-period true range |
