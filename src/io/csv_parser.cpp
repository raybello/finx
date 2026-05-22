#include "io/csv_parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <regex>

// ── Delimiter detection ────────────────────────────────────────────────

static char detect_delimiter(const std::string& first_line) {
    int commas    = 0;
    int tabs      = 0;
    int semicolons = 0;
    bool in_quote = false;
    for (char c : first_line) {
        if (c == '"') { in_quote = !in_quote; continue; }
        if (in_quote) continue;
        if (c == ',')  ++commas;
        if (c == '\t') ++tabs;
        if (c == ';')  ++semicolons;
    }
    if (tabs >= commas && tabs >= semicolons) return '\t';
    if (semicolons > commas) return ';';
    return ',';
}

// ── RFC 4180 row splitter ──────────────────────────────────────────────

static std::vector<std::string> split_row(const std::string& line, char delim) {
    std::vector<std::string> fields;
    std::string field;
    bool in_quote = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            if (in_quote && i + 1 < line.size() && line[i+1] == '"') {
                field += '"';
                ++i;
            } else {
                in_quote = !in_quote;
            }
        } else if (c == delim && !in_quote) {
            fields.push_back(field);
            field.clear();
        } else {
            field += c;
        }
    }
    fields.push_back(field);
    return fields;
}

// ── Timestamp parsing ──────────────────────────────────────────────────

// Attempt to parse ISO-8601 date like YYYY-MM-DD or YYYY-MM-DDTHH:MM:SS
// Returns epoch seconds, or NaN on failure.
static double try_parse_timestamp(const std::string& s) {
    if (s.size() >= 10 && s[4] == '-' && s[7] == '-') {
        // Looks like YYYY-MM-DD[THH:MM:SS[Z]]
        struct tm t{};
        t.tm_year = std::stoi(s.substr(0,4)) - 1900;
        t.tm_mon  = std::stoi(s.substr(5,2)) - 1;
        t.tm_mday = std::stoi(s.substr(8,2));
        if (s.size() >= 19 && s[10] == 'T') {
            t.tm_hour = std::stoi(s.substr(11,2));
            t.tm_min  = std::stoi(s.substr(14,2));
            t.tm_sec  = std::stoi(s.substr(17,2));
        }
        t.tm_isdst = -1;
#ifdef _WIN32
        time_t epoch = _mkgmtime(&t);
#else
        time_t epoch = timegm(&t);
#endif
        if (epoch != -1) return static_cast<double>(epoch);
    }
    return std::numeric_limits<double>::quiet_NaN();
}

// ── Type heuristic ─────────────────────────────────────────────────────

enum class CellType { NUMBER, TIMESTAMP, STRING };

static CellType detect_type(const std::vector<std::string>& samples) {
    int num_count = 0, ts_count = 0, str_count = 0;
    for (const auto& s : samples) {
        if (s.empty()) continue;
        // Try number first
        char* end = nullptr;
        double v = std::strtod(s.c_str(), &end);
        if (end != s.c_str() && *end == '\0') {
            // Could be a unix epoch ms or s
            if (v > 1e12) {
                ++ts_count; // ms epoch
            } else if (v > 1e9 && v < 3e9) {
                ++ts_count; // s epoch
            } else {
                ++num_count;
            }
        } else {
            // Try ISO date
            double ts = try_parse_timestamp(s);
            if (!std::isnan(ts)) {
                ++ts_count;
            } else {
                ++str_count;
            }
        }
    }
    if (ts_count > num_count && ts_count > str_count) return CellType::TIMESTAMP;
    if (num_count >= str_count) return CellType::NUMBER;
    return CellType::STRING;
}

// ── Line splitter (handles \r\n and \n) ────────────────────────────────

static std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            size_t end = i;
            if (end > start && text[end-1] == '\r') --end;
            lines.emplace_back(text.substr(start, end - start));
            start = i + 1;
        }
    }
    if (start < text.size()) {
        std::string last = text.substr(start);
        if (!last.empty() && last.back() == '\r') last.pop_back();
        if (!last.empty()) lines.push_back(last);
    }
    return lines;
}

// ── Main parser ────────────────────────────────────────────────────────

ParsedTable parse_csv(const std::string& text) {
    ParsedTable result;

    std::vector<std::string> lines = split_lines(text);
    if (lines.empty()) {
        result.error = "Empty input";
        return result;
    }

    char delim = detect_delimiter(lines[0]);
    std::vector<std::string> headers = split_row(lines[0], delim);

    if (headers.empty()) {
        result.error = "No columns found";
        return result;
    }

    // Trim whitespace from headers
    for (auto& h : headers) {
        while (!h.empty() && std::isspace((unsigned char)h.front())) h.erase(h.begin());
        while (!h.empty() && std::isspace((unsigned char)h.back()))  h.pop_back();
        if (h.empty()) h = "col_" + std::to_string(&h - &headers[0]);
    }

    size_t num_cols = headers.size();

    // Collect up to 50 sample rows for type detection
    size_t sample_end = std::min(lines.size(), (size_t)51);
    std::vector<std::vector<std::string>> raw_cols(num_cols);
    for (size_t row = 1; row < sample_end; ++row) {
        if (lines[row].empty()) continue;
        auto cells = split_row(lines[row], delim);
        for (size_t c = 0; c < num_cols; ++c) {
            if (c < cells.size()) raw_cols[c].push_back(cells[c]);
        }
    }

    // Detect types
    std::vector<CellType> types(num_cols);
    for (size_t c = 0; c < num_cols; ++c) {
        types[c] = detect_type(raw_cols[c]);
    }

    // Build schema
    for (size_t c = 0; c < num_cols; ++c) {
        FieldDef fd;
        fd.name = headers[c];
        switch (types[c]) {
            case CellType::NUMBER:    fd.type = FieldType::NUMBER;    break;
            case CellType::TIMESTAMP: fd.type = FieldType::TIMESTAMP; break;
            case CellType::STRING:    fd.type = FieldType::STRING;    break;
        }
        result.schema.push_back(fd);
    }

    // Parse all rows
    for (size_t row = 1; row < lines.size(); ++row) {
        if (lines[row].empty()) continue;
        auto cells = split_row(lines[row], delim);

        for (size_t c = 0; c < num_cols; ++c) {
            std::string cell = (c < cells.size()) ? cells[c] : "";
            // Trim
            while (!cell.empty() && std::isspace((unsigned char)cell.front())) cell.erase(cell.begin());
            while (!cell.empty() && std::isspace((unsigned char)cell.back()))  cell.pop_back();

            const std::string& col_name = headers[c];

            if (types[c] == CellType::STRING) {
                result.str_columns[col_name].push_back(cell);
            } else if (types[c] == CellType::TIMESTAMP) {
                double ts = std::numeric_limits<double>::quiet_NaN();
                if (!cell.empty()) {
                    char* end = nullptr;
                    double v = std::strtod(cell.c_str(), &end);
                    if (end != cell.c_str() && *end == '\0') {
                        if (v > 1e12) ts = v / 1000.0; // ms to s
                        else          ts = v;
                    } else {
                        ts = try_parse_timestamp(cell);
                    }
                }
                result.columns[col_name].push_back(std::isnan(ts) ? 0.0 : ts);
            } else {
                // NUMBER
                double v = 0.0;
                if (!cell.empty()) {
                    char* end = nullptr;
                    v = std::strtod(cell.c_str(), &end);
                    if (end == cell.c_str()) v = 0.0;
                }
                result.columns[col_name].push_back(v);
            }
        }
        ++result.row_count;
    }

    return result;
}
