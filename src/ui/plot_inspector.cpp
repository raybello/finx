#include "ui/plot_inspector.h"
#include "app.h"
#include "data/stream_store.h"
#include "data/plot_store.h"
#include "imgui.h"
#include "implot.h"
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <algorithm>

// Helper: build a NUL-separated combo string from a vector of strings.
// Returns a std::string with NUL chars, terminated by double NUL.
static std::string build_combo_str(const std::vector<std::string>& items) {
    std::string s;
    for (const auto& item : items) {
        s += item;
        s += '\0';
    }
    s += '\0';
    return s;
}

// Find the index of a string in a vector, returns -1 if not found
static int find_index(const std::vector<std::string>& v, const std::string& s) {
    for (int i = 0; i < (int)v.size(); ++i) {
        if (v[i] == s) return i;
    }
    return -1;
}

void render_plot_inspector(App& app) {
    ImGui::SetNextWindowSize(ImVec2(280, -1), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Plot Inspector##panel")) {
        ImGui::End();
        return;
    }

    Plot* plot = nullptr;
    if (app.focused_plot_id != 0) {
        plot = app.plot_store.find(app.focused_plot_id);
    }

    if (!plot) {
        ImGui::TextDisabled("Select a plot window to inspect.");
        ImGui::End();
        return;
    }

    // ── Plot metadata ────────────────────────────────────────────────────────
    ImGui::Text("Plot: %s (id=%u)", plot->name.c_str(), plot->id);
    ImGui::Separator();

    // Name
    {
        static char name_buf[128];
        // Sync buffer with plot name whenever plot changes
        static uint32_t last_plot_id = 0;
        if (last_plot_id != plot->id) {
            last_plot_id = plot->id;
            std::strncpy(name_buf, plot->name.c_str(), sizeof(name_buf) - 1);
            name_buf[sizeof(name_buf) - 1] = '\0';
        }
        ImGui::Text("Name");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##plot_name", name_buf, sizeof(name_buf))) {
            plot->name = name_buf;
        }
    }

    // Axis labels
    {
        static char x_buf[128]  = {};
        static char y_buf[128]  = {};
        static char y2_buf[128] = {};
        static uint32_t last_id = 0;
        if (last_id != plot->id) {
            last_id = plot->id;
            std::strncpy(x_buf,  plot->x_label.c_str(),  sizeof(x_buf)  - 1);
            std::strncpy(y_buf,  plot->y_label.c_str(),  sizeof(y_buf)  - 1);
            std::strncpy(y2_buf, plot->y2_label.c_str(), sizeof(y2_buf) - 1);
        }
        ImGui::Text("X Label");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##xlbl", x_buf, sizeof(x_buf)))   plot->x_label  = x_buf;
        ImGui::Text("Y Label");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##ylbl", y_buf, sizeof(y_buf)))   plot->y_label  = y_buf;
        ImGui::Text("Y2 Label");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##y2lbl", y2_buf, sizeof(y2_buf))) plot->y2_label = y2_buf;
    }

    ImGui::Checkbox("Show Legend", &plot->show_legend);

    ImGui::Separator();
    ImGui::Text("Series");
    ImGui::Spacing();

    // Build stream list for combo
    std::vector<std::string> stream_names;
    std::vector<uint32_t>    stream_ids;
    for (const auto& ds : app.stream_store.all()) {
        stream_names.push_back(ds.name);
        stream_ids.push_back(ds.id);
    }
    std::string stream_combo = build_combo_str(stream_names);

    static const char* plot_types_str = "Line\0Scatter\0Bar\0Step\0\0";

    int delete_series = -1;

    for (int i = 0; i < (int)plot->series.size(); ++i) {
        PlotSeries& ser = plot->series[i];

        ImGui::PushID(i);
        char header[64];
        std::snprintf(header, sizeof(header), "Series %d: %s##sh", i + 1, ser.label.empty() ? "(unlabeled)" : ser.label.c_str());

        if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
            // Stream selector
            ImGui::Text("Stream");
            ImGui::SameLine();
            int stream_idx = -1;
            for (int si = 0; si < (int)stream_ids.size(); ++si) {
                if (stream_ids[si] == ser.stream_id) { stream_idx = si; break; }
            }
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##stream_sel", &stream_idx,
                             stream_combo.c_str())) {
                if (stream_idx >= 0 && stream_idx < (int)stream_ids.size()) {
                    ser.stream_id = stream_ids[stream_idx];
                    ser.x_field.clear();
                    ser.y_field.clear();
                }
            }

            // Get field lists for selected stream
            std::vector<std::string> numeric_fields;
            std::vector<std::string> x_fields; // NUMBER + TIMESTAMP
            DataStream* ds_sel = nullptr;
            if (stream_idx >= 0) ds_sel = app.stream_store.find(stream_ids[stream_idx]);
            if (ds_sel) {
                for (const auto& fd : ds_sel->schema) {
                    if (fd.type == FieldType::NUMBER || fd.type == FieldType::TIMESTAMP) {
                        x_fields.push_back(fd.name);
                    }
                    if (fd.type == FieldType::NUMBER) {
                        numeric_fields.push_back(fd.name);
                    }
                }
            }

            std::string x_combo = build_combo_str(x_fields);
            std::string y_combo = build_combo_str(numeric_fields);

            // X field
            ImGui::Text("X Field");
            ImGui::SameLine();
            int x_idx = find_index(x_fields, ser.x_field);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##xfield", &x_idx, x_combo.c_str())) {
                if (x_idx >= 0 && x_idx < (int)x_fields.size()) {
                    ser.x_field = x_fields[x_idx];
                }
            }

            // Y field
            ImGui::Text("Y Field");
            ImGui::SameLine();
            int y_idx = find_index(numeric_fields, ser.y_field);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##yfield", &y_idx, y_combo.c_str())) {
                if (y_idx >= 0 && y_idx < (int)numeric_fields.size()) {
                    ser.y_field = numeric_fields[y_idx];
                }
            }

            // Plot type
            ImGui::Text("Type");
            ImGui::SameLine();
            int pt_idx = static_cast<int>(ser.plot_type);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##ptype", &pt_idx, plot_types_str)) {
                ser.plot_type = static_cast<PlotType>(pt_idx);
            }

            // Label
            {
                static char lbl_bufs[64][64] = {};
                int bi = i % 64;
                static uint32_t lbl_plot_id[64] = {};
                if (lbl_plot_id[bi] != plot->id) {
                    lbl_plot_id[bi] = plot->id;
                    std::strncpy(lbl_bufs[bi], ser.label.c_str(), sizeof(lbl_bufs[bi]) - 1);
                }
                ImGui::Text("Label");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputText("##ser_label", lbl_bufs[bi], sizeof(lbl_bufs[bi]))) {
                    ser.label = lbl_bufs[bi];
                }
            }

            // Color
            ImGui::Text("Color");
            ImGui::SameLine();
            ImGui::ColorEdit4("##ser_color", &ser.color.x,
                              ImGuiColorEditFlags_NoInputs |
                              ImGuiColorEditFlags_NoLabel);

            // Y Axis
            ImGui::Text("Y Axis");
            ImGui::SameLine();
            if (ImGui::RadioButton("Left##ya",  ser.y_axis == 0)) ser.y_axis = 0;
            ImGui::SameLine();
            if (ImGui::RadioButton("Right##ya", ser.y_axis == 1)) ser.y_axis = 1;

            // Delete button
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
            if (ImGui::SmallButton("Delete Series")) delete_series = i;
            ImGui::PopStyleColor();
        }

        ImGui::PopID();
        ImGui::Spacing();
    }

    if (delete_series >= 0) {
        plot->series.erase(plot->series.begin() + delete_series);
    }

    ImGui::Separator();
    if (ImGui::Button("+ Add Series", ImVec2(-1, 0))) {
        PlotSeries s;
        // Auto-pick a color from ImPlot colormap
        int color_idx = static_cast<int>(plot->series.size());
        ImVec4 col = ImPlot::GetColormapColor(color_idx, ImPlotColormap_Deep);
        s.color     = col;
        s.label     = "Series " + std::to_string(plot->series.size() + 1);
        plot->series.push_back(s);
    }

    ImGui::End();
}
