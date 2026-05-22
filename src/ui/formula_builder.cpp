#include "ui/formula_builder.h"
#include "app.h"
#include "data/stream_store.h"
#include "data/types.h"
#include "imgui.h"
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <algorithm>

// ── Helpers ────────────────────────────────────────────────────────────────

static std::string make_alias(const std::string& field_name) {
    std::string a;
    for (char c : field_name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            a += c;
        } else if (!a.empty() && a.back() != '_') {
            a += '_';
        }
    }
    while (!a.empty() && a.back() == '_') a.pop_back();
    if (a.empty() || std::isdigit(static_cast<unsigned char>(a[0]))) a = "f" + a;
    if (a.size() > 63) a.resize(63);
    return a;
}

static std::string unique_alias(const std::string& base,
                                const std::vector<FormulaBinding>& bindings) {
    std::string candidate = base;
    bool taken = true;
    int  suffix = 2;
    while (taken) {
        taken = false;
        for (const auto& b : bindings) {
            if (b.alias == candidate) { taken = true; break; }
        }
        if (taken) {
            char buf[80];
            std::snprintf(buf, sizeof(buf), "%s_%d", base.c_str(), suffix++);
            candidate = buf;
        }
    }
    return candidate;
}

static void expr_append(char* buf, size_t bufsz, const char* token) {
    size_t cur = std::strlen(buf);
    if (cur + std::strlen(token) + 2 >= bufsz) return;

    // Add a leading space if the buffer is non-empty and doesn't end with '(' or whitespace
    if (cur > 0) {
        char last = buf[cur - 1];
        if (last != '(' && !std::isspace(static_cast<unsigned char>(last))) {
            buf[cur++] = ' ';
            buf[cur]   = '\0';
        }
    }
    std::strncat(buf, token, bufsz - cur - 1);
}

static std::string build_combo(const std::vector<std::string>& items) {
    std::string s;
    for (const auto& item : items) { s += item; s += '\0'; }
    s += '\0';
    return s;
}

// ── State ──────────────────────────────────────────────────────────────────

struct FBState {
    bool     open_requested = false;
    uint32_t edit_id        = 0;

    char name_buf[128]   = {};
    char result_buf[128] = {};
    char expr_buf[512]   = {};

    int  picker_stream_idx = -1;
    int  picker_field_idx  = -1;
    char alias_buf[64]     = {};

    int  x_stream_idx = -1;
    int  x_field_idx  = -1;

    std::vector<FormulaBinding> bindings;
    std::string error_msg;
};

static FBState g_fb;

// ── Public API ─────────────────────────────────────────────────────────────

void formula_builder_request_open(uint32_t edit_id) {
    g_fb.edit_id        = edit_id;
    g_fb.open_requested = true;
}

// ── Pre-fill helpers ───────────────────────────────────────────────────────

static void reset_state() {
    g_fb = FBState{};
    std::strncpy(g_fb.name_buf,   "derived_stream", sizeof(g_fb.name_buf) - 1);
    std::strncpy(g_fb.result_buf, "result",          sizeof(g_fb.result_buf) - 1);
}

static void prefill_for_edit(App& app, uint32_t edit_id) {
    DataStream* ds = app.stream_store.find(edit_id);
    if (!ds || ds->source_type != SourceType::FORMULA) return;

    std::strncpy(g_fb.name_buf,   ds->name.c_str(),
                 sizeof(g_fb.name_buf) - 1);
    std::strncpy(g_fb.result_buf, ds->formula_source.result_name.c_str(),
                 sizeof(g_fb.result_buf) - 1);
    std::strncpy(g_fb.expr_buf,   ds->formula_source.expression.c_str(),
                 sizeof(g_fb.expr_buf) - 1);
    g_fb.bindings = ds->formula_source.bindings;

    // Locate x-stream + x-field indices
    const auto& streams = app.stream_store.all();
    for (int si = 0; si < static_cast<int>(streams.size()); ++si) {
        if (streams[si].id == ds->formula_source.x_stream_id) {
            g_fb.x_stream_idx = si;
            const auto& schema = streams[si].schema;
            for (int fi = 0; fi < static_cast<int>(schema.size()); ++fi) {
                if (schema[fi].name == ds->formula_source.x_field) {
                    g_fb.x_field_idx = fi;
                    break;
                }
            }
            break;
        }
    }
}

// ── Render ─────────────────────────────────────────────────────────────────

void formula_builder_render(App& app) {
    // Handle deferred open
    if (g_fb.open_requested) {
        g_fb.open_requested = false;
        uint32_t eid = g_fb.edit_id;
        reset_state();
        g_fb.edit_id = eid;
        if (eid != 0) {
            prefill_for_edit(app, eid);
        }
        ImGui::OpenPopup("Formula Builder##fb");
    }

    ImGui::SetNextWindowSize(ImVec2(620, 560), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Formula Builder##fb", nullptr,
                                ImGuiWindowFlags_NoResize)) {
        return;
    }

    // ── Build stream lists (refreshed every frame) ─────────────────────────
    const auto& streams = app.stream_store.all();
    std::vector<std::string> stream_names;
    std::vector<uint32_t>    stream_ids;
    for (const auto& ds : streams) {
        stream_names.push_back(ds.name);
        stream_ids.push_back(ds.id);
    }
    std::string stream_combo_str = build_combo(stream_names);

    // ── Name + Result field ────────────────────────────────────────────────
    float half = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;

    ImGui::Text("Stream Name");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(half - 80.0f);
    ImGui::InputText("##fb_name", g_fb.name_buf, sizeof(g_fb.name_buf));

    ImGui::SameLine(0.0f, 16.0f);
    ImGui::Text("Result Field");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##fb_result", g_fb.result_buf, sizeof(g_fb.result_buf));

    ImGui::Spacing();

    // ── Expression input ───────────────────────────────────────────────────
    ImGui::Text("Expression:");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##fb_expr", g_fb.expr_buf, sizeof(g_fb.expr_buf));

    ImGui::Spacing();

    // ── Operator buttons ───────────────────────────────────────────────────
    ImGui::TextDisabled("Operators");
    auto op_btn = [&](const char* label, const char* token) {
        if (ImGui::Button(label)) expr_append(g_fb.expr_buf, sizeof(g_fb.expr_buf), token);
        ImGui::SameLine();
    };
    op_btn("(", "(");
    op_btn(")", ")");
    op_btn("+", "+");
    op_btn("-", "-");
    op_btn("*", "*");
    op_btn("/", "/");
    op_btn("%", "%");
    ImGui::NewLine();

    ImGui::TextDisabled("Functions");
    op_btn("sqrt(", "sqrt(");
    op_btn("cos(", "cos(");
    op_btn("sin(", "sin(");
    op_btn("tan(", "tan(");
    op_btn("abs(", "abs(");
    op_btn("log(", "log(");
    op_btn("exp(", "exp(");
    ImGui::NewLine();

    ImGui::TextDisabled("Window Functions  (alias, period)");
    op_btn("sma(", "sma(");
    op_btn("ema(", "ema(");
    op_btn("stddev(", "stddev(");
    op_btn("rmin(", "rmin(");
    op_btn("rmax(", "rmax(");
    op_btn("roc(", "roc(");
    ImGui::NewLine();

    // Backspace and clear
    if (ImGui::Button("Backspace")) {
        size_t len = std::strlen(g_fb.expr_buf);
        if (len > 0) g_fb.expr_buf[len - 1] = '\0';
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("Clear Expression")) {
        g_fb.expr_buf[0] = '\0';
    }
    ImGui::PopStyleColor();

    ImGui::Separator();
    ImGui::Spacing();

    // ── Insert Field ────────────────────────────────────────────────────────
    ImGui::TextDisabled("Insert Field");

    // Stream picker for field insertion
    ImGui::Text("Stream");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::Combo("##fb_picker_stream", &g_fb.picker_stream_idx,
                     stream_combo_str.c_str())) {
        g_fb.picker_field_idx = -1;
        g_fb.alias_buf[0] = '\0';
    }

    // Field picker (NUMBER fields only)
    std::vector<std::string> picker_fields;
    if (g_fb.picker_stream_idx >= 0 &&
        g_fb.picker_stream_idx < static_cast<int>(streams.size())) {
        for (const auto& fd : streams[g_fb.picker_stream_idx].schema) {
            if (fd.type == FieldType::NUMBER) {
                picker_fields.push_back(fd.name);
            }
        }
    }
    std::string picker_field_combo = build_combo(picker_fields);

    ImGui::SameLine();
    ImGui::Text("Field");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    bool field_changed = ImGui::Combo("##fb_picker_field", &g_fb.picker_field_idx,
                                      picker_field_combo.c_str());
    if (field_changed && g_fb.picker_field_idx >= 0 &&
        g_fb.picker_field_idx < static_cast<int>(picker_fields.size())) {
        std::string alias = make_alias(picker_fields[g_fb.picker_field_idx]);
        std::strncpy(g_fb.alias_buf, alias.c_str(), sizeof(g_fb.alias_buf) - 1);
    }

    ImGui::Text("Alias");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputText("##fb_alias", g_fb.alias_buf, sizeof(g_fb.alias_buf));

    ImGui::SameLine();
    bool can_insert = g_fb.picker_stream_idx >= 0 &&
                      g_fb.picker_field_idx  >= 0 &&
                      g_fb.picker_field_idx < static_cast<int>(picker_fields.size()) &&
                      g_fb.alias_buf[0] != '\0';

    if (!can_insert) ImGui::BeginDisabled();
    if (ImGui::Button("Insert Field")) {
        std::string alias     = g_fb.alias_buf;
        std::string field     = picker_fields[g_fb.picker_field_idx];
        uint32_t    stream_id = stream_ids[g_fb.picker_stream_idx];

        // Check if alias already bound to a different field/stream
        bool found_exact = false;
        bool conflict    = false;
        for (const auto& b : g_fb.bindings) {
            if (b.alias == alias) {
                if (b.stream_id == stream_id && b.field_name == field) {
                    found_exact = true;
                } else {
                    conflict = true;
                }
                break;
            }
        }

        if (conflict) {
            g_fb.error_msg = "Alias '" + alias + "' is already bound to a different field. "
                             "Choose a different alias.";
        } else {
            g_fb.error_msg.clear();
            if (!found_exact) {
                FormulaBinding b;
                b.alias      = alias;
                b.stream_id  = stream_id;
                b.field_name = field;
                g_fb.bindings.push_back(b);
            }
            expr_append(g_fb.expr_buf, sizeof(g_fb.expr_buf), alias.c_str());
        }
    }
    if (!can_insert) ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::Spacing();

    // ── X Axis picker ───────────────────────────────────────────────────────
    ImGui::TextDisabled("X Axis");
    ImGui::Text("Stream");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::Combo("##fb_x_stream", &g_fb.x_stream_idx,
                     stream_combo_str.c_str())) {
        g_fb.x_field_idx = -1;
    }

    std::vector<std::string> x_fields;
    if (g_fb.x_stream_idx >= 0 &&
        g_fb.x_stream_idx < static_cast<int>(streams.size())) {
        for (const auto& fd : streams[g_fb.x_stream_idx].schema) {
            if (fd.type == FieldType::NUMBER || fd.type == FieldType::TIMESTAMP) {
                x_fields.push_back(fd.name);
            }
        }
    }
    std::string x_field_combo = build_combo(x_fields);

    ImGui::SameLine();
    ImGui::Text("Field");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##fb_x_field", &g_fb.x_field_idx, x_field_combo.c_str());

    ImGui::Separator();
    ImGui::Spacing();

    // ── Bindings list ───────────────────────────────────────────────────────
    ImGui::TextDisabled("Variable Bindings");
    float list_h = ImGui::GetContentRegionAvail().y - 70.0f;
    if (list_h < 60.0f) list_h = 60.0f;

    ImGui::BeginChild("##fb_bindings", ImVec2(-1, list_h), true);
    int del_binding = -1;
    for (int i = 0; i < static_cast<int>(g_fb.bindings.size()); ++i) {
        const FormulaBinding& b = g_fb.bindings[i];
        ImGui::PushID(i);

        // Look up stream name
        std::string stream_name = "(unknown)";
        for (const auto& ds : streams) {
            if (ds.id == b.stream_id) { stream_name = ds.name; break; }
        }

        char row_text[256];
        std::snprintf(row_text, sizeof(row_text), "%-20s  \xe2\x86\x92  %s : %s",
                      b.alias.c_str(), stream_name.c_str(), b.field_name.c_str());
        ImGui::TextUnformatted(row_text);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
        if (ImGui::SmallButton("X")) del_binding = i;
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    if (del_binding >= 0) {
        g_fb.bindings.erase(g_fb.bindings.begin() + del_binding);
    }
    if (g_fb.bindings.empty()) {
        ImGui::TextDisabled("  No bindings yet. Use 'Insert Field' above.");
    }
    ImGui::EndChild();

    // ── Error message ───────────────────────────────────────────────────────
    if (!g_fb.error_msg.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                           "%s", g_fb.error_msg.c_str());
    }

    ImGui::Separator();
    ImGui::Spacing();

    // ── Bottom buttons ──────────────────────────────────────────────────────
    bool can_confirm = g_fb.name_buf[0] != '\0' &&
                       g_fb.result_buf[0] != '\0' &&
                       g_fb.expr_buf[0]   != '\0' &&
                       g_fb.x_stream_idx  >= 0 &&
                       g_fb.x_field_idx   >= 0 &&
                       g_fb.x_field_idx < static_cast<int>(x_fields.size());

    if (!can_confirm) ImGui::BeginDisabled();
    const char* confirm_label = (g_fb.edit_id == 0) ? "Create Stream" : "Update Stream";
    if (ImGui::Button(confirm_label, ImVec2(140, 0))) {
        // Build FormulaSource
        FormulaSource src;
        src.expression  = g_fb.expr_buf;
        src.result_name = g_fb.result_buf;
        src.x_stream_id = stream_ids[g_fb.x_stream_idx];
        src.x_field     = x_fields[g_fb.x_field_idx];
        src.bindings    = g_fb.bindings;

        if (g_fb.edit_id == 0) {
            uint32_t sid = app.stream_store.add_formula(g_fb.name_buf, src);
            uint32_t pid = app.new_plot();
            if (Plot* p = app.plot_store.find(pid)) {
                PlotSeries s;
                s.stream_id = sid;
                s.label     = "Series 1";
                p->series.push_back(s);
            }
            g_fb.error_msg.clear();
            ImGui::CloseCurrentPopup();
        } else {
            DataStream* ds = app.stream_store.find(g_fb.edit_id);
            if (ds) {
                ds->name           = g_fb.name_buf;
                ds->formula_source = src;
                bool ok = app.stream_store.evaluate_formula(g_fb.edit_id);
                if (!ok) {
                    g_fb.error_msg = ds->error_msg;
                } else {
                    g_fb.error_msg.clear();
                    ImGui::CloseCurrentPopup();
                }
            }
        }
    }
    if (!can_confirm) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
