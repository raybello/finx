// Integration tests for the pybind11 yfinance bridge.
// Compiled separately with -DHAVE_PYBIND11; run via: make test-yfinance
//
// py::scoped_interpreter must only be created once per process, so a global
// ::testing::Environment handles init/shutdown and all tests run within it.
//
// Tests that require the yfinance Python package or network access call
// GTEST_SKIP() when those resources are absent — the suite always exits green.

#include <gtest/gtest.h>
#include <pybind11/embed.h>
#include <atomic>
#include <chrono>
#include <thread>

#include "io/csv_parser.h"
#include "io/yfinance_client.h"
#include "data/types.h"

namespace py = pybind11;

// ── Global environment ────────────────────────────────────────────────────────

// Initialise the Python interpreter (via yfinance_init) exactly once per
// process — destroying and recreating py::scoped_interpreter is undefined.
class YFinanceEnvironment : public ::testing::Environment {
public:
    void SetUp()    override { yfinance_init(); }
    void TearDown() override { yfinance_shutdown(); }
};

// ── helpers ───────────────────────────────────────────────────────────────────

static bool drain_until(std::atomic<bool>& flag, int max_ms = 20000) {
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(max_ms);
    while (!flag.load()) {
        yfinance_poll_results();
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return true;
}

static bool yfinance_importable() {
    py::gil_scoped_acquire gil;
    try {
        py::module_::import("yfinance");
        return true;
    } catch (...) {
        return false;
    }
}

// ── Post-init state ───────────────────────────────────────────────────────────

TEST(YFinanceBasic, AvailableAfterInit) {
    EXPECT_TRUE(yfinance_available());
}

TEST(YFinanceBasic, PollEmptyQueueDoesNotCrash) {
    EXPECT_NO_THROW(yfinance_poll_results());
    EXPECT_NO_THROW(yfinance_poll_results());
}

// ── Worker dispatch ───────────────────────────────────────────────────────────

TEST(YFinanceDispatch, CallbackNotFiredBeforePoll) {
    std::atomic<bool> called{false};

    YFinanceSource src{"INVALID_XYZ_99999", "1d", "1h"};
    yfinance_fetch_async(1, src, [&](bool, ParsedTable) {
        called.store(true);
    });
    // Without polling, the main thread must not have received the callback yet.
    EXPECT_FALSE(called.load());
    drain_until(called, 20000); // drain so subsequent tests start clean
}

TEST(YFinanceDispatch, CallbackArrivesViaPoll) {
    std::atomic<bool> called{false};

    YFinanceSource src{"INVALID_XYZ_99999", "1d", "1h"};
    yfinance_fetch_async(1, src, [&](bool, ParsedTable) {
        called.store(true);
    });

    EXPECT_TRUE(drain_until(called, 20000))
        << "Timeout: callback never delivered through yfinance_poll_results()";
    EXPECT_TRUE(called.load());
}

TEST(YFinanceDispatch, InvalidTickerReportsError) {
    std::atomic<bool> done{false};
    bool ok_out      = true;
    std::string error_out;

    YFinanceSource src{"INVALID_XYZ_99999", "1d", "1h"};
    yfinance_fetch_async(1, src, [&](bool ok, ParsedTable tbl) {
        ok_out    = ok;
        error_out = tbl.error;
        done.store(true);
    });

    ASSERT_TRUE(drain_until(done, 20000));
    // Empty DataFrame for unknown ticker → bridge surfaces an error.
    EXPECT_FALSE(ok_out);
    EXPECT_FALSE(error_out.empty());
}

TEST(YFinanceDispatch, MultipleRequestsAllDelivered) {
    std::atomic<int> count{0};

    for (int i = 0; i < 3; ++i) {
        YFinanceSource src{"INVALID_XYZ_" + std::to_string(i), "1d", "1h"};
        yfinance_fetch_async(i, src, [&](bool, ParsedTable) {
            count.fetch_add(1);
        });
    }

    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(30000);
    while (count.load() < 3 &&
           std::chrono::steady_clock::now() < deadline) {
        yfinance_poll_results();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_EQ(count.load(), 3);
}

// ── Module import ─────────────────────────────────────────────────────────────

TEST(YFinanceModule, YFinancePackageImportable) {
    if (!yfinance_importable()) {
        GTEST_SKIP() << "yfinance Python package not installed";
    }
    SUCCEED();
}

// ── Real network (requires yfinance + internet) ───────────────────────────────

TEST(YFinanceNetwork, FetchSNPS_Schema) {
    if (!yfinance_importable()) {
        GTEST_SKIP() << "yfinance not installed";
    }

    std::atomic<bool> done{false};
    bool ok_out = false;
    ParsedTable result;

    YFinanceSource src{"SNPS", "5d", "1d"};
    yfinance_fetch_async(1, src, [&](bool ok, ParsedTable tbl) {
        ok_out = ok;
        result = std::move(tbl);
        done.store(true);
    });

    ASSERT_TRUE(drain_until(done, 30000));

    if (!ok_out) {
        GTEST_SKIP() << "Network unavailable: " << result.error;
    }

    EXPECT_TRUE(result.error.empty());
    EXPECT_GT(result.row_count, 0u);

    for (const char* col : {"Date", "Open", "High", "Low", "Close", "Volume"}) {
        EXPECT_TRUE(result.columns.count(col)) << "missing column: " << col;
    }

    bool found_ts = false;
    for (const auto& fd : result.schema) {
        if (fd.name == "Date") {
            EXPECT_EQ(fd.type, FieldType::TIMESTAMP);
            found_ts = true;
        }
    }
    EXPECT_TRUE(found_ts);
}

TEST(YFinanceNetwork, FetchSNPS_TimestampsAreEpochSeconds) {
    if (!yfinance_importable()) {
        GTEST_SKIP() << "yfinance not installed";
    }

    std::atomic<bool> done{false};
    bool ok_out = false;
    ParsedTable result;

    YFinanceSource src{"SNPS", "5d", "1d"};
    yfinance_fetch_async(1, src, [&](bool ok, ParsedTable tbl) {
        ok_out = ok;
        result = std::move(tbl);
        done.store(true);
    });

    ASSERT_TRUE(drain_until(done, 30000));

    if (!ok_out) {
        GTEST_SKIP() << "Network unavailable: " << result.error;
    }

    ASSERT_TRUE(result.columns.count("Date"));
    for (double ts : result.columns.at("Date")) {
        // Valid range: 1990 – 2100 in epoch seconds.
        // If nanoseconds crept through they'd be ~1.7e18, not ~1.7e9.
        EXPECT_GT(ts, 6.31e8) << "timestamp predates 1990 — wrong resolution?";
        EXPECT_LT(ts, 4.10e9) << "timestamp after 2100 — likely nanoseconds, not seconds";
    }
}

TEST(YFinanceNetwork, FetchSNPS_ColumnLengthsConsistent) {
    if (!yfinance_importable()) {
        GTEST_SKIP() << "yfinance not installed";
    }

    std::atomic<bool> done{false};
    bool ok_out = false;
    ParsedTable result;

    YFinanceSource src{"SNPS", "5d", "1d"};
    yfinance_fetch_async(1, src, [&](bool ok, ParsedTable tbl) {
        ok_out = ok;
        result = std::move(tbl);
        done.store(true);
    });

    ASSERT_TRUE(drain_until(done, 30000));

    if (!ok_out) {
        GTEST_SKIP() << "Network unavailable: " << result.error;
    }

    size_t n = result.row_count;
    for (const auto& [name, col] : result.columns) {
        EXPECT_EQ(col.size(), n) << "column '" << name << "' length mismatch";
    }
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    // Register the global environment before running tests.
    ::testing::AddGlobalTestEnvironment(new YFinanceEnvironment());
    return RUN_ALL_TESTS();
}
