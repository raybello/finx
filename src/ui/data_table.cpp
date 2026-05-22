#include "ui/data_table.h"
#include "app.h"
#include "data/stream_store.h"
#include "data/types.h"
#include "imgui.h"
#include <cstdio>
#include <ctime>
#include <vector>
#include <string>

void render_data_tables(App& app) {
    std::vector<uint32_t> to_close;

    for (uint32_t id : app.open_tables) {
        DataStream* ds = app.stream_store.find(id);
        if (!ds) { to_close.push_back(id); continue; }

        char win_title[256];
        std::snprintf(win_title, sizeof(win_title),
                      "Data: %s###dtable_%u", ds->name.c_str(), id);

        bool open = true;
        ImGui::SetNextWindowSize(ImVec2(720, 420), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(win_title, &open)) {
            ImGui::End();
            if (!open) to_close.push_back(id);
            continue;
        }

        ImGui::TextDisabled("%zu rows  |  %zu columns  |  %s",
                            ds->row_count, ds->schema.size(),
                            ds->status == StreamStatus::OK ? "OK" :
                            ds->status == StreamStatus::LOADING ? "Loading..." : "Error");
        ImGui::Separator();

        if (ds->schema.empty() || ds->status != StreamStatus::OK) {
            ImGui::TextDisabled("No data available.");
            ImGui::End();
            if (!open) to_close.push_back(id);
            continue;
        }

        ImGuiTableFlags tbl_flags =
            ImGuiTableFlags_BordersOuter  |
            ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_RowBg         |
            ImGuiTableFlags_ScrollX       |
            ImGuiTableFlags_ScrollY       |
            ImGuiTableFlags_SizingFixedFit;

        ImVec2 tbl_size = ImGui::GetContentRegionAvail();
        int ncols = (int)ds->schema.size();

        if (ImGui::BeginTable("##dt", ncols, tbl_flags, tbl_size)) {
            for (const auto& fd : ds->schema) {
                const char* type_suffix = (fd.type == FieldType::TIMESTAMP) ? " [ts]"
                                        : (fd.type == FieldType::STRING)    ? " [str]"
                                        : "";
                char hdr[128];
                std::snprintf(hdr, sizeof(hdr), "%s%s", fd.name.c_str(), type_suffix);
                ImGui::TableSetupColumn(hdr, ImGuiTableColumnFlags_None, 120.0f);
            }
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            ImGuiListClipper clipper;
            clipper.Begin((int)ds->row_count);
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    ImGui::TableNextRow();
                    for (const auto& fd : ds->schema) {
                        ImGui::TableNextColumn();
                        if (fd.type == FieldType::STRING) {
                            auto it = ds->str_columns.find(fd.name);
                            if (it != ds->str_columns.end() &&
                                row < (int)it->second.size())
                                ImGui::TextUnformatted(it->second[row].c_str());
                        } else if (fd.type == FieldType::TIMESTAMP) {
                            auto it = ds->columns.find(fd.name);
                            if (it != ds->columns.end() &&
                                row < (int)it->second.size()) {
                                time_t t = (time_t)it->second[row];
                                char buf[32];
                                strftime(buf, sizeof(buf), "%Y-%m-%d", gmtime(&t));
                                ImGui::TextUnformatted(buf);
                            }
                        } else {
                            auto it = ds->columns.find(fd.name);
                            if (it != ds->columns.end() &&
                                row < (int)it->second.size()) {
                                ImGui::Text("%.6g", it->second[row]);
                            }
                        }
                    }
                }
            }
            clipper.End();
            ImGui::EndTable();
        }

        ImGui::End();
        if (!open) to_close.push_back(id);
    }

    for (uint32_t id : to_close) app.open_tables.erase(id);
}
