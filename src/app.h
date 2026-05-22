#pragma once
#include "data/stream_store.h"
#include "data/plot_store.h"
#include "imgui.h"
#include <string>
#include <unordered_set>

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

    std::unordered_set<uint32_t> open_tables; // stream IDs with open data table windows

    // Non-empty signals main_loop to capture the framebuffer after next render.
    std::string pending_png_path;
};
