#include <gtest/gtest.h>
#include "io/csv_parser.h"

// ── Delimiter detection ───────────────────────────────────────────────────────

TEST(CsvParser, CommaDelimiter) {
    auto t = parse_csv("a,b,c\n1,2,3\n");
    EXPECT_EQ(t.schema.size(), 3u);
    EXPECT_EQ(t.schema[0].name, "a");
    EXPECT_EQ(t.schema[2].name, "c");
}

TEST(CsvParser, SemicolonDelimiter) {
    auto t = parse_csv("x;y\n10;20\n30;40\n");
    ASSERT_EQ(t.row_count, 2u);
    EXPECT_DOUBLE_EQ(t.columns.at("x")[0], 10.0);
    EXPECT_DOUBLE_EQ(t.columns.at("y")[1], 40.0);
}

TEST(CsvParser, TabDelimiter) {
    auto t = parse_csv("x\ty\n1\t2\n3\t4\n");
    ASSERT_EQ(t.row_count, 2u);
    EXPECT_DOUBLE_EQ(t.columns.at("x")[1], 3.0);
}

// ── Row / column counts ───────────────────────────────────────────────────────

TEST(CsvParser, RowCount) {
    auto t = parse_csv("a,b\n1,2\n3,4\n5,6\n");
    EXPECT_EQ(t.row_count, 3u);
}

TEST(CsvParser, HeaderOnly) {
    auto t = parse_csv("a,b,c\n");
    EXPECT_EQ(t.row_count, 0u);
    EXPECT_EQ(t.schema.size(), 3u);
    EXPECT_TRUE(t.error.empty());
}

TEST(CsvParser, EmptyInput) {
    auto t = parse_csv("");
    EXPECT_FALSE(t.error.empty());
}

// ── Type inference ────────────────────────────────────────────────────────────

TEST(CsvParser, TypeNumber) {
    auto t = parse_csv("val\n1.5\n2.5\n3.0\n");
    ASSERT_EQ(t.schema.size(), 1u);
    EXPECT_EQ(t.schema[0].type, FieldType::NUMBER);
    EXPECT_DOUBLE_EQ(t.columns.at("val")[0], 1.5);
    EXPECT_DOUBLE_EQ(t.columns.at("val")[2], 3.0);
}

TEST(CsvParser, TypeString) {
    auto t = parse_csv("name\nalice\nbob\ncharlie\n");
    ASSERT_EQ(t.schema.size(), 1u);
    EXPECT_EQ(t.schema[0].type, FieldType::STRING);
    EXPECT_EQ(t.str_columns.at("name")[1], "bob");
}

TEST(CsvParser, TypeTimestampUnixSeconds) {
    // Values between 1e9 and 3e9 are treated as unix epoch seconds
    auto t = parse_csv("ts\n1700000000\n1700086400\n");
    ASSERT_EQ(t.schema.size(), 1u);
    EXPECT_EQ(t.schema[0].type, FieldType::TIMESTAMP);
    EXPECT_DOUBLE_EQ(t.columns.at("ts")[0], 1700000000.0);
}

TEST(CsvParser, TypeTimestampUnixMilliseconds) {
    // Values > 1e12 are treated as ms epoch, stored as seconds
    auto t = parse_csv("ts\n1700000000000\n1700086400000\n");
    ASSERT_EQ(t.schema.size(), 1u);
    EXPECT_EQ(t.schema[0].type, FieldType::TIMESTAMP);
    EXPECT_DOUBLE_EQ(t.columns.at("ts")[0], 1700000000.0);
}

TEST(CsvParser, TypeTimestampIso8601) {
    auto t = parse_csv("date\n2024-01-15\n2024-02-20\n");
    ASSERT_EQ(t.schema.size(), 1u);
    EXPECT_EQ(t.schema[0].type, FieldType::TIMESTAMP);
    EXPECT_GT(t.columns.at("date")[0], 0.0);
}

TEST(CsvParser, MixedColumns) {
    auto t = parse_csv("label,value,time\nhello,3.14,1700000000\nworld,2.71,1700086400\n");
    ASSERT_EQ(t.schema.size(), 3u);
    EXPECT_EQ(t.schema[0].type, FieldType::STRING);
    EXPECT_EQ(t.schema[1].type, FieldType::NUMBER);
    EXPECT_EQ(t.schema[2].type, FieldType::TIMESTAMP);
}

// ── Quoted fields ─────────────────────────────────────────────────────────────

TEST(CsvParser, QuotedFieldWithComma) {
    auto t = parse_csv("name,val\n\"Smith, John\",42\n");
    ASSERT_EQ(t.row_count, 1u);
    EXPECT_EQ(t.str_columns.at("name")[0], "Smith, John");
    EXPECT_DOUBLE_EQ(t.columns.at("val")[0], 42.0);
}

TEST(CsvParser, EscapedQuoteInField) {
    auto t = parse_csv("desc\n\"say \"\"hi\"\"\"\n");
    ASSERT_EQ(t.row_count, 1u);
    EXPECT_EQ(t.str_columns.at("desc")[0], "say \"hi\"");
}

// ── Whitespace handling ───────────────────────────────────────────────────────

TEST(CsvParser, HeaderWhitespaceTrimmed) {
    auto t = parse_csv(" x , y \n1,2\n");
    ASSERT_EQ(t.schema.size(), 2u);
    EXPECT_EQ(t.schema[0].name, "x");
    EXPECT_EQ(t.schema[1].name, "y");
}

TEST(CsvParser, CrLfLineEndings) {
    auto t = parse_csv("a,b\r\n1,2\r\n3,4\r\n");
    EXPECT_EQ(t.row_count, 2u);
    EXPECT_DOUBLE_EQ(t.columns.at("a")[1], 3.0);
}

// ── Negative and floating point numbers ──────────────────────────────────────

TEST(CsvParser, NegativeNumbers) {
    auto t = parse_csv("v\n-1.5\n-2.5\n");
    EXPECT_DOUBLE_EQ(t.columns.at("v")[0], -1.5);
    EXPECT_DOUBLE_EQ(t.columns.at("v")[1], -2.5);
}

TEST(CsvParser, FloatingPointPrecision) {
    auto t = parse_csv("v\n3.141592653589793\n");
    EXPECT_NEAR(t.columns.at("v")[0], 3.14159265, 1e-7);
}
