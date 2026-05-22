#pragma once
#include <string>
#include "data/types.h"

class StreamStore;
class PlotStore;

// Open the "Add Data Stream" modal on next frame
void modals_request_add_stream();

// True if a CSV file has been loaded from the browser file picker
bool modals_csv_pending();

// Consume the pending CSV (filename and raw text)
void modals_consume_csv(std::string& filename, std::string& raw);

// Open the "Export PNG" modal on next frame
void modals_request_export_png();

// True if the user confirmed a PNG save path
bool modals_png_path_ready();

// Consume the confirmed PNG save path
std::string modals_consume_png_path();

// HTTP draft: last-used values auto-saved to finx.json so they persist across sessions
HttpSource modals_get_http_draft();
void       modals_set_http_draft(const HttpSource& src);

// Render all modals — call each frame
void modals_render(StreamStore& ss, PlotStore& ps);
