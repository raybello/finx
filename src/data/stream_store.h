#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include "data/types.h"

struct ParsedTable;

class StreamStore {
public:
    uint32_t add_csv(const std::string& name, const std::string& filename, const std::string& raw_text);
    uint32_t add_http(const std::string& name, const HttpSource& src);
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
