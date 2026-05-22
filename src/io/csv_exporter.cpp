#include "io/csv_exporter.h"
#include <fstream>
#include <cstdio>
#include <ctime>
#include <cctype>
#include <cmath>

std::string csv_export_default_path(const DataStream& ds) {
    std::string name;
    for (char c : ds.name) {
        name += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    }
    if (name.empty()) name = "stream";
    return name + "_export.csv";
}

bool export_stream_csv(const DataStream& ds, const std::string& path) {
    if (ds.schema.empty() || ds.row_count == 0) return false;

    std::ofstream f(path);
    if (!f.is_open()) return false;

    // Header row
    for (size_t ci = 0; ci < ds.schema.size(); ++ci) {
        if (ci) f << ',';
        f << ds.schema[ci].name;
    }
    f << '\n';

    // Data rows
    for (size_t row = 0; row < ds.row_count; ++row) {
        for (size_t ci = 0; ci < ds.schema.size(); ++ci) {
            if (ci) f << ',';
            const auto& fd = ds.schema[ci];

            if (fd.type == FieldType::STRING) {
                auto it = ds.str_columns.find(fd.name);
                if (it != ds.str_columns.end() && row < it->second.size())
                    f << it->second[row];
            } else if (fd.type == FieldType::TIMESTAMP) {
                auto it = ds.columns.find(fd.name);
                if (it != ds.columns.end() && row < it->second.size()) {
                    time_t t = (time_t)it->second[row];
                    char buf[32];
                    strftime(buf, sizeof(buf), "%Y-%m-%d", gmtime(&t));
                    f << buf;
                }
            } else {
                auto it = ds.columns.find(fd.name);
                if (it != ds.columns.end() && row < it->second.size()) {
                    double v = it->second[row];
                    if (std::isnan(v)) {
                        // write empty cell for NaN (common convention)
                    } else {
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "%.10g", v);
                        f << buf;
                    }
                }
            }
        }
        f << '\n';
    }

    return true;
}
