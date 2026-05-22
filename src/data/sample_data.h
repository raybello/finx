#pragma once
class App;

// Loads the built-in sample CSV stream + a default plot.
// Called by config_load() when no saved project is found.
void load_sample_defaults(App& app);
