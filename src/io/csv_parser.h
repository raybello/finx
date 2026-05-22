#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "data/types.h"

struct ParsedTable {
    std::vector<FieldDef> schema;
    std::unordered_map<std::string, std::vector<double>>      columns;
    std::unordered_map<std::string, std::vector<std::string>> str_columns;
    size_t row_count = 0;
    std::string error;
};

ParsedTable parse_csv(const std::string& text);
