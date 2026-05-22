#pragma once
#include "data/stream_store.h"
#include "data/plot_store.h"
#include <string>

class App {
public:
    void init();
    void render();    // called every frame
    void shutdown();

    StreamStore stream_store;
    PlotStore   plot_store;
    uint32_t    focused_plot_id = 0;

    // Non-empty signals main_loop to capture the framebuffer after next render.
    std::string pending_png_path;
};
