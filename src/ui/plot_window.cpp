#include "ui/plot_window.h"
#include "app.h"
#include "data/stream_store.h"
#include "data/plot_store.h"
#include "imgui.h"
#include "implot.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cmath>

void render_plot_windows(App& app) {
    auto& plots = app.plot_store.all();

    for (auto& plot : plots) {
        if (!plot.open) continue;

        char win_title[128];
        std::snprintf(win_title, sizeof(win_title), "%s##plot_%u",
                      plot.name.c_str(), plot.id);

        ImGuiWindowFlags win_flags =
            ImGuiWindowFlags_NoScrollbar       |
            ImGuiWindowFlags_NoScrollWithMouse;

        ImGui::SetNextWindowSize(ImVec2(640, 400), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin(win_title, &plot.open, win_flags)) {
            ImGui::End();
            continue;
        }

        // Detect focus to update inspector
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            app.focused_plot_id = plot.id;
        }

        // ── ImPlot ───────────────────────────────────────────────────────────
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x < 10 || avail.y < 10) {
            ImGui::End();
            continue;
        }

        ImPlotFlags plot_flags = ImPlotFlags_None;
        if (!plot.show_legend) plot_flags |= ImPlotFlags_NoLegend;

        // Check if any series uses right Y axis
        bool has_y2 = false;
        for (const auto& ser : plot.series) {
            if (ser.y_axis == 1) { has_y2 = true; break; }
        }

        char plot_id[128];
        std::snprintf(plot_id, sizeof(plot_id), "##iplot_%u", plot.id);

        if (!ImPlot::BeginPlot(plot_id, avail, plot_flags)) {
            ImGui::End();
            continue;
        }

        // Setup axes
        const char* x_lbl  = plot.x_label.empty()  ? nullptr : plot.x_label.c_str();
        const char* y_lbl  = plot.y_label.empty()   ? nullptr : plot.y_label.c_str();
        const char* y2_lbl = plot.y2_label.empty()  ? nullptr : plot.y2_label.c_str();

        ImPlot::SetupAxes(x_lbl, y_lbl, ImPlotAxisFlags_None, ImPlotAxisFlags_None);

        if (has_y2) {
            ImPlot::SetupAxis(ImAxis_Y2, y2_lbl, ImPlotAxisFlags_AuxDefault);
        }

        // Detect if X axis should be time-scaled
        // Check first series that has valid data
        bool x_is_time = false;
        for (const auto& ser : plot.series) {
            DataStream* ds = app.stream_store.find(ser.stream_id);
            if (!ds || ser.x_field.empty()) continue;
            for (const auto& fd : ds->schema) {
                if (fd.name == ser.x_field && fd.type == FieldType::TIMESTAMP) {
                    x_is_time = true;
                    break;
                }
            }
            if (x_is_time) break;
        }
        if (x_is_time) {
            ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
        }

        // Setup must happen before PlotXxx calls
        ImPlot::SetupFinish();

        // Draw each series
        for (int si = 0; si < (int)plot.series.size(); ++si) {
            const PlotSeries& ser = plot.series[si];

            DataStream* ds = app.stream_store.find(ser.stream_id);
            if (!ds || ds->status != StreamStatus::OK) continue;
            if (ser.x_field.empty() || ser.y_field.empty()) continue;

            auto it_x = ds->columns.find(ser.x_field);
            auto it_y = ds->columns.find(ser.y_field);

            if (it_x == ds->columns.end() || it_y == ds->columns.end()) continue;

            const std::vector<double>& xs = it_x->second;
            const std::vector<double>& ys = it_y->second;

            if (xs.empty() || ys.empty()) continue;

            int count = static_cast<int>(std::min(xs.size(), ys.size()));

            // Label — use series label or auto
            std::string label = ser.label.empty()
                ? ("Series " + std::to_string(si + 1))
                : ser.label;
            const char* lbl = label.c_str();

            ImPlot::SetNextLineStyle(ser.color);
            ImPlot::SetNextFillStyle(ser.color, 0.25f);

            // Set axes for this series
            if (ser.y_axis == 1) {
                ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
            } else {
                ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
            }

            switch (ser.plot_type) {
                case PlotType::LINE:
                    ImPlot::PlotLine(lbl, xs.data(), ys.data(), count);
                    break;

                case PlotType::SCATTER:
                    ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 4.0f, ser.color, 1.0f, ser.color);
                    ImPlot::PlotScatter(lbl, xs.data(), ys.data(), count);
                    break;

                case PlotType::BAR: {
                    double bar_width = 0.5;
                    if (count > 1) {
                        bar_width = 0.67 * (xs[1] - xs[0]);
                        if (std::isnan(bar_width) || bar_width <= 0) bar_width = 0.5;
                    }
                    ImPlot::PlotBars(lbl, xs.data(), ys.data(), count, bar_width);
                    break;
                }

                case PlotType::STEP:
                    ImPlot::PlotStairs(lbl, xs.data(), ys.data(), count);
                    break;
            }
        }

        ImPlot::EndPlot();
        ImGui::End();
    }

    // Remove closed plots
    auto& all_plots = app.plot_store.all();
    for (int i = (int)all_plots.size() - 1; i >= 0; --i) {
        if (!all_plots[i].open) {
            if (app.focused_plot_id == all_plots[i].id) {
                app.focused_plot_id = 0;
            }
            app.plot_store.remove(all_plots[i].id);
        }
    }
}
