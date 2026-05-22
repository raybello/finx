#pragma once
#include <string>

class StreamStore;
class PlotStore;

// Open the "Add Data Stream" modal on next frame
void modals_request_add_stream();

// True if a CSV file has been loaded from the browser file picker
bool modals_csv_pending();

// Consume the pending CSV (filename and raw text)
void modals_consume_csv(std::string& filename, std::string& raw);

// Render all modals — call each frame
void modals_render(StreamStore& ss, PlotStore& ps);
