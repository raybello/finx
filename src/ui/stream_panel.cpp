#include "ui/stream_panel.h"
#include "ui/modals.h"
#include "app.h"
#include "data/stream_store.h"
#include "imgui.h"
#include <cstdio>
#include <string>

static const char* field_type_label(FieldType t) {
    switch (t) {
        case FieldType::NUMBER:    return "num";
        case FieldType::TIMESTAMP: return "ts";
        case FieldType::STRING:    return "str";
    }
    return "?";
}

void render_stream_panel(App& app) {
    ImGui::SetNextWindowSize(ImVec2(220, -1), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar    |
        ImGuiWindowFlags_NoScrollWithMouse;

    if (!ImGui::Begin("Data Streams##panel", nullptr, flags)) {
        ImGui::End();
        return;
    }

    auto& streams = app.stream_store.all();

    ImGui::BeginChild("##stream_list",
                       ImVec2(-1, ImGui::GetContentRegionAvail().y - 36.0f),
                       false);

    uint32_t delete_id  = 0;
    uint32_t refresh_id = 0;

    for (auto& ds : streams) {
        // Icon tag
        const char* type_tag = (ds.source_type == SourceType::CSV_FILE) ? "[CSV]" : "[HTTP]";

        // Status dot color
        ImVec4 dot_color;
        const char* dot_char = " *";
        switch (ds.status) {
            case StreamStatus::OK:          dot_color = {0.2f,0.9f,0.3f,1.0f}; break;
            case StreamStatus::LOADING:     dot_color = {0.9f,0.8f,0.1f,1.0f}; break;
            case StreamStatus::ERROR_STATE: dot_color = {0.9f,0.2f,0.2f,1.0f}; break;
            default:                        dot_color = {0.5f,0.5f,0.5f,1.0f}; break;
        }

        // Selectable row
        char label[256];
        std::snprintf(label, sizeof(label), "%s %s##sid%u",
                      type_tag, ds.name.c_str(), ds.id);

        bool selected = false;
        ImGui::Selectable(label, &selected,
                          ImGuiSelectableFlags_AllowDoubleClick,
                          ImVec2(ImGui::GetContentRegionAvail().x - 20.0f, 0));

        // Status dot on the same line
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 4.0f);
        ImGui::TextColored(dot_color, "*");

        // Context menu
        if (ImGui::BeginPopupContextItem(label)) {
            if (ds.source_type == SourceType::HTTP_GET) {
                if (ImGui::MenuItem("Refresh")) {
                    refresh_id = ds.id;
                }
            }
            if (ImGui::MenuItem("Delete")) {
                delete_id = ds.id;
            }
            ImGui::EndPopup();
        }

        // Tooltip: show error if status is error
        if (ds.status == StreamStatus::ERROR_STATE && !ds.error_msg.empty()) {
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Error: %s", ds.error_msg.c_str());
            }
        }

        // Expanded info
        if (selected || ImGui::IsItemClicked()) {
            // handled below via TreeNode if double-clicked
        }

        // Show schema info in an indented tree node
        char tree_id[64];
        std::snprintf(tree_id, sizeof(tree_id), "##tree%u", ds.id);
        if (ImGui::TreeNode(tree_id)) {
            char info[128];
            std::snprintf(info, sizeof(info), "%zu rows, %zu fields",
                          ds.row_count, ds.schema.size());
            ImGui::TextDisabled("%s", info);
            for (const auto& fd : ds.schema) {
                ImGui::TextDisabled("  %s [%s]", fd.name.c_str(), field_type_label(fd.type));
            }
            ImGui::TreePop();
        }

        ImGui::Spacing();
    }

    ImGui::EndChild();

    // "+" button at the bottom
    ImGui::Separator();
    if (ImGui::Button("+ Add Stream", ImVec2(-1, 0))) {
        modals_request_add_stream();
    }

    if (delete_id)  app.stream_store.remove(delete_id);
    if (refresh_id) app.stream_store.refresh(refresh_id);

    ImGui::End();
}
