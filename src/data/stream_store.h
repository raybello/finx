#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include "data/types.h"

struct ParsedTable;

class StreamStore {
public:
    uint32_t add_csv(const std::string& name, const std::string& filename, const std::string& raw_text, const std::string& path = "");
    uint32_t add_csv_placeholder(const std::string& name, const std::string& filename, const std::string& path = "");
    uint32_t add_http(const std::string& name, const HttpSource& src);
    uint32_t add_formula(const std::string& name, const FormulaSource& src);
    bool     evaluate_formula(uint32_t id);
    void     reevaluate_dependents(uint32_t upstream_id);
    void     refresh(uint32_t id);
    void     remove(uint32_t id);
    DataStream* find(uint32_t id);
    std::vector<DataStream>& all() { return streams_; }
    void poll(); // call each frame — drains HTTP results

private:
    std::vector<DataStream> streams_;
    uint32_t next_id_ = 1;
    void apply_parsed(uint32_t id, ParsedTable&& t);
};
