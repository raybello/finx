#pragma once
class App;

void load_sample_defaults(App& app);

// Returns the raw text of the embedded sample CSV, or nullptr if name doesn't match.
const char* get_embedded_csv(const char* filename);
