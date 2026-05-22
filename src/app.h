#pragma once
#include "data/stream_store.h"
#include "data/plot_store.h"
#include "imgui.h"
#include <string>

class App {
public:
    void     init();
    void     render();    // called every frame
    void     shutdown();
    uint32_t new_plot();  // create a new plot with an auto-generated name

    StreamStore stream_store;
    PlotStore   plot_store;
    uint32_t    focused_plot_id = 0;
    ImGuiID     dock_center_id  = 0; // center dock node; set each frame

    // Non-empty signals main_loop to capture the framebuffer after next render.
    std::string pending_png_path;
};
