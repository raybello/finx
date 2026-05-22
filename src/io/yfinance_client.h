#pragma once
#include <string>
#include <functional>
#include <cstdint>
#include "data/types.h"

struct ParsedTable;

// Callback receives (success, result_table_or_error)
// On failure table.error is non-empty; on success it is empty.
using YFCallback = std::function<void(bool ok, ParsedTable table)>;

// Call once at native startup to initialise the Python interpreter and worker thread.
// No-op when compiled with Emscripten or without HAVE_PYBIND11.
void yfinance_init();

// Call at shutdown to drain the worker thread and finalise Python.
void yfinance_shutdown();

// Queue an async yfinance history fetch.
// Callback is dispatched on the main thread via yfinance_poll_results().
void yfinance_fetch_async(uint32_t stream_id, const YFinanceSource& src, YFCallback cb);

// Drain the result queue — call each frame (mirrors http_poll_results()).
void yfinance_poll_results();

// Returns true when pybind11+yfinance are compiled in and initialised.
bool yfinance_available();
