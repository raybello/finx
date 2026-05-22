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

    ImGui::SetNextWindowSize(ImVec2(700, 580), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Help##keymaps", nullptr,
                                ImGuiWindowFlags_NoResize)) {
        return;
    }

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "FinX — Help");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTabBar("##help_tabs")) {

        // ── Tab 1: Shortcuts & Workflow ──────────────────────────────────────
        if (ImGui::BeginTabItem("Shortcuts & Workflow")) {
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.5f, 1.0f), "Keyboard Shortcuts");
            ImGui::Spacing();

            if (ImGui::BeginTable("##shortcuts", 2,
                                   ImGuiTableFlags_Borders |
                                   ImGuiTableFlags_RowBg   |
                                   ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Action",   ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                auto row = [](const char* key, const char* desc) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", key);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(desc);
                };

                row("Ctrl+N",         "New data stream (opens Add Stream dialog)");
                row("Ctrl+P",         "New plot window");
                row("Ctrl+S",         "Save configuration to finx.json");
                row("Ctrl+E",         "Export current view as PNG");
                row("Double-click",   "Fit all data in view (inside a plot)");
                row("Scroll",         "Zoom in / out inside a plot");
                row("Drag",           "Pan the plot view");
                row("Right-click",    "Stream context menu (refresh, export, data table…)");
                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.5f, 1.0f), "Workflow");
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
                "3. Explore  —  Hover over a plot to see a crosshair tooltip with "
                "exact Y values for each series. Scroll to zoom, drag to pan. "
                "Right-click a stream in the left panel to refresh, view the raw "
                "data table, or export to CSV.");
            ImGui::Spacing();
            ImGui::TextWrapped(
                "4. Save & Export  —  Ctrl+S saves your layout and streams to "
                "finx.json (auto-loaded on next launch). Ctrl+E exports the "
                "current view as a PNG image.");

            ImGui::EndTabItem();
        }

        // ── Tab 2: Formula Reference ─────────────────────────────────────────
        if (ImGui::BeginTabItem("Formula Reference")) {
            ImGui::Spacing();
            ImGui::TextWrapped(
                "Formula streams let you derive new data from any loaded stream. "
                "Open the Formula Builder from the Data Streams panel, bind field "
                "aliases to columns, then write an expression using those aliases.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ── Operators ────────────────────────────────────────────────────
            ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.5f, 1.0f), "Operators");
            ImGui::Spacing();
            if (ImGui::BeginTable("##ops", 2,
                                   ImGuiTableFlags_Borders |
                                   ImGuiTableFlags_RowBg   |
                                   ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Operator", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                auto row = [](const char* op, const char* desc) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", op);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(desc);
                };
                row("+  -  *  /", "Add, subtract, multiply, divide");
                row("%",          "Modulo (remainder)");
                row("( )",        "Parentheses for grouping");
                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ── Scalar functions ─────────────────────────────────────────────
            ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.5f, 1.0f), "Scalar Functions");
            ImGui::TextDisabled("Evaluate independently per row. Arguments can be any expression.");
            ImGui::Spacing();
            if (ImGui::BeginTable("##scalar", 2,
                                   ImGuiTableFlags_Borders |
                                   ImGuiTableFlags_RowBg   |
                                   ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Function",    ImGuiTableColumnFlags_WidthFixed, 160.0f);
                ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                auto row = [](const char* fn, const char* desc) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", fn);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(desc);
                };
                row("abs(x)",      "Absolute value");
                row("sqrt(x)",     "Square root");
                row("pow(x, n)",   "x raised to the power n");
                row("log(x)",      "Natural logarithm (ln)");
                row("exp(x)",      "e raised to the power x");
                row("sin(x)",      "Sine (radians)");
                row("cos(x)",      "Cosine (radians)");
                row("tan(x)",      "Tangent (radians)");
                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ── Window functions ─────────────────────────────────────────────
            ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.5f, 1.0f), "Window Functions");
            ImGui::TextDisabled(
                "Operate over a rolling window of N rows. "
                "Syntax: fn(alias, N)  —  alias must be a bound field name, N must be a plain integer.");
            ImGui::Spacing();
            if (ImGui::BeginTable("##window", 3,
                                   ImGuiTableFlags_Borders |
                                   ImGuiTableFlags_RowBg   |
                                   ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Function",    ImGuiTableColumnFlags_WidthFixed, 160.0f);
                ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Notes",       ImGuiTableColumnFlags_WidthFixed, 180.0f);
                ImGui::TableHeadersRow();

                struct WFRow { const char* fn; const char* desc; const char* note; };
                static const WFRow wf[] = {
                    { "sma(alias, N)",    "Simple moving average over N periods",
                      "NaN for first N-1 rows" },
                    { "ema(alias, N)",    "Exponential moving average",
                      "Seeded with SMA; k = 2/(N+1)" },
                    { "stddev(alias, N)", "Rolling population std deviation",
                      "NaN for first N-1 rows" },
                    { "rmin(alias, N)",   "Rolling minimum over N periods",
                      "NaN for first N-1 rows" },
                    { "rmax(alias, N)",   "Rolling maximum over N periods",
                      "NaN for first N-1 rows" },
                    { "roc(alias, N)",    "Rate of change vs N periods ago",
                      "(v[i]-v[i-N])/v[i-N] \xc3\x97 100" },
                };
                for (const auto& r : wf) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", r.fn);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(r.desc);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextDisabled("%s", r.note);
                }
                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ── Examples ─────────────────────────────────────────────────────
            ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.5f, 1.0f), "Examples");
            ImGui::Spacing();
            if (ImGui::BeginTable("##examples", 2,
                                   ImGuiTableFlags_Borders |
                                   ImGuiTableFlags_RowBg   |
                                   ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Expression",  ImGuiTableColumnFlags_WidthFixed, 320.0f);
                ImGui::TableSetupColumn("What it computes", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                auto row = [](const char* expr, const char* what) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "%s", expr);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(what);
                };
                row("(a / b) * 100",                     "a as a percentage of b");
                row("close - sma(close, 20)",             "Distance from 20-day MA");
                row("(close-sma(close,20))/stddev(close,20)",
                                                          "Z-score vs 20-day window");
                row("ema(close, 12) - ema(close, 26)",   "MACD line");
                row("close / sma(close, 200) - 1",       "% above/below 200-day MA");
                row("roc(close, 1)",                      "Daily return %");
                row("rmax(high, 52) - rmin(low, 52)",    "52-period true range");
                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::TextDisabled(
                "Tip: window functions produce NaN for warmup rows — "
                "these appear as gaps in the plot, not zeros.");

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float btn_x = (ImGui::GetContentRegionAvail().x - 80.0f) * 0.5f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + btn_x);
    if (ImGui::Button("Close", ImVec2(80, 0)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}
