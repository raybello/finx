#pragma once
#include <string>
#include <functional>
#include <cstdint>
#include "data/types.h"

struct ParsedTable; // forward

// Callback receives (success, body_or_error)
using FetchCallback = std::function<void(bool ok, std::string body)>;

// Fires async GET request. Callback called on main thread (Emscripten) or queued for main thread (native).
void http_get_async(uint32_t stream_id, const std::string& url, FetchCallback cb);

// Called each frame from app to drain the result queue (native only; no-op on Emscripten)
void http_poll_results();

// Build URL from template + params (replace {key} with value)
std::string build_url(const std::string& url_template,
                      const std::vector<std::pair<std::string,std::string>>& params);

// Extract ParsedTable from JSON response given json_path and field_map
ParsedTable extract_json(const std::string& json_text,
                         const std::string& json_path,
                         const std::vector<FieldMapEntry>& field_map);

// Parse an HTTP response body using the format specified in src.
// AUTO sniffs the body: JSON-like first char ({/[) → extract_json, otherwise → parse_csv.
ParsedTable extract_response(const std::string& body, const HttpSource& src);
