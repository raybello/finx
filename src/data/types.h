#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "imgui.h"

enum class FieldType    { NUMBER, TIMESTAMP, STRING };
enum class SourceType   { CSV_FILE, HTTP_GET };
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

struct HttpSource {
    std::string url_template;
    std::vector<std::pair<std::string,std::string>> params;
    std::string json_path;          // dot-separated, e.g. "results"
    std::vector<FieldMapEntry> field_map;
};

struct CsvSource {
    std::string filename;
    std::string raw_text;           // full file contents
};

struct DataStream {
    uint32_t     id = 0;
    std::string  name;
    SourceType   source_type = SourceType::CSV_FILE;
    CsvSource    csv_source;
    HttpSource   http_source;
    std::vector<FieldDef> schema;
    std::unordered_map<std::string, std::vector<double>>      columns;
    std::unordered_map<std::string, std::vector<std::string>> str_columns;
    StreamStatus status    = StreamStatus::IDLE;
    std::string  error_msg;
    size_t       row_count = 0;
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
};
