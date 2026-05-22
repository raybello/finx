#pragma once
#include "data/types.h"
#include <string>

// Write ds to a CSV file at path. Returns true on success.
bool export_stream_csv(const DataStream& ds, const std::string& path);

// Returns a safe default export filename derived from the stream name.
std::string csv_export_default_path(const DataStream& ds);
