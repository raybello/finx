#include "ui/stream_panel.h"
#include "ui/modals.h"
#include "ui/formula_builder.h"
#include "app.h"
#include "data/stream_store.h"
#include "io/csv_exporter.h"
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
    uint32_t view_table_id   = 0;
    uint32_t export_csv_id   = 0;

    // Brief export status message: (message, expiry_time)
    static char    s_export_msg[256]  = {};
    static double  s_export_msg_time  = -999.0;

    for (auto& ds : streams) {
        const char* type_tag;
        switch (ds.source_type) {
            case SourceType::CSV_FILE: type_tag = "[CSV]";  break;
            case SourceType::HTTP_GET: type_tag = "[HTTP]"; break;
            case SourceType::YFINANCE: type_tag = "[YF]";   break;
            default:                   type_tag = "[EXPR]"; break;
        }

        ImVec4 dot_color;
        switch (ds.status) {
            case StreamStatus::OK:          dot_color = {0.2f, 0.9f, 0.3f, 1.0f}; break;
            case StreamStatus::LOADING:     dot_color = {0.9f, 0.8f, 0.1f, 1.0f}; break;
            case StreamStatus::ERROR_STATE: dot_color = {0.9f, 0.2f, 0.2f, 1.0f}; break;
            default:                        dot_color = {0.5f, 0.5f, 0.5f, 1.0f}; break;
        }

        char label[256];
        std::snprintf(label, sizeof(label), "%s  %s##sid%u",
                      type_tag, ds.name.c_str(), ds.id);

        bool open = ImGui::CollapsingHeader(label);

        // Status dot — drawn on top of the header's right edge via draw list
        {
            float cy = ImGui::GetItemRectMin().y + ImGui::GetItemRectSize().y * 0.5f;
            float cx = ImGui::GetItemRectMax().x - 14.0f;
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(cx, cy), 5.0f,
                ImGui::ColorConvertFloat4ToU32(dot_color));
        }

        // Error tooltip
        if (ds.status == StreamStatus::ERROR_STATE && !ds.error_msg.empty()) {
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Error: %s", ds.error_msg.c_str());
        }

        // Context menu on right-click
        char ctx_id[64];
        std::snprintf(ctx_id, sizeof(ctx_id), "##ctx%u", ds.id);
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup(ctx_id);
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
                if (ImGui::MenuItem("Edit Formula")) edit_formula_id = ds.id;
                if (ImGui::MenuItem("Recalculate")) recalc_id        = ds.id;
            }
            ImGui::Separator();
            if (ds.status == StreamStatus::OK) {
                if (ImGui::MenuItem("View Data Table")) view_table_id = ds.id;
#ifndef __EMSCRIPTEN__
                if (ImGui::MenuItem("Export to CSV"))   export_csv_id = ds.id;
#endif
                ImGui::Separator();
            }
            if (ImGui::MenuItem("Delete")) delete_id = ds.id;
            ImGui::EndPopup();
        }

        // Expanded schema content
        if (open) {
            ImGui::Indent();

            char info[128];
            std::snprintf(info, sizeof(info), "%zu rows, %zu fields",
                          ds.row_count, ds.schema.size());
            ImGui::TextDisabled("%s", info);

            for (const auto& fd : ds.schema)
                ImGui::TextDisabled("  %s [%s]", fd.name.c_str(), field_type_label(fd.type));

            if (ds.source_type == SourceType::FORMULA) {
                const auto& expr = ds.formula_source.expression;
                std::string preview = expr.size() > 60
                    ? expr.substr(0, 57) + "..."
                    : expr;
                ImGui::TextDisabled("  expr: %s", preview.c_str());
                for (const auto& b : ds.formula_source.bindings)
                    ImGui::TextDisabled("    %s = field '%s'",
                                        b.alias.c_str(), b.field_name.c_str());
            }

            if (ds.source_type == SourceType::YFINANCE) {
                ImGui::TextDisabled("  ticker:   %s", ds.yf_source.ticker.c_str());
                ImGui::TextDisabled("  period:   %s", ds.yf_source.period.c_str());
                ImGui::TextDisabled("  interval: %s", ds.yf_source.interval.c_str());
            }

            ImGui::Unindent();
        }

        ImGui::Spacing();
    }

    ImGui::EndChild();

    // Buttons at the bottom
    ImGui::Separator();
    if (ImGui::Button("+ Add Stream", ImVec2(-1, 0)))
        modals_request_add_stream();
    if (ImGui::Button("+ Add Formula Stream", ImVec2(-1, 0)))
        formula_builder_request_open(0);

    if (delete_id)       app.stream_store.remove(delete_id);
    if (refresh_id)      app.stream_store.refresh(refresh_id);
    if (recalc_id)       app.stream_store.evaluate_formula(recalc_id);
    if (edit_formula_id) formula_builder_request_open(edit_formula_id);
    if (edit_http_id)    modals_request_edit_http(edit_http_id);
    if (edit_yf_id)      modals_request_edit_yfinance(edit_yf_id);
    if (view_table_id)   app.open_tables.insert(view_table_id);

#ifndef __EMSCRIPTEN__
    if (export_csv_id) {
        DataStream* eds = app.stream_store.find(export_csv_id);
        if (eds) {
            std::string path = csv_export_default_path(*eds);
            bool ok = export_stream_csv(*eds, path);
            std::snprintf(s_export_msg, sizeof(s_export_msg),
                          ok ? "Exported: %s" : "Export failed: %s", path.c_str());
            s_export_msg_time = ImGui::GetTime();
        }
    }
    if (s_export_msg[0] && (ImGui::GetTime() - s_export_msg_time) < 4.0) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "%s", s_export_msg);
    }
#endif

    ImGui::End();
}
