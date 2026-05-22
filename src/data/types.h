#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "imgui.h"

enum class FieldType    { NUMBER, TIMESTAMP, STRING };
enum class SourceType   { CSV_FILE, HTTP_GET, FORMULA, YFINANCE };
enum class StreamStatus { IDLE, LOADING, OK, ERROR_STATE };
enum class PlotType     { LINE, SCATTER, BAR, STEP };

struct FieldDef {
    std::string name;
    FieldType   type = FieldType::NUMBER;
};

struct FieldMapEntry {
    std::string output_name;
    std::string json_key;
    FieldType   type = FieldType::NUMBER;
};

enum class ResponseFormat { AUTO, JSON, CSV };

struct HttpSource {
    std::string url_template;
    std::vector<std::pair<std::string,std::string>> params;
    std::string json_path;          // dot-separated, e.g. "results"
    std::vector<FieldMapEntry> field_map;
    ResponseFormat response_format = ResponseFormat::AUTO;
};

struct CsvSource {
    std::string filename;           // basename for display
    std::string path;               // full filesystem path (empty on web/embedded)
    std::string raw_text;           // full file contents
};

struct YFinanceSource {
    std::string ticker;                   // e.g. "AAPL"
    std::string period   = "1mo";         // e.g. "1d","5d","1mo","3mo","6mo","1y","2y","5y","10y","ytd","max"
    std::string interval = "1d";          // e.g. "1m","5m","15m","1h","1d","1wk","1mo"
};

struct FormulaBinding {
    std::string alias;        // identifier used in expression, e.g. "births"
    uint32_t    stream_id = 0;
    std::string field_name;
};

struct FormulaSource {
    std::string expression;       // e.g. "(births / total_pop) * 100"
    std::string result_name;      // output column name
    uint32_t    x_stream_id = 0;
    std::string x_field;          // x-axis column in that stream
    std::vector<FormulaBinding> bindings;
};

struct DataStream {
    uint32_t     id = 0;
    std::string  name;
    SourceType   source_type = SourceType::CSV_FILE;
    CsvSource      csv_source;
    HttpSource     http_source;
    YFinanceSource yf_source;
    std::vector<FieldDef> schema;
    std::unordered_map<std::string, std::vector<double>>      columns;
    std::unordered_map<std::string, std::vector<std::string>> str_columns;
    FormulaSource formula_source;
    StreamStatus status       = StreamStatus::IDLE;
    std::string  error_msg;
    size_t       row_count    = 0;
    bool         data_changed = false; // set by apply_parsed; consumed by App::render
};

struct PlotSeries {
    uint32_t    stream_id = 0;
    std::string x_field;
    std::string y_field;
    int         y_axis    = 0;           // 0=left, 1=right
    PlotType    plot_type = PlotType::LINE;
    ImVec4      color     = {0.2f,0.8f,0.4f,1.0f};
    std::string label;
};

struct Plot {
    uint32_t    id = 0;
    std::string name;
    std::vector<PlotSeries> series;
    std::string x_label;
    std::string y_label;
    std::string y2_label;
    bool        show_legend = true;
    bool        open        = true;
    bool        needs_fit   = false; // set true to auto-fit axes on next frame
};
