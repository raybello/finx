#include "ui/menu_bar.h"
#include "ui/modals.h"
#include "app.h"
#include "persist/config.h"
#include "imgui.h"
#include <string>

void render_menu_bar(App& app) {
    if (!ImGui::BeginMenuBar()) return;

    // ── File menu ────────────────────────────────────────────────────────────
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Stream", "Ctrl+N")) {
            modals_request_add_stream();
        }
        if (ImGui::MenuItem("New Plot", "Ctrl+P")) {
            static int plot_count = 0;
            ++plot_count;
            app.plot_store.add("Plot " + std::to_string(plot_count));
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save Config", "Ctrl+S")) {
            config_save(app);
        }
        if (ImGui::MenuItem("Load Config")) {
            config_load(app);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Export PNG (TODO)")) {
            // Phase 2
        }
        ImGui::EndMenu();
    }

    // ── View menu ────────────────────────────────────────────────────────────
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Reset Layout")) {
            // Force re-build dockspace layout on next frame
            // We communicate via a simple flag
            ImGui::LoadIniSettingsFromMemory("", 0); // clear ini → triggers first_time
        }
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}
