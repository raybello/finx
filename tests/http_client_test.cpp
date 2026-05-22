#include <gtest/gtest.h>
#include "io/http_client.h"
#include "io/csv_parser.h"

// ── build_url ─────────────────────────────────────────────────────────────────

TEST(BuildUrl, NoParams) {
    EXPECT_EQ(build_url("https://api.example.com/data", {}),
              "https://api.example.com/data");
}

TEST(BuildUrl, SinglePlaceholder) {
    auto url = build_url("https://api.example.com/{symbol}/prices",
                         {{"symbol", "AAPL"}});
    EXPECT_EQ(url, "https://api.example.com/AAPL/prices");
}

TEST(BuildUrl, MultiplePlaceholders) {
    auto url = build_url("https://api.example.com/{sym}?token={tok}",
                         {{"sym", "MSFT"}, {"tok", "abc123"}});
    EXPECT_EQ(url, "https://api.example.com/MSFT?token=abc123");
}

TEST(BuildUrl, UnusedParamBecomesQueryString) {
    auto url = build_url("https://api.example.com/data",
                         {{"apikey", "secret"}});
    EXPECT_EQ(url, "https://api.example.com/data?apikey=secret");
}

TEST(BuildUrl, UnusedParamAppendsToExistingQuery) {
    auto url = build_url("https://api.example.com/data?format=json",
                         {{"apikey", "secret"}});
    EXPECT_EQ(url, "https://api.example.com/data?format=json&apikey=secret");
}

TEST(BuildUrl, MixedPlaceholderAndUnused) {
    auto url = build_url("https://api.example.com/{sym}",
                         {{"sym", "GOOG"}, {"limit", "100"}});
    EXPECT_EQ(url, "https://api.example.com/GOOG?limit=100");
}

// ── extract_json ──────────────────────────────────────────────────────────────

TEST(ExtractJson, SimpleArray) {
    const std::string body = R"([{"x":1.0,"y":2.0},{"x":3.0,"y":4.0}])";
    auto t = extract_json(body, "", {
        {"X", "x", FieldType::NUMBER},
        {"Y", "y", FieldType::NUMBER}
    });
    ASSERT_TRUE(t.error.empty());
    EXPECT_EQ(t.row_count, 2u);
    EXPECT_DOUBLE_EQ(t.columns.at("X")[0], 1.0);
    EXPECT_DOUBLE_EQ(t.columns.at("Y")[1], 4.0);
}

TEST(ExtractJson, NestedPath) {
    const std::string body = R"({"results":{"data":[{"v":42.0}]}})";
    auto t = extract_json(body, "results.data", {{"V", "v", FieldType::NUMBER}});
    ASSERT_TRUE(t.error.empty());
    EXPECT_EQ(t.row_count, 1u);
    EXPECT_DOUBLE_EQ(t.columns.at("V")[0], 42.0);
}

TEST(ExtractJson, StringField) {
    const std::string body = R"([{"name":"alpha"},{"name":"beta"}])";
    auto t = extract_json(body, "", {{"Name", "name", FieldType::STRING}});
    ASSERT_TRUE(t.error.empty());
    EXPECT_EQ(t.str_columns.at("Name")[0], "alpha");
    EXPECT_EQ(t.str_columns.at("Name")[1], "beta");
}

TEST(ExtractJson, TimestampNumeric) {
    const std::string body = R"([{"ts":1700000000},{"ts":1700086400}])";
    auto t = extract_json(body, "", {{"Date", "ts", FieldType::TIMESTAMP}});
    ASSERT_TRUE(t.error.empty());
    EXPECT_DOUBLE_EQ(t.columns.at("Date")[0], 1700000000.0);
}

TEST(ExtractJson, TimestampMilliseconds) {
    // Values > 1e12 divided by 1000
    const std::string body = R"([{"ts":1700000000000}])";
    auto t = extract_json(body, "", {{"Date", "ts", FieldType::TIMESTAMP}});
    ASSERT_TRUE(t.error.empty());
    EXPECT_DOUBLE_EQ(t.columns.at("Date")[0], 1700000000.0);
}

TEST(ExtractJson, TimestampIso8601String) {
    const std::string body = R"([{"d":"2024-01-15"}])";
    auto t = extract_json(body, "", {{"Date", "d", FieldType::TIMESTAMP}});
    ASSERT_TRUE(t.error.empty());
    EXPECT_GT(t.columns.at("Date")[0], 0.0);
}

TEST(ExtractJson, MissingKeyInsertsDefault) {
    const std::string body = R"([{"x":1.0},{"y":2.0}])";
    auto t = extract_json(body, "", {{"X", "x", FieldType::NUMBER}});
    ASSERT_TRUE(t.error.empty());
    EXPECT_EQ(t.row_count, 2u);
    EXPECT_DOUBLE_EQ(t.columns.at("X")[0], 1.0);
    EXPECT_DOUBLE_EQ(t.columns.at("X")[1], 0.0); // default for missing key
}

TEST(ExtractJson, BadJson) {
    auto t = extract_json("not json at all", "data", {});
    EXPECT_FALSE(t.error.empty());
}

TEST(ExtractJson, PathNotFound) {
    auto t = extract_json(R"({"a":1})", "missing_key", {});
    EXPECT_FALSE(t.error.empty());
}

TEST(ExtractJson, PathNotArray) {
    auto t = extract_json(R"({"data":{"x":1}})", "data", {});
    EXPECT_FALSE(t.error.empty());
}

TEST(ExtractJson, BooleanAsNumber) {
    const std::string body = R"([{"active":true},{"active":false}])";
    auto t = extract_json(body, "", {{"Active", "active", FieldType::NUMBER}});
    ASSERT_TRUE(t.error.empty());
    EXPECT_DOUBLE_EQ(t.columns.at("Active")[0], 1.0);
    EXPECT_DOUBLE_EQ(t.columns.at("Active")[1], 0.0);
}

// ── extract_response ──────────────────────────────────────────────────────────

TEST(ExtractResponse, AutoDetectsJson) {
    const std::string body = R"({"items":[{"v":7.0}]})";
    HttpSource src;
    src.json_path       = "items";
    src.response_format = ResponseFormat::AUTO;
    src.field_map       = {{"V", "v", FieldType::NUMBER}};
    auto t = extract_response(body, src);
    ASSERT_TRUE(t.error.empty());
    EXPECT_EQ(t.row_count, 1u);
    EXPECT_DOUBLE_EQ(t.columns.at("V")[0], 7.0);
}

TEST(ExtractResponse, AutoDetectsJsonArray) {
    const std::string body = R"([{"v":7.0}])";
    HttpSource src;
    src.response_format = ResponseFormat::AUTO;
    src.field_map       = {{"V", "v", FieldType::NUMBER}};
    auto t = extract_response(body, src);
    ASSERT_TRUE(t.error.empty());
    EXPECT_EQ(t.row_count, 1u);
}

TEST(ExtractResponse, AutoDetectsCsv) {
    const std::string body = "a,b\n1,2\n3,4\n";
    HttpSource src;
    src.response_format = ResponseFormat::AUTO;
    auto t = extract_response(body, src);
    ASSERT_TRUE(t.error.empty());
    EXPECT_EQ(t.row_count, 2u);
}

TEST(ExtractResponse, ExplicitCsvOverridesJsonContent) {
    const std::string body = "x,y\n10,20\n";
    HttpSource src;
    src.response_format = ResponseFormat::CSV;
    auto t = extract_response(body, src);
    ASSERT_TRUE(t.error.empty());
    EXPECT_EQ(t.row_count, 1u);
    EXPECT_DOUBLE_EQ(t.columns.at("x")[0], 10.0);
}

TEST(ExtractResponse, ExplicitJsonParsesJson) {
    const std::string body = R"([{"n":99.0}])";
    HttpSource src;
    src.response_format = ResponseFormat::JSON;
    src.field_map       = {{"N", "n", FieldType::NUMBER}};
    auto t = extract_response(body, src);
    ASSERT_TRUE(t.error.empty());
    EXPECT_DOUBLE_EQ(t.columns.at("N")[0], 99.0);
}

TEST(ExtractResponse, WhitespaceBeforeJsonBrace) {
    const std::string body = "   \n{\"d\":[{\"v\":5.0}]}";
    HttpSource src;
    src.json_path       = "d";
    src.response_format = ResponseFormat::AUTO;
    src.field_map       = {{"V", "v", FieldType::NUMBER}};
    auto t = extract_response(body, src);
    ASSERT_TRUE(t.error.empty());
    EXPECT_DOUBLE_EQ(t.columns.at("V")[0], 5.0);
}
