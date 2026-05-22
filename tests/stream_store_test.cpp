#include <gtest/gtest.h>
#include <cmath>
#include "data/stream_store.h"
#include "io/yfinance_client.h"

// ── add_csv ───────────────────────────────────────────────────────────────────

TEST(StreamStore, AddCsv_StatusOk) {
    StreamStore ss;
    uint32_t id = ss.add_csv("test", "test.csv", "x,y\n1,2\n3,4\n");
    DataStream* ds = ss.find(id);
    ASSERT_NE(ds, nullptr);
    EXPECT_EQ(ds->status, StreamStatus::OK);
}

TEST(StreamStore, AddCsv_RowCount) {
    StreamStore ss;
    uint32_t id = ss.add_csv("data", "data.csv", "a,b\n1,2\n3,4\n5,6\n");
    EXPECT_EQ(ss.find(id)->row_count, 3u);
}

TEST(StreamStore, AddCsv_Schema) {
    StreamStore ss;
    uint32_t id = ss.add_csv("data", "data.csv", "time,value\n1700000000,3.14\n");
    DataStream* ds = ss.find(id);
    ASSERT_NE(ds, nullptr);
    ASSERT_EQ(ds->schema.size(), 2u);
    EXPECT_EQ(ds->schema[0].name, "time");
    EXPECT_EQ(ds->schema[0].type, FieldType::TIMESTAMP);
    EXPECT_EQ(ds->schema[1].name, "value");
    EXPECT_EQ(ds->schema[1].type, FieldType::NUMBER);
}

TEST(StreamStore, AddCsv_Name) {
    StreamStore ss;
    uint32_t id = ss.add_csv("My Stream", "f.csv", "x\n1\n");
    EXPECT_EQ(ss.find(id)->name, "My Stream");
}

TEST(StreamStore, AddCsv_SourceType) {
    StreamStore ss;
    uint32_t id = ss.add_csv("s", "f.csv", "x\n1\n");
    EXPECT_EQ(ss.find(id)->source_type, SourceType::CSV_FILE);
}

TEST(StreamStore, AddCsv_ColumnValues) {
    StreamStore ss;
    uint32_t id = ss.add_csv("s", "f.csv", "x,y\n10,20\n30,40\n");
    DataStream* ds = ss.find(id);
    ASSERT_NE(ds, nullptr);
    EXPECT_DOUBLE_EQ(ds->columns.at("x")[0], 10.0);
    EXPECT_DOUBLE_EQ(ds->columns.at("y")[1], 40.0);
}

TEST(StreamStore, AddCsv_BadData_ErrorState) {
    StreamStore ss;
    uint32_t id = ss.add_csv("bad", "bad.csv", "");
    DataStream* ds = ss.find(id);
    ASSERT_NE(ds, nullptr);
    EXPECT_EQ(ds->status, StreamStatus::ERROR_STATE);
    EXPECT_FALSE(ds->error_msg.empty());
}

// ── add_csv_placeholder ───────────────────────────────────────────────────────

TEST(StreamStore, AddCsvPlaceholder_ErrorState) {
    StreamStore ss;
    uint32_t id = ss.add_csv_placeholder("missing", "missing.csv");
    DataStream* ds = ss.find(id);
    ASSERT_NE(ds, nullptr);
    EXPECT_EQ(ds->status, StreamStatus::ERROR_STATE);
    EXPECT_FALSE(ds->error_msg.empty());
}

// ── find / remove ─────────────────────────────────────────────────────────────

TEST(StreamStore, Find_MissingId) {
    StreamStore ss;
    EXPECT_EQ(ss.find(999), nullptr);
}

TEST(StreamStore, Find_AfterAdd) {
    StreamStore ss;
    uint32_t id = ss.add_csv("s", "f.csv", "x\n1\n");
    EXPECT_NE(ss.find(id), nullptr);
}

TEST(StreamStore, Remove_StreamGone) {
    StreamStore ss;
    uint32_t id = ss.add_csv("s", "f.csv", "x\n1\n");
    ss.remove(id);
    EXPECT_EQ(ss.find(id), nullptr);
    EXPECT_EQ(ss.all().size(), 0u);
}

TEST(StreamStore, Remove_OnlyTargetRemoved) {
    StreamStore ss;
    uint32_t id1 = ss.add_csv("a", "a.csv", "x\n1\n");
    uint32_t id2 = ss.add_csv("b", "b.csv", "x\n2\n");
    ss.remove(id1);
    EXPECT_EQ(ss.find(id1), nullptr);
    EXPECT_NE(ss.find(id2), nullptr);
}

TEST(StreamStore, IdsAreUnique) {
    StreamStore ss;
    uint32_t id1 = ss.add_csv("a", "a.csv", "x\n1\n");
    uint32_t id2 = ss.add_csv("b", "b.csv", "x\n2\n");
    uint32_t id3 = ss.add_csv("c", "c.csv", "x\n3\n");
    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);
    EXPECT_NE(id1, id3);
}

// ── formula streams ───────────────────────────────────────────────────────────

TEST(StreamStore, Formula_BasicEval) {
    StreamStore ss;
    uint32_t src_id = ss.add_csv("data", "d.csv", "t,x\n0,10\n1,20\n2,30\n");

    FormulaSource fsrc;
    fsrc.expression  = "x * 2";
    fsrc.result_name = "doubled";
    fsrc.x_stream_id = src_id;
    fsrc.x_field     = "t";
    fsrc.bindings    = {{"x", src_id, "x"}};

    uint32_t fid = ss.add_formula("doubled", fsrc);
    DataStream* fds = ss.find(fid);
    ASSERT_NE(fds, nullptr);
    EXPECT_EQ(fds->status, StreamStatus::OK);
    ASSERT_EQ(fds->row_count, 3u);
    EXPECT_DOUBLE_EQ(fds->columns.at("doubled")[0], 20.0);
    EXPECT_DOUBLE_EQ(fds->columns.at("doubled")[2], 60.0);
}

TEST(StreamStore, Formula_SourceType) {
    StreamStore ss;
    uint32_t src_id = ss.add_csv("d", "d.csv", "t,x\n0,1\n");
    FormulaSource fsrc;
    fsrc.expression  = "x";
    fsrc.result_name = "out";
    fsrc.x_stream_id = src_id;
    fsrc.x_field     = "t";
    fsrc.bindings    = {{"x", src_id, "x"}};
    uint32_t fid = ss.add_formula("f", fsrc);
    EXPECT_EQ(ss.find(fid)->source_type, SourceType::FORMULA);
}

TEST(StreamStore, Formula_CrossStream) {
    StreamStore ss;
    uint32_t id1 = ss.add_csv("a", "a.csv", "t,val\n1,10\n2,20\n");
    uint32_t id2 = ss.add_csv("b", "b.csv", "t,val\n1,5\n2,8\n");

    FormulaSource fsrc;
    fsrc.expression  = "a + b";
    fsrc.result_name = "sum";
    fsrc.x_stream_id = id1;
    fsrc.x_field     = "t";
    fsrc.bindings    = {{"a", id1, "val"}, {"b", id2, "val"}};

    uint32_t fid = ss.add_formula("sum", fsrc);
    DataStream* fds = ss.find(fid);
    ASSERT_NE(fds, nullptr);
    EXPECT_EQ(fds->status, StreamStatus::OK);
    EXPECT_DOUBLE_EQ(fds->columns.at("sum")[0], 15.0);
    EXPECT_DOUBLE_EQ(fds->columns.at("sum")[1], 28.0);
}

TEST(StreamStore, Formula_ErrorOnMissingUpstream) {
    StreamStore ss;
    FormulaSource fsrc;
    fsrc.expression  = "x";
    fsrc.result_name = "out";
    fsrc.x_stream_id = 9999; // nonexistent
    fsrc.x_field     = "t";
    uint32_t fid = ss.add_formula("broken", fsrc);
    EXPECT_EQ(ss.find(fid)->status, StreamStatus::ERROR_STATE);
}

TEST(StreamStore, Formula_ErrorOnEmptyExpression) {
    StreamStore ss;
    uint32_t src_id = ss.add_csv("d", "d.csv", "t,x\n0,1\n");
    FormulaSource fsrc;
    fsrc.expression  = "";
    fsrc.result_name = "out";
    fsrc.x_stream_id = src_id;
    fsrc.x_field     = "t";
    uint32_t fid = ss.add_formula("empty", fsrc);
    EXPECT_EQ(ss.find(fid)->status, StreamStatus::ERROR_STATE);
}

TEST(StreamStore, Formula_ReevaluatesOnManualCall) {
    StreamStore ss;
    uint32_t src_id = ss.add_csv("d", "d.csv", "t,x\n0,1\n1,2\n");

    FormulaSource fsrc;
    fsrc.expression  = "x * 10";
    fsrc.result_name = "result";
    fsrc.x_stream_id = src_id;
    fsrc.x_field     = "t";
    fsrc.bindings    = {{"x", src_id, "x"}};

    uint32_t fid = ss.add_formula("f", fsrc);
    EXPECT_DOUBLE_EQ(ss.find(fid)->columns.at("result")[0], 10.0);

    // Mutate upstream data and re-evaluate
    ss.find(src_id)->columns["x"] = {5.0, 6.0};
    ss.evaluate_formula(fid);

    EXPECT_DOUBLE_EQ(ss.find(fid)->columns.at("result")[0], 50.0);
    EXPECT_DOUBLE_EQ(ss.find(fid)->columns.at("result")[1], 60.0);
}

TEST(StreamStore, Formula_ReevaluateDependents) {
    StreamStore ss;
    uint32_t src_id = ss.add_csv("d", "d.csv", "t,x\n0,1\n1,2\n");

    FormulaSource fsrc;
    fsrc.expression  = "x + 1";
    fsrc.result_name = "result";
    fsrc.x_stream_id = src_id;
    fsrc.x_field     = "t";
    fsrc.bindings    = {{"x", src_id, "x"}};
    uint32_t fid = ss.add_formula("f", fsrc);

    EXPECT_DOUBLE_EQ(ss.find(fid)->columns.at("result")[0], 2.0);

    // Calling reevaluate_dependents should re-run the formula
    ss.find(src_id)->columns["x"] = {10.0, 20.0};
    ss.reevaluate_dependents(src_id);

    EXPECT_DOUBLE_EQ(ss.find(fid)->columns.at("result")[0], 11.0);
    EXPECT_DOUBLE_EQ(ss.find(fid)->columns.at("result")[1], 21.0);
}

// ── yfinance stream (stub build) ──────────────────────────────────────────────

TEST(StreamStore, AddYFinance_SourceType) {
    StreamStore ss;
    YFinanceSource yfsrc;
    yfsrc.ticker   = "AAPL";
    yfsrc.period   = "1mo";
    yfsrc.interval = "1d";
    uint32_t id = ss.add_yfinance("AAPL", yfsrc);
    DataStream* ds = ss.find(id);
    ASSERT_NE(ds, nullptr);
    EXPECT_EQ(ds->source_type, SourceType::YFINANCE);
}

TEST(StreamStore, AddYFinance_MetadataStored) {
    StreamStore ss;
    YFinanceSource yfsrc;
    yfsrc.ticker   = "MSFT";
    yfsrc.period   = "3mo";
    yfsrc.interval = "1wk";
    uint32_t id = ss.add_yfinance("Microsoft", yfsrc);
    DataStream* ds = ss.find(id);
    ASSERT_NE(ds, nullptr);
    EXPECT_EQ(ds->yf_source.ticker,   "MSFT");
    EXPECT_EQ(ds->yf_source.period,   "3mo");
    EXPECT_EQ(ds->yf_source.interval, "1wk");
    EXPECT_EQ(ds->name, "Microsoft");
}

TEST(StreamStore, AddYFinance_StubReportsUnavailable) {
    // In non-pybind11 builds the stub callback fires synchronously with an error.
    // In a real pybind11 build the stream starts LOADING; result arrives via poll().
    StreamStore ss;
    YFinanceSource yfsrc{"AAPL", "1mo", "1d"};
    uint32_t id = ss.add_yfinance("AAPL", yfsrc);
    DataStream* ds = ss.find(id);
    ASSERT_NE(ds, nullptr);
#if !defined(HAVE_PYBIND11)
    EXPECT_EQ(ds->status, StreamStatus::ERROR_STATE);
    EXPECT_FALSE(ds->error_msg.empty());
#else
    // With pybind11 the fetch is async; check at least it was created
    EXPECT_TRUE(ds->status == StreamStatus::LOADING ||
                ds->status == StreamStatus::OK      ||
                ds->status == StreamStatus::ERROR_STATE);
#endif
}

// ── poll (smoke test) ─────────────────────────────────────────────────────────

TEST(StreamStore, Poll_DoesNotCrash) {
    StreamStore ss;
    ss.add_csv("s", "f.csv", "x\n1\n");
    EXPECT_NO_THROW(ss.poll());
    EXPECT_NO_THROW(ss.poll());
}

// ── window function formulas ──────────────────────────────────────────────────

static uint32_t make_price_stream(StreamStore& ss) {
    // 6-row stream: t=1..6, close=10,20,30,40,50,60
    return ss.add_csv("prices", "p.csv",
                      "t,close\n1,10\n2,20\n3,30\n4,40\n5,50\n6,60\n");
}

static uint32_t add_window_formula(StreamStore& ss, uint32_t src_id,
                                    const std::string& expr,
                                    const std::string& result = "out") {
    FormulaSource fsrc;
    fsrc.expression  = expr;
    fsrc.result_name = result;
    fsrc.x_stream_id = src_id;
    fsrc.x_field     = "t";
    fsrc.bindings    = {{"close", src_id, "close"}};
    return ss.add_formula("w", fsrc);
}

TEST(StreamStore, WindowSMA_WarmupIsNaN) {
    StreamStore ss;
    uint32_t src = make_price_stream(ss);
    uint32_t fid = add_window_formula(ss, src, "sma(close, 3)");
    DataStream* ds = ss.find(fid);
    ASSERT_NE(ds, nullptr);
    EXPECT_EQ(ds->status, StreamStatus::OK);
    EXPECT_EQ(ds->row_count, 6u);
    const auto& out = ds->columns.at("out");
    EXPECT_TRUE(std::isnan(out[0]));
    EXPECT_TRUE(std::isnan(out[1]));
    // sma(3) of [10,20,30] = 20
    EXPECT_DOUBLE_EQ(out[2], 20.0);
    // sma(3) of [20,30,40] = 30
    EXPECT_DOUBLE_EQ(out[3], 30.0);
}

TEST(StreamStore, WindowSMA_Values) {
    StreamStore ss;
    uint32_t src = make_price_stream(ss);
    uint32_t fid = add_window_formula(ss, src, "sma(close, 3)");
    const auto& out = ss.find(fid)->columns.at("out");
    EXPECT_DOUBLE_EQ(out[4], 40.0);  // (30+40+50)/3
    EXPECT_DOUBLE_EQ(out[5], 50.0);  // (40+50+60)/3
}

TEST(StreamStore, WindowEMA_ValidFromPeriodOnward) {
    StreamStore ss;
    uint32_t src = make_price_stream(ss);
    uint32_t fid = add_window_formula(ss, src, "ema(close, 3)");
    DataStream* ds = ss.find(fid);
    ASSERT_NE(ds, nullptr);
    EXPECT_EQ(ds->status, StreamStatus::OK);
    const auto& out = ds->columns.at("out");
    EXPECT_TRUE(std::isnan(out[0]));
    EXPECT_TRUE(std::isnan(out[1]));
    // seed = sma(3) of [10,20,30] = 20
    EXPECT_DOUBLE_EQ(out[2], 20.0);
    // ema: k=0.5, out[3] = 40*0.5 + 20*0.5 = 30
    EXPECT_DOUBLE_EQ(out[3], 30.0);
}

TEST(StreamStore, WindowStddev_Values) {
    StreamStore ss;
    // Simple: 3 rows all same value → stddev = 0
    uint32_t src = ss.add_csv("c", "c.csv", "t,x\n1,5\n2,5\n3,5\n4,5\n");
    FormulaSource fsrc;
    fsrc.expression  = "stddev(x, 3)";
    fsrc.result_name = "sd";
    fsrc.x_stream_id = src;
    fsrc.x_field     = "t";
    fsrc.bindings    = {{"x", src, "x"}};
    uint32_t fid = ss.add_formula("sd", fsrc);
    DataStream* ds = ss.find(fid);
    ASSERT_NE(ds, nullptr);
    EXPECT_EQ(ds->status, StreamStatus::OK);
    EXPECT_DOUBLE_EQ(ds->columns.at("sd")[2], 0.0);
    EXPECT_DOUBLE_EQ(ds->columns.at("sd")[3], 0.0);
}

TEST(StreamStore, WindowRmin_Rmax) {
    StreamStore ss;
    uint32_t src = make_price_stream(ss);
    uint32_t rmin_fid = add_window_formula(ss, src, "rmin(close, 3)", "lo");
    uint32_t rmax_fid = add_window_formula(ss, src, "rmax(close, 3)", "hi");
    const auto& lo = ss.find(rmin_fid)->columns.at("lo");
    const auto& hi = ss.find(rmax_fid)->columns.at("hi");
    // window at row 2 = [10,20,30]
    EXPECT_DOUBLE_EQ(lo[2], 10.0);
    EXPECT_DOUBLE_EQ(hi[2], 30.0);
}

TEST(StreamStore, WindowROC_Values) {
    StreamStore ss;
    uint32_t src = make_price_stream(ss);
    uint32_t fid = add_window_formula(ss, src, "roc(close, 1)");
    const auto& out = ss.find(fid)->columns.at("out");
    EXPECT_TRUE(std::isnan(out[0]));
    // roc(1) at row 1: (20-10)/10*100 = 100
    EXPECT_DOUBLE_EQ(out[1], 100.0);
    // roc(1) at row 2: (30-20)/20*100 = 50
    EXPECT_DOUBLE_EQ(out[2], 50.0);
}

TEST(StreamStore, WindowSMA_InExpression) {
    StreamStore ss;
    uint32_t src = make_price_stream(ss);
    // (close - sma(close, 3)) at row 2: 30 - 20 = 10
    uint32_t fid = add_window_formula(ss, src, "close - sma(close, 3)");
    const auto& out = ss.find(fid)->columns.at("out");
    EXPECT_TRUE(std::isnan(out[0]));
    EXPECT_TRUE(std::isnan(out[1]));
    EXPECT_DOUBLE_EQ(out[2], 10.0);  // 30 - 20
    EXPECT_DOUBLE_EQ(out[3], 10.0);  // 40 - 30
}

TEST(StreamStore, WindowInvalidSyntax_Error) {
    StreamStore ss;
    uint32_t src = make_price_stream(ss);
    // First arg is an expression, not a variable — should fail
    FormulaSource fsrc;
    fsrc.expression  = "sma(close + 1, 3)";
    fsrc.result_name = "out";
    fsrc.x_stream_id = src;
    fsrc.x_field     = "t";
    fsrc.bindings    = {{"close", src, "close"}};
    uint32_t fid = ss.add_formula("bad", fsrc);
    EXPECT_EQ(ss.find(fid)->status, StreamStatus::ERROR_STATE);
}

TEST(StreamStore, WindowUnknownAlias_Error) {
    StreamStore ss;
    uint32_t src = make_price_stream(ss);
    // Alias "price" not in bindings
    FormulaSource fsrc;
    fsrc.expression  = "sma(price, 3)";
    fsrc.result_name = "out";
    fsrc.x_stream_id = src;
    fsrc.x_field     = "t";
    fsrc.bindings    = {{"close", src, "close"}};
    uint32_t fid = ss.add_formula("bad", fsrc);
    EXPECT_EQ(ss.find(fid)->status, StreamStatus::ERROR_STATE);
}
