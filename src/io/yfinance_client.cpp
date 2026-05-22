#include "io/yfinance_client.h"
#include "io/csv_parser.h"

// ── Full pybind11 implementation (native + HAVE_PYBIND11 only) ────────────────
#if !defined(__EMSCRIPTEN__) && defined(HAVE_PYBIND11)

#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

namespace py = pybind11;

// ── Request / result queues ───────────────────────────────────────────────────

struct YFRequest {
    YFinanceSource src;
    YFCallback     callback;
};

struct YFResult {
    YFCallback  callback;
    bool        ok;
    ParsedTable table;
};

static std::mutex              g_req_mu;
static std::condition_variable g_req_cv;
static std::queue<YFRequest>   g_req_q;

static std::mutex            g_res_mu;
static std::queue<YFResult>  g_res_q;

// ── Python interpreter lifetime ───────────────────────────────────────────────

static std::unique_ptr<py::scoped_interpreter> g_interp;
static PyThreadState*                          g_main_ts = nullptr;

// ── Worker ────────────────────────────────────────────────────────────────────

static std::thread        g_worker;
static std::atomic<bool>  g_running{false};

// Perform the yfinance fetch inside the GIL — returns a filled ParsedTable.
static ParsedTable do_fetch(const YFinanceSource& src) {
    ParsedTable result;
    try {
        py::module_ yf = py::module_::import("yfinance");
        auto ticker    = yf.attr("Ticker")(src.ticker);
        auto hist      = ticker.attr("history")(
            py::arg("period")   = src.period,
            py::arg("interval") = src.interval
        );

        // Bail out early on empty DataFrame
        if (py::len(hist) == 0) {
            result.error = "No data returned for '" + src.ticker +
                           "' (period=" + src.period + ", interval=" + src.interval + ")";
            return result;
        }

        // ── Timestamps ────────────────────────────────────────────────────────
        // hist.index is a tz-aware DatetimeIndex.
        // Pandas 2.x uses datetime64[s, UTC]  → astype('int64') = epoch seconds.
        // Pandas 1.x uses datetime64[ns, UTC] → astype('int64') = epoch nanoseconds.
        // Detect the resolution from the dtype string and scale accordingly.
        auto index_dtype = hist.attr("index").attr("dtype")
                               .attr("__str__")().cast<std::string>();
        double ts_scale = 1.0; // default: seconds
        if (index_dtype.find("[ns") != std::string::npos) ts_scale = 1e-9;
        else if (index_dtype.find("[us") != std::string::npos) ts_scale = 1e-6;
        else if (index_dtype.find("[ms") != std::string::npos) ts_scale = 1e-3;

        auto ts_list = hist.attr("index")
                           .attr("astype")("int64")
                           .attr("tolist")()
                           .cast<std::vector<int64_t>>();

        std::vector<double> timestamps;
        timestamps.reserve(ts_list.size());
        for (int64_t t : ts_list)
            timestamps.push_back(static_cast<double>(t) * ts_scale);

        result.schema.push_back({"Date", FieldType::TIMESTAMP});
        result.columns["Date"] = std::move(timestamps);

        // ── Numeric columns ───────────────────────────────────────────────────
        static const char* kCols[] = {
            "Open", "High", "Low", "Close", "Volume", "Dividends", "Stock Splits"
        };
        for (const char* col_name : kCols) {
            try {
                auto col  = hist.attr("__getitem__")(col_name);
                auto vals = col.attr("tolist")().cast<std::vector<double>>();
                result.schema.push_back({col_name, FieldType::NUMBER});
                result.columns[col_name] = std::move(vals);
            } catch (...) {
                // column absent — skip
            }
        }

        result.row_count = result.columns.count("Date")
                         ? result.columns.at("Date").size() : 0;

    } catch (const py::error_already_set& e) {
        result.error = std::string("Python: ") + e.what();
    } catch (const std::exception& e) {
        result.error = std::string("Error: ") + e.what();
    }
    return result;
}

static void worker_loop() {
    while (true) {
        YFRequest req;
        {
            std::unique_lock<std::mutex> lk(g_req_mu);
            g_req_cv.wait(lk, []{ return !g_req_q.empty() || !g_running; });
            if (!g_running && g_req_q.empty()) break;
            req = std::move(g_req_q.front());
            g_req_q.pop();
        }

        // Acquire GIL for this Python call (PyGILState_Ensure / PyGILState_Release).
        py::gil_scoped_acquire gil;
        ParsedTable tbl = do_fetch(req.src);
        bool ok = tbl.error.empty();

        YFResult res;
        res.callback = std::move(req.callback);
        res.ok       = ok;
        res.table    = std::move(tbl);
        {
            std::lock_guard<std::mutex> lk(g_res_mu);
            g_res_q.push(std::move(res));
        }
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void yfinance_init() {
    if (g_running) return;

    // Initialise the Python interpreter on the main thread.
    g_interp = std::make_unique<py::scoped_interpreter>();

    // Release the GIL so the worker thread can acquire it freely.
    // PyEval_SaveThread() releases GIL and returns the current thread state.
    g_main_ts = PyEval_SaveThread();

    g_running = true;
    g_worker  = std::thread(worker_loop);
}

void yfinance_shutdown() {
    if (!g_running) return;
    g_running = false;
    g_req_cv.notify_all();
    if (g_worker.joinable()) g_worker.join();

    // Restore the main thread's GIL state before tearing down the interpreter.
    if (g_main_ts) {
        PyEval_RestoreThread(g_main_ts);
        g_main_ts = nullptr;
    }
    g_interp.reset();
}

void yfinance_fetch_async(uint32_t /*stream_id*/, const YFinanceSource& src, YFCallback cb) {
    if (!g_running) {
        ParsedTable err;
        err.error = "yfinance not initialised";
        cb(false, std::move(err));
        return;
    }
    YFRequest req;
    req.src      = src;
    req.callback = std::move(cb);
    {
        std::lock_guard<std::mutex> lk(g_req_mu);
        g_req_q.push(std::move(req));
    }
    g_req_cv.notify_one();
}

void yfinance_poll_results() {
    std::queue<YFResult> local;
    {
        std::lock_guard<std::mutex> lk(g_res_mu);
        std::swap(local, g_res_q);
    }
    while (!local.empty()) {
        YFResult& r = local.front();
        r.callback(r.ok, std::move(r.table));
        local.pop();
    }
}

bool yfinance_available() { return true; }

// ── Stubs for Emscripten / no-pybind11 builds ─────────────────────────────────
#else

void yfinance_init()    {}
void yfinance_shutdown() {}
void yfinance_fetch_async(uint32_t, const YFinanceSource&, YFCallback cb) {
    ParsedTable err;
    err.error = "yfinance not available in this build";
    cb(false, std::move(err));
}
void yfinance_poll_results() {}
bool yfinance_available() { return false; }

#endif
