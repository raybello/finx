#include "ui/modals.h"
#include "ui/formula_builder.h"
#include "data/stream_store.h"
#include "data/plot_store.h"
#include "io/csv_parser.h"
#include "io/http_client.h"
#include "imgui.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <ctime>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/emscripten.h>

// ── Browser file picker ────────────────────────────────────────────────────

static bool        g_csv_pending = false;
static std::string g_csv_name;
static std::string g_csv_path;
static std::string g_csv_data;

extern "C" EMSCRIPTEN_KEEPALIVE
void finx_csv_loaded(const char* name, int nlen, const char* data, int dlen) {
    g_csv_name    = std::string(name, static_cast<size_t>(nlen));
    g_csv_data    = std::string(data, static_cast<size_t>(dlen));
    g_csv_pending = true;
}

EM_JS(void, js_open_file_picker, (), {
    var inp = document.createElement('input');
    inp.type = 'file';
    inp.accept = '.csv,.tsv,.txt';
    inp.onchange = function(e) {
        var file = e.target.files[0];
        if (!file) return;
        var reader = new FileReader();
        reader.onload = function(evt) {
            var nm      = file.name;
            var content = evt.target.result;
            var nLen = lengthBytesUTF8(nm) + 1;
            var nPtr = _malloc(nLen);
            stringToUTF8(nm, nPtr, nLen);
            var dLen = lengthBytesUTF8(content) + 1;
            var dPtr = _malloc(dLen);
            stringToUTF8(content, dPtr, dLen);
            _finx_csv_loaded(nPtr, nLen - 1, dPtr, dLen - 1);
            _free(nPtr);
            _free(dPtr);
        };
        reader.readAsText(file);
    };
    inp.click();
});

#else // ── Native: async popen-based file dialog ─────────────────────────────

#include <future>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cstdio>

static bool        g_csv_pending = false;
static std::string g_csv_name;
static std::string g_csv_path;
static std::string g_csv_data;

static std::future<std::string> g_dialog_future;

// Opens a native OS file-picker synchronously (called on a background thread).
// Returns the selected file path, or "" if cancelled / no tool available.
static std::string native_open_csv_dialog_sync() {
#ifdef __APPLE__
    FILE* fp = popen(
        "osascript -e 'POSIX path of (choose file of type"
        " {\"public.comma-separated-values-text\",\"public.plain-text\"}"
        " with prompt \"Open CSV File\")' 2>/dev/null", "r");
    if (fp) {
        char buf[4096] = {};
        bool got = (fgets(buf, sizeof(buf), fp) != nullptr);
        pclose(fp);
        if (got) {
            std::string s(buf);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            if (!s.empty()) return s;
        }
    }
#else
    // Try zenity (GNOME / GTK)
    FILE* fp = popen(
        "zenity --file-selection"
        " --title='Open CSV File'"
        " --file-filter='CSV files (*.csv *.tsv *.txt) | *.csv *.tsv *.txt'"
        " 2>/dev/null", "r");
    if (fp) {
        char buf[4096] = {};
        bool got = (fgets(buf, sizeof(buf), fp) != nullptr);
        pclose(fp);
        if (got) {
            std::string s(buf);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            if (!s.empty()) return s;
        }
    }
    // Try kdialog (KDE)
    fp = popen("kdialog --getopenfilename . '*.csv *.tsv *.txt' 2>/dev/null", "r");
    if (fp) {
        char buf[4096] = {};
        bool got = (fgets(buf, sizeof(buf), fp) != nullptr);
        pclose(fp);
        if (got) {
            std::string s(buf);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            if (!s.empty()) return s;
        }
    }
#endif
    return "";
}

// Polls the background dialog task; populates g_csv_* when the user picks a file.
static void poll_native_dialog() {
    if (!g_dialog_future.valid()) return;
    if (g_dialog_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

    std::string path = g_dialog_future.get(); // releases the future
    if (path.empty()) return;

    // Extract the filename component
    size_t slash = path.rfind('/');
    std::string fname = (slash != std::string::npos) ? path.substr(slash + 1) : path;

    // Read file contents
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::ostringstream ss;
    ss << f.rdbuf();

    g_csv_name    = std::move(fname);
    g_csv_path    = path;
    g_csv_data    = ss.str();
    g_csv_pending = true;
}

static void js_open_file_picker() {
    if (!g_dialog_future.valid()) {
        g_dialog_future = std::async(std::launch::async, native_open_csv_dialog_sync);
    }
}

#endif

bool modals_csv_pending() { return g_csv_pending; }

void modals_consume_csv(std::string& filename, std::string& raw) {
    filename      = std::move(g_csv_name);
    raw           = std::move(g_csv_data);
    g_csv_pending = false;
}

// ── PNG export modal ───────────────────────────────────────────────────────

#ifndef __EMSCRIPTEN__
struct ExportPngState {
    bool pending_open = false;
    char path_buf[512] = {};
    std::future<std::string> save_dialog_future;
};
static ExportPngState g_png;
static bool        g_png_ready = false;
static std::string g_png_confirmed_path;

// Opens a native OS save dialog on a background thread.
static std::string native_save_png_dialog_sync(const std::string& suggested) {
#ifdef __APPLE__
    char cmd[1024];
    size_t slash = suggested.rfind('/');
    std::string defname = (slash != std::string::npos)
        ? suggested.substr(slash + 1)
        : suggested;
    std::snprintf(cmd, sizeof(cmd),
        "osascript -e 'POSIX path of (choose file name default name \"%s\""
        " with prompt \"Save chart as PNG\")' 2>/dev/null",
        defname.c_str());
    FILE* fp = popen(cmd, "r");
    if (fp) {
        char buf[4096] = {};
        bool got = (fgets(buf, sizeof(buf), fp) != nullptr);
        pclose(fp);
        if (got) {
            std::string s(buf);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            if (!s.empty()) return s;
        }
    }
#else
    char cmd[1024];
    size_t slash = suggested.rfind('/');
    std::string defname = (slash != std::string::npos)
        ? suggested.substr(slash + 1)
        : suggested;
    std::snprintf(cmd, sizeof(cmd),
        "zenity --file-selection --save --confirm-overwrite"
        " --filename='%s' --title='Save PNG'"
        " --file-filter='PNG files (*.png) | *.png' 2>/dev/null",
        defname.c_str());
    FILE* fp = popen(cmd, "r");
    if (fp) {
        char buf[4096] = {};
        bool got = (fgets(buf, sizeof(buf), fp) != nullptr);
        pclose(fp);
        if (got) {
            std::string s(buf);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            if (!s.empty()) {
                if (s.size() < 4 || s.substr(s.size() - 4) != ".png")
                    s += ".png";
                return s;
            }
        }
    }
    fp = popen("kdialog --getsavefilename . '*.png' 2>/dev/null", "r");
    if (fp) {
        char buf[4096] = {};
        bool got = (fgets(buf, sizeof(buf), fp) != nullptr);
        pclose(fp);
        if (got) {
            std::string s(buf);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            if (!s.empty()) return s;
        }
    }
#endif
    return "";
}

static void generate_default_png_path(char* buf, size_t bufsz) {
    std::string dir;
#ifdef __APPLE__
    if (const char* home = getenv("HOME")) {
        dir = std::string(home) + "/Desktop/";
    }
#endif
    time_t t = time(nullptr);
    struct tm* ti = localtime(&t);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", ti);
    std::snprintf(buf, bufsz, "%sfinx_%s.png", dir.c_str(), ts);
}
#endif // !__EMSCRIPTEN__

void modals_request_export_png() {
#ifndef __EMSCRIPTEN__
    generate_default_png_path(g_png.path_buf, sizeof(g_png.path_buf));
    g_png.pending_open = true;
#endif
}

#ifndef __EMSCRIPTEN__
bool modals_png_path_ready() { return g_png_ready; }

std::string modals_consume_png_path() {
    g_png_ready = false;
    return std::move(g_png_confirmed_path);
}
#else
bool modals_png_path_ready() { return false; }
std::string modals_consume_png_path() { return ""; }
#endif

// ── HTTP test sentinel ─────────────────────────────────────────────────────

static constexpr uint32_t kTestSentinel = 0xFFFFFFFFu;

struct HttpTestResult {
    bool        ready = false;
    bool        ok    = false;
    std::string body;
};
static HttpTestResult g_http_test;

// ── Modal state ────────────────────────────────────────────────────────────

struct ParamRow {
    char key[64]  = {};
    char val[128] = {};
};

struct FMapRow {
    char out[64]  = {};
    char key[64]  = {};
    int  type_idx = 0; // 0=NUM,1=TS,2=STR
};

struct AddStreamState {
    bool pending_open   = false;
    char name[128]      = "Stream 1";
    int  source_type_idx = 0;   // 0=CSV, 1=HTTP

    // CSV
    bool        csv_ready   = false;
    std::string csv_filename;
    std::string csv_path;
    std::string csv_raw;
    std::string csv_preview;

    // HTTP
    char                  url_buf[512]   = {};
    std::vector<ParamRow> params;
    char                  json_path[256] = "results";
    std::vector<FMapRow>  fmap;
    bool                  http_test_ok   = false;
    std::string           http_preview;

    std::string modal_error;
};

static AddStreamState g_state;

void modals_request_add_stream() {
    g_state.pending_open = true;
}

// Helper: build a multi-line preview string from a ParsedTable
static std::string build_preview(const ParsedTable& t, int max_rows = 5) {
    if (!t.error.empty()) return "Error: " + t.error;
    std::string out;
    // Header row
    bool first = true;
    for (const auto& fd : t.schema) {
        if (!first) out += " | ";
        out += fd.name;
        first = false;
    }
    out += "\n";

    int rows = static_cast<int>(std::min(t.row_count, static_cast<size_t>(max_rows)));
    for (int r = 0; r < rows; ++r) {
        first = true;
        for (const auto& fd : t.schema) {
            if (!first) out += " | ";
            if (fd.type == FieldType::STRING) {
                auto it = t.str_columns.find(fd.name);
                if (it != t.str_columns.end() && r < (int)it->second.size()) {
                    out += it->second[r];
                } else {
                    out += "";
                }
            } else {
                auto it = t.columns.find(fd.name);
                if (it != t.columns.end() && r < (int)it->second.size()) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%.4g", it->second[r]);
                    out += buf;
                } else {
                    out += "0";
                }
            }
            first = false;
        }
        out += "\n";
    }
    if (t.row_count > static_cast<size_t>(max_rows)) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "... (%zu more rows)", t.row_count - max_rows);
        out += buf;
    }
    return out;
}

// ── Main render ────────────────────────────────────────────────────────────

void modals_render(StreamStore& ss, PlotStore& /*ps*/) {
#ifndef __EMSCRIPTEN__
    poll_native_dialog();

    // Poll the PNG save dialog future
    if (g_png.save_dialog_future.valid() &&
        g_png.save_dialog_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::string chosen = g_png.save_dialog_future.get();
        if (!chosen.empty()) {
            std::strncpy(g_png.path_buf, chosen.c_str(), sizeof(g_png.path_buf) - 1);
        }
    }

    // Open PNG export popup if requested
    if (g_png.pending_open) {
        g_png.pending_open = false;
        ImGui::OpenPopup("Export PNG##modal");
    }

    ImGui::SetNextWindowSize(ImVec2(480, 130), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("Export PNG##modal", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::Text("Save path:");
        ImGui::SetNextItemWidth(-80.0f);
        ImGui::InputText("##png_path", g_png.path_buf, sizeof(g_png.path_buf));
        ImGui::SameLine();
        bool dialog_busy = g_png.save_dialog_future.valid();
        if (dialog_busy) ImGui::BeginDisabled();
        if (ImGui::Button("Browse...")) {
            std::string suggested(g_png.path_buf);
            g_png.save_dialog_future = std::async(std::launch::async,
                [suggested]() { return native_save_png_dialog_sync(suggested); });
        }
        if (dialog_busy) ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Save", ImVec2(100, 0))) {
            if (g_png.path_buf[0] != '\0') {
                g_png_confirmed_path = g_png.path_buf;
                g_png_ready = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
#endif // !__EMSCRIPTEN__

    // Check for pending CSV loaded from file picker (web) or native dialog
    if (g_csv_pending) {
        std::string fname, raw;
        modals_consume_csv(fname, raw);
        g_state.csv_filename = fname;
        g_state.csv_path     = std::move(g_csv_path);
        g_state.csv_raw      = raw;
        g_state.csv_ready    = true;
        // Build preview
        ParsedTable t = parse_csv(raw);
        g_state.csv_preview  = build_preview(t, 5);
    }

    // Check HTTP test result
    if (g_http_test.ready) {
        g_http_test.ready = false;
        if (g_http_test.ok) {
            // Build field_map from current fmap state
            std::vector<FieldMapEntry> fm;
            for (const auto& r : g_state.fmap) {
                FieldMapEntry e;
                e.output_name = r.out;
                e.json_key    = r.key;
                switch (r.type_idx) {
                    case 1: e.type = FieldType::TIMESTAMP; break;
                    case 2: e.type = FieldType::STRING;    break;
                    default: e.type = FieldType::NUMBER;   break;
                }
                fm.push_back(e);
            }
            ParsedTable t = extract_json(g_http_test.body, g_state.json_path, fm);
            if (!t.error.empty()) {
                g_state.modal_error  = t.error;
                g_state.http_test_ok = false;
            } else {
                g_state.http_preview  = build_preview(t, 5);
                g_state.http_test_ok  = true;
                g_state.modal_error.clear();
            }
        } else {
            g_state.modal_error  = g_http_test.body;
            g_state.http_test_ok = false;
        }
    }

    // Open the popup if requested
    if (g_state.pending_open) {
        g_state.pending_open = false;
        ImGui::OpenPopup("Add Data Stream##modal");
    }

    ImGui::SetNextWindowSize(ImVec2(620, 520), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Add Data Stream##modal", nullptr,
                                 ImGuiWindowFlags_NoResize)) {
        return;
    }

    // ── Name ────────────────────────────────────────────────────────────────
    ImGui::Text("Stream Name");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##stream_name", g_state.name, sizeof(g_state.name));

    ImGui::Spacing();

    // ── Source type tabs ────────────────────────────────────────────────────
    if (ImGui::RadioButton("CSV File", g_state.source_type_idx == 0)) {
        g_state.source_type_idx = 0;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("HTTP GET", g_state.source_type_idx == 1)) {
        g_state.source_type_idx = 1;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Formula", g_state.source_type_idx == 2)) {
        g_state.source_type_idx = 2;
    }

    ImGui::Separator();
    ImGui::Spacing();

    float available_h = ImGui::GetContentRegionAvail().y - 50.0f; // reserve for buttons

    // ── CSV section ─────────────────────────────────────────────────────────
    if (g_state.source_type_idx == 0) {
        bool dialog_busy = false;
#ifndef __EMSCRIPTEN__
        dialog_busy = g_dialog_future.valid();
#endif
        if (dialog_busy) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Browse File...")) {
            js_open_file_picker();
        }
        if (dialog_busy) {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("Opening...");
        }

        if (g_state.csv_ready) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "[OK]");
            ImGui::SameLine();
            ImGui::Text("%s", g_state.csv_filename.c_str());

            ImGui::Spacing();
            ImGui::Text("Preview (first 5 rows):");
            ImGui::BeginChild("##csv_preview", ImVec2(-1, available_h - 60.0f), true);
            ImGui::TextUnformatted(g_state.csv_preview.c_str());
            ImGui::EndChild();
        } else {
            ImGui::SameLine();
            ImGui::TextDisabled("No file selected");
            ImGui::BeginChild("##csv_preview_empty", ImVec2(-1, available_h - 60.0f), true);
            ImGui::TextDisabled("File preview will appear here after loading.");
            ImGui::EndChild();
        }
    }

    // ── Formula section ─────────────────────────────────────────────────────
    if (g_state.source_type_idx == 2) {
        ImGui::BeginChild("##formula_info", ImVec2(-1, available_h), false);
        ImGui::TextDisabled("Use the Formula Builder to define a new derived stream.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "A derived stream computes a new column from an arithmetic "
            "expression over fields in other streams (e.g. "
            "(births / total_pop) * 100). Click 'Open Formula Builder' below.");
        ImGui::EndChild();
    }

    // ── HTTP section ────────────────────────────────────────────────────────
    if (g_state.source_type_idx == 1) {
        ImGui::BeginChild("##http_form", ImVec2(-1, available_h), false);

        // URL
        ImGui::Text("URL Template");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##url", g_state.url_buf, sizeof(g_state.url_buf));
        ImGui::TextDisabled("Use {key} placeholders for parameters, e.g. https://api.example.com/data?token={token}");

        ImGui::Spacing();

        // Params table
        ImGui::Text("Parameters");
        if (ImGui::BeginTable("##params_table", 3,
                               ImGuiTableFlags_Borders |
                               ImGuiTableFlags_RowBg   |
                               ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Key",   ImGuiTableColumnFlags_WidthStretch, 0.4f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.5f);
            ImGui::TableSetupColumn("Del",   ImGuiTableColumnFlags_WidthFixed,   40.0f);
            ImGui::TableHeadersRow();

            int del_idx = -1;
            for (int i = 0; i < (int)g_state.params.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-1.0f);
                char key_id[32]; std::snprintf(key_id, sizeof(key_id), "##pk%d", i);
                ImGui::InputText(key_id, g_state.params[i].key, sizeof(g_state.params[i].key));
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1.0f);
                char val_id[32]; std::snprintf(val_id, sizeof(val_id), "##pv%d", i);
                ImGui::InputText(val_id, g_state.params[i].val, sizeof(g_state.params[i].val));
                ImGui::TableSetColumnIndex(2);
                char del_id[32]; std::snprintf(del_id, sizeof(del_id), "X##pdel%d", i);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
                if (ImGui::SmallButton(del_id)) del_idx = i;
                ImGui::PopStyleColor();
            }
            if (del_idx >= 0) g_state.params.erase(g_state.params.begin() + del_idx);
            ImGui::EndTable();
        }
        if (ImGui::SmallButton("+ Add Param")) {
            g_state.params.emplace_back();
        }

        ImGui::Spacing();

        // JSON path
        ImGui::Text("JSON Path (dot-separated)");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##json_path", g_state.json_path, sizeof(g_state.json_path));

        ImGui::Spacing();

        // Field map table
        ImGui::Text("Field Map");
        static const char* type_labels[] = { "Number", "Timestamp", "String" };
        if (ImGui::BeginTable("##fmap_table", 4,
                               ImGuiTableFlags_Borders |
                               ImGuiTableFlags_RowBg   |
                               ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Output Name", ImGuiTableColumnFlags_WidthStretch, 0.3f);
            ImGui::TableSetupColumn("JSON Key",    ImGuiTableColumnFlags_WidthStretch, 0.3f);
            ImGui::TableSetupColumn("Type",        ImGuiTableColumnFlags_WidthStretch, 0.25f);
            ImGui::TableSetupColumn("Del",         ImGuiTableColumnFlags_WidthFixed,   40.0f);
            ImGui::TableHeadersRow();

            int del_fm = -1;
            for (int i = 0; i < (int)g_state.fmap.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-1.0f);
                char oid[32]; std::snprintf(oid, sizeof(oid), "##fo%d", i);
                ImGui::InputText(oid, g_state.fmap[i].out, sizeof(g_state.fmap[i].out));
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1.0f);
                char kid[32]; std::snprintf(kid, sizeof(kid), "##fk%d", i);
                ImGui::InputText(kid, g_state.fmap[i].key, sizeof(g_state.fmap[i].key));
                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-1.0f);
                char tid[32]; std::snprintf(tid, sizeof(tid), "##ft%d", i);
                ImGui::Combo(tid, &g_state.fmap[i].type_idx, type_labels, 3);
                ImGui::TableSetColumnIndex(3);
                char fdelid[32]; std::snprintf(fdelid, sizeof(fdelid), "X##fmdel%d", i);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
                if (ImGui::SmallButton(fdelid)) del_fm = i;
                ImGui::PopStyleColor();
            }
            if (del_fm >= 0) g_state.fmap.erase(g_state.fmap.begin() + del_fm);
            ImGui::EndTable();
        }
        if (ImGui::SmallButton("+ Add Field")) {
            g_state.fmap.emplace_back();
        }

        ImGui::Spacing();

        // Test fetch button
        if (ImGui::Button("Test Fetch")) {
            std::string url_str = g_state.url_buf;
            std::vector<std::pair<std::string,std::string>> param_vec;
            for (const auto& r : g_state.params) {
                if (r.key[0] != '\0') {
                    param_vec.emplace_back(r.key, r.val);
                }
            }
            std::string url = build_url(url_str, param_vec);
            g_state.modal_error.clear();
            g_state.http_test_ok = false;
            g_http_test.ready = false;
            http_get_async(kTestSentinel, url, [](bool ok, std::string body) {
                g_http_test.ok    = ok;
                g_http_test.body  = std::move(body);
                g_http_test.ready = true;
            });
        }

        if (g_state.http_test_ok) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "[OK]");
            ImGui::Spacing();
            ImGui::Text("Preview:");
            ImGui::BeginChild("##http_preview", ImVec2(-1, 120.0f), true);
            ImGui::TextUnformatted(g_state.http_preview.c_str());
            ImGui::EndChild();
        }

        ImGui::EndChild();
    }

    // ── Error line ──────────────────────────────────────────────────────────
    if (!g_state.modal_error.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: %s", g_state.modal_error.c_str());
    }

    // ── Bottom buttons ──────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    bool is_formula = (g_state.source_type_idx == 2);
    bool can_confirm = false;
    if (g_state.source_type_idx == 0) {
        can_confirm = g_state.csv_ready;
    } else if (g_state.source_type_idx == 1) {
        can_confirm = (g_state.url_buf[0] != '\0');
    } else {
        can_confirm = true; // Formula: always allow (opens a new modal)
    }

    if (!can_confirm) ImGui::BeginDisabled();

    const char* confirm_label = is_formula ? "Open Formula Builder" : "Confirm";
    if (ImGui::Button(confirm_label, ImVec2(160, 0))) {
        g_state.modal_error.clear();
        if (g_state.source_type_idx == 0) {
            ss.add_csv(g_state.name, g_state.csv_filename, g_state.csv_raw, g_state.csv_path);
            g_state = AddStreamState{};
            ImGui::CloseCurrentPopup();
        } else if (g_state.source_type_idx == 1) {
            HttpSource src;
            src.url_template = g_state.url_buf;
            src.json_path    = g_state.json_path;
            for (const auto& r : g_state.params) {
                if (r.key[0] != '\0') src.params.emplace_back(r.key, r.val);
            }
            for (const auto& r : g_state.fmap) {
                FieldMapEntry e;
                e.output_name = r.out;
                e.json_key    = r.key;
                switch (r.type_idx) {
                    case 1: e.type = FieldType::TIMESTAMP; break;
                    case 2: e.type = FieldType::STRING;    break;
                    default: e.type = FieldType::NUMBER;   break;
                }
                src.field_map.push_back(e);
            }
            ss.add_http(g_state.name, src);
            g_state = AddStreamState{};
            ImGui::CloseCurrentPopup();
        } else {
            // Formula: close this modal and open the formula builder
            formula_builder_request_open(0);
            g_state = AddStreamState{};
            ImGui::CloseCurrentPopup();
        }
    }

    if (!can_confirm) ImGui::EndDisabled();

    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        g_state = AddStreamState{};
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
