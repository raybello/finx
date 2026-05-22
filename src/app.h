#pragma once
#include "data/stream_store.h"
#include "data/plot_store.h"

class App {
public:
    void init();
    void render();    // called every frame
    void shutdown();

    StreamStore stream_store;
    PlotStore   plot_store;
    uint32_t    focused_plot_id = 0;
};
