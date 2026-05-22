#include "ui/menu_bar.h"
#include "ui/modals.h"
#include "app.h"
#include "persist/config.h"
#include "imgui.h"
#include <string>

static bool g_show_help = false;

void render_menu_bar(App& app) {
    if (!ImGui::BeginMenuBar()) return;

    // ── File menu ────────────────────────────────────────────────────────────
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Stream", "Ctrl+N")) {
            modals_request_add_stream();
        }
        if (ImGui::MenuItem("New Plot", "Ctrl+P")) {
            app.new_plot();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save Config", "Ctrl+S")) {
            config_save(app);
        }
        if (ImGui::MenuItem("Load Config")) {
            config_load(app);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Export PNG", "Ctrl+E")) {
            modals_request_export_png();
        }
        ImGui::EndMenu();
    }

    // ── View menu ────────────────────────────────────────────────────────────
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Reset Layout")) {
            ImGui::LoadIniSettingsFromMemory("", 0);
        }
        ImGui::EndMenu();
    }

    // ── Help menu (right-aligned) ────────────────────────────────────────────
    float help_width = ImGui::CalcTextSize("Help").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         ImGui::GetContentRegionAvail().x - help_width);
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Keyboard Shortcuts & Guide"))
            g_show_help = true;
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

void render_help_modal() {
    if (g_show_help) {
        g_show_help = false;
        ImGui::OpenPopup("Help##keymaps");
    }

    ImGui::SetNextWindowSize(ImVec2(560, 480), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Help##keymaps", nullptr,
                                ImGuiWindowFlags_NoResize)) {
        return;
    }

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "FinX — Keyboard Shortcuts");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTable("##shortcuts", 2,
                           ImGuiTableFlags_Borders |
                           ImGuiTableFlags_RowBg   |
                           ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Action",   ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        auto row = [](const char* key, const char* desc) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", key);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(desc);
        };

        row("Ctrl+N", "New data stream (opens Add Stream dialog)");
        row("Ctrl+P", "New plot window");
        row("Ctrl+S", "Save configuration to finx.json");
        row("Ctrl+E", "Export current view as PNG");
        row("Double-click plot", "Fit all data in view");
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Workflow");
    ImGui::Spacing();

    ImGui::TextWrapped(
        "1. Load Data  —  Use File > New Stream (Ctrl+N) to add a CSV file, "
        "HTTP feed, yfinance ticker, or formula-derived stream. "
        "A new plot window is created automatically.");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "2. Configure Plot  —  Click a plot window to select it. "
        "The Plot Inspector (right panel) shows its settings. "
        "Add series, choose a stream, then pick X and Y fields. "
        "The plot auto-fits to show all data when fields are selected.");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "3. Explore  —  Scroll to zoom, drag to pan inside any plot. "
        "Double-click to reset the view. Right-click a stream in the "
        "left panel to refresh or edit its settings.");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "4. Save & Export  —  Ctrl+S saves your layout and streams to "
        "finx.json (auto-loaded on next launch). Ctrl+E exports the "
        "current view as a PNG image.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float btn_x = (ImGui::GetContentRegionAvail().x - 80.0f) * 0.5f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + btn_x);
    if (ImGui::Button("Close", ImVec2(80, 0)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}
