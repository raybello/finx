#include "app.h"
#include "ui/menu_bar.h"
#include "ui/stream_panel.h"
#include "ui/plot_window.h"
#include "ui/plot_inspector.h"
#include "ui/modals.h"
#include "ui/formula_builder.h"
#include "io/http_client.h"
#include "io/yfinance_client.h"
#include "persist/config.h"
#include "imgui.h"
#include "imgui_internal.h"

// ── Dockspace layout constants ─────────────────────────────────────────────
static constexpr float kStreamPanelWidth   = 220.0f;
static constexpr float kInspectorWidth     = 280.0f;

void App::init() {
    yfinance_init();
    config_load(*this);
}

void App::shutdown() {
    config_save(*this);
    yfinance_shutdown();
}

void App::render() {
    // Drain HTTP result queue
    stream_store.poll();

    // Create full-viewport dockspace
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoTitleBar        |
        ImGuiWindowFlags_NoCollapse        |
        ImGuiWindowFlags_NoResize          |
        ImGuiWindowFlags_NoMove            |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoDocking         |
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0,0));
    ImGui::Begin("##DockHost", nullptr, host_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");

    // Build default layout once
    static bool first_time = true;
    if (first_time) {
        first_time = false;
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

        ImGuiID dock_main  = dockspace_id;
        ImGuiID dock_left  = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, kStreamPanelWidth / viewport->WorkSize.x, nullptr, &dock_main);
        ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, kInspectorWidth / (viewport->WorkSize.x - kStreamPanelWidth), nullptr, &dock_main);

        ImGui::DockBuilderDockWindow("Data Streams##panel",  dock_left);
        ImGui::DockBuilderDockWindow("Plot Inspector##panel", dock_right);
        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::DockSpace(dockspace_id, ImVec2(0,0), ImGuiDockNodeFlags_PassthruCentralNode);

    // Menu bar rendered inside the host window
    render_menu_bar(*this);

    ImGui::End();

    // Side panels
    render_stream_panel(*this);
    render_plot_inspector(*this);

    // Plot windows
    render_plot_windows(*this);

    // Modals
    modals_render(stream_store, plot_store);
    formula_builder_render(*this);
    if (modals_png_path_ready()) {
        pending_png_path = modals_consume_png_path();
    }
}
