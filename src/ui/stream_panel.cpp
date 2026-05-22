#include "ui/stream_panel.h"
#include "ui/modals.h"
#include "ui/formula_builder.h"
#include "app.h"
#include "data/stream_store.h"
#include "io/yfinance_client.h"
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
                       ImVec2(-1, ImGui::GetContentRegionAvail().y - 66.0f),
                       false);

    uint32_t delete_id       = 0;
    uint32_t refresh_id      = 0;
    uint32_t recalc_id       = 0;
    uint32_t edit_formula_id = 0;
    uint32_t edit_http_id    = 0;
    uint32_t edit_yf_id      = 0;

    for (auto& ds : streams) {
        // Icon tag
        const char* type_tag;
        switch (ds.source_type) {
            case SourceType::CSV_FILE: type_tag = "[CSV]";  break;
            case SourceType::HTTP_GET: type_tag = "[HTTP]"; break;
            case SourceType::YFINANCE: type_tag = "[YF]";   break;
            default:                   type_tag = "[EXPR]"; break;
        }

        // Status dot color
        ImVec4 dot_color;
        const char* dot_char = " *";
        switch (ds.status) {
            case StreamStatus::OK:          dot_color = {0.2f,0.9f,0.3f,1.0f}; break;
            case StreamStatus::LOADING:     dot_color = {0.9f,0.8f,0.1f,1.0f}; break;
            case StreamStatus::ERROR_STATE: dot_color = {0.9f,0.2f,0.2f,1.0f}; break;
            default:                        dot_color = {0.5f,0.5f,0.5f,1.0f}; break;
        }

        // Wrap the entire item (header row + expanded tree) in a group so that
        // right-clicking anywhere — header OR expanded content — triggers the
        // context menu via the group's bounding box.
        ImGui::BeginGroup();

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

        // Tooltip: show error if status is error
        if (ds.status == StreamStatus::ERROR_STATE && !ds.error_msg.empty()) {
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Error: %s", ds.error_msg.c_str());
            }
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
            if (ds.source_type == SourceType::FORMULA) {
                const auto& expr = ds.formula_source.expression;
                std::string preview = expr.size() > 60
                    ? expr.substr(0, 57) + "..."
                    : expr;
                ImGui::TextDisabled("  expr: %s", preview.c_str());
                for (const auto& b : ds.formula_source.bindings) {
                    ImGui::TextDisabled("    %s = field '%s'",
                                        b.alias.c_str(), b.field_name.c_str());
                }
            }
            if (ds.source_type == SourceType::YFINANCE) {
                ImGui::TextDisabled("  ticker:   %s", ds.yf_source.ticker.c_str());
                ImGui::TextDisabled("  period:   %s", ds.yf_source.period.c_str());
                ImGui::TextDisabled("  interval: %s", ds.yf_source.interval.c_str());
            }
            ImGui::TreePop();
        }

        ImGui::EndGroup();

        // Context menu: right-click anywhere on the whole group (header + tree content)
        char ctx_id[64];
        std::snprintf(ctx_id, sizeof(ctx_id), "##ctx%u", ds.id);
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup(ctx_id);
        }
        if (ImGui::BeginPopup(ctx_id)) {
            if (ds.source_type == SourceType::HTTP_GET) {
                if (ImGui::MenuItem("Refresh"))          refresh_id   = ds.id;
                if (ImGui::MenuItem("Edit Settings...")) edit_http_id = ds.id;
            }
            if (ds.source_type == SourceType::YFINANCE) {
                if (ImGui::MenuItem("Refresh"))          refresh_id = ds.id;
                if (ImGui::MenuItem("Edit Settings...")) edit_yf_id = ds.id;
            }
            if (ds.source_type == SourceType::FORMULA) {
                if (ImGui::MenuItem("Edit Formula"))   edit_formula_id = ds.id;
                if (ImGui::MenuItem("Recalculate"))    recalc_id       = ds.id;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete")) delete_id = ds.id;
            ImGui::EndPopup();
        }

        ImGui::Spacing();
    }

    ImGui::EndChild();

    // "+" button at the bottom
    ImGui::Separator();
    if (ImGui::Button("+ Add Stream", ImVec2(-1, 0))) {
        modals_request_add_stream();
    }
    if (ImGui::Button("+ Add Formula Stream", ImVec2(-1, 0))) {
        formula_builder_request_open(0);
    }

    if (delete_id)       app.stream_store.remove(delete_id);
    if (refresh_id)      app.stream_store.refresh(refresh_id);
    if (recalc_id)       app.stream_store.evaluate_formula(recalc_id);
    if (edit_formula_id) formula_builder_request_open(edit_formula_id);
    if (edit_http_id)    modals_request_edit_http(edit_http_id);
    if (edit_yf_id)      modals_request_edit_yfinance(edit_yf_id);

    ImGui::End();
}
