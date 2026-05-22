#include "persist/config.h"
#include "app.h"
#include "data/stream_store.h"
#include "data/plot_store.h"
#include "data/sample_data.h"
#include "json.hpp"
#include <string>
#include <fstream>
#include <sstream>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS(void, js_localstorage_set, (const char* key, const char* val), {
    try {
        localStorage.setItem(UTF8ToString(key), UTF8ToString(val));
    } catch(e) {}
});

EM_JS(void, js_localstorage_get, (const char* key, char* buf, int len), {
    try {
        var v = localStorage.getItem(UTF8ToString(key)) || "";
        stringToUTF8(v, buf, len);
    } catch(e) {
        if (len > 0) HEAP8[buf] = 0;
    }
});
#endif

using json = nlohmann::json;

static const char* kConfigKey  = "finx_config";
static const char* kConfigFile = "finx.json";

// ── Serialise ───────────────────────────────────────────────────────────────

static json serialise_app(const App& app) {
    json j;

    // Streams (HTTP only — CSV data not re-serialised)
    json j_streams = json::array();
    for (const auto& ds : const_cast<App&>(app).stream_store.all()) {
        json s;
        s["id"]          = ds.id;
        s["name"]        = ds.name;
        s["source_type"] = (ds.source_type == SourceType::CSV_FILE) ? "csv" : "http";

        if (ds.source_type == SourceType::HTTP_GET) {
            json h;
            h["url_template"] = ds.http_source.url_template;
            h["json_path"]    = ds.http_source.json_path;
            json params = json::array();
            for (const auto& kv : ds.http_source.params) {
                params.push_back({ {"key", kv.first}, {"val", kv.second} });
            }
            h["params"] = params;
            json fmap = json::array();
            for (const auto& fm : ds.http_source.field_map) {
                json f;
                f["output_name"] = fm.output_name;
                f["json_key"]    = fm.json_key;
                switch (fm.type) {
                    case FieldType::TIMESTAMP: f["type"] = "ts";  break;
                    case FieldType::STRING:    f["type"] = "str"; break;
                    default:                   f["type"] = "num"; break;
                }
                fmap.push_back(f);
            }
            h["field_map"] = fmap;
            s["http"]      = h;
        } else {
            // CSV: just remember the filename
            s["csv_filename"] = ds.csv_source.filename;
        }

        j_streams.push_back(s);
    }
    j["streams"] = j_streams;

    // Plots
    json j_plots = json::array();
    for (const auto& p : const_cast<App&>(app).plot_store.all()) {
        json jp;
        jp["id"]          = p.id;
        jp["name"]        = p.name;
        jp["x_label"]     = p.x_label;
        jp["y_label"]     = p.y_label;
        jp["y2_label"]    = p.y2_label;
        jp["show_legend"] = p.show_legend;

        json jseries = json::array();
        for (const auto& ser : p.series) {
            json js;
            js["stream_id"] = ser.stream_id;
            js["x_field"]   = ser.x_field;
            js["y_field"]   = ser.y_field;
            js["y_axis"]    = ser.y_axis;
            js["label"]     = ser.label;
            switch (ser.plot_type) {
                case PlotType::SCATTER: js["plot_type"] = "scatter"; break;
                case PlotType::BAR:     js["plot_type"] = "bar";     break;
                case PlotType::STEP:    js["plot_type"] = "step";    break;
                default:                js["plot_type"] = "line";    break;
            }
            js["color"] = { ser.color.x, ser.color.y, ser.color.z, ser.color.w };
            jseries.push_back(js);
        }
        jp["series"] = jseries;
        j_plots.push_back(jp);
    }
    j["plots"] = j_plots;

    return j;
}

// ── Deserialise ─────────────────────────────────────────────────────────────

static void deserialise_app(App& app, const json& j) {
    // Streams
    if (j.contains("streams") && j["streams"].is_array()) {
        for (const auto& s : j["streams"]) {
            std::string name        = s.value("name", "");
            std::string source_type = s.value("source_type", "csv");

            if (source_type == "http" && s.contains("http")) {
                const auto& h = s["http"];
                HttpSource src;
                src.url_template = h.value("url_template", "");
                src.json_path    = h.value("json_path", "");
                if (h.contains("params") && h["params"].is_array()) {
                    for (const auto& p : h["params"]) {
                        src.params.emplace_back(p.value("key",""), p.value("val",""));
                    }
                }
                if (h.contains("field_map") && h["field_map"].is_array()) {
                    for (const auto& fm : h["field_map"]) {
                        FieldMapEntry e;
                        e.output_name = fm.value("output_name","");
                        e.json_key    = fm.value("json_key","");
                        std::string type = fm.value("type","num");
                        if      (type == "ts")  e.type = FieldType::TIMESTAMP;
                        else if (type == "str") e.type = FieldType::STRING;
                        else                    e.type = FieldType::NUMBER;
                        src.field_map.push_back(e);
                    }
                }
                app.stream_store.add_http(name, src);
            } else {
                // CSV: mark as needing re-upload (no raw data stored)
                std::string fname = s.value("csv_filename", "");
                DataStream ds;
                ds.name        = name;
                ds.source_type = SourceType::CSV_FILE;
                ds.csv_source.filename = fname;
                ds.status      = StreamStatus::ERROR_STATE;
                ds.error_msg   = "CSV data not available — please re-upload the file.";
                // Add directly (bypass parse)
                app.stream_store.all().push_back(std::move(ds));
            }
        }
    }

    // Plots
    if (j.contains("plots") && j["plots"].is_array()) {
        for (const auto& jp : j["plots"]) {
            std::string name = jp.value("name", "Plot");
            uint32_t id = app.plot_store.add(name);
            Plot* p = app.plot_store.find(id);
            if (!p) continue;

            p->x_label     = jp.value("x_label", "");
            p->y_label     = jp.value("y_label", "");
            p->y2_label    = jp.value("y2_label", "");
            p->show_legend = jp.value("show_legend", true);

            if (jp.contains("series") && jp["series"].is_array()) {
                for (const auto& js : jp["series"]) {
                    PlotSeries ser;
                    ser.stream_id  = js.value("stream_id", 0u);
                    ser.x_field    = js.value("x_field", "");
                    ser.y_field    = js.value("y_field", "");
                    ser.y_axis     = js.value("y_axis", 0);
                    ser.label      = js.value("label", "");
                    std::string pt = js.value("plot_type", "line");
                    if      (pt == "scatter") ser.plot_type = PlotType::SCATTER;
                    else if (pt == "bar")     ser.plot_type = PlotType::BAR;
                    else if (pt == "step")    ser.plot_type = PlotType::STEP;
                    else                      ser.plot_type = PlotType::LINE;

                    if (js.contains("color") && js["color"].is_array() && js["color"].size() >= 4) {
                        ser.color.x = js["color"][0].get<float>();
                        ser.color.y = js["color"][1].get<float>();
                        ser.color.z = js["color"][2].get<float>();
                        ser.color.w = js["color"][3].get<float>();
                    }
                    p->series.push_back(ser);
                }
            }
        }
    }
}

// ── Public API ──────────────────────────────────────────────────────────────

void config_save(const App& app) {
    json j = serialise_app(app);
    std::string text = j.dump(2);

#ifdef __EMSCRIPTEN__
    js_localstorage_set(kConfigKey, text.c_str());
#else
    std::ofstream f(kConfigFile);
    if (f.is_open()) {
        f << text;
    }
#endif
}

void config_load(App& app) {
    std::string text;

#ifdef __EMSCRIPTEN__
    char buf[65536] = {};
    js_localstorage_get(kConfigKey, buf, sizeof(buf));
    text = buf;
#else
    std::ifstream f(kConfigFile);
    if (f.is_open()) {
        std::ostringstream ss;
        ss << f.rdbuf();
        text = ss.str();
    }
#endif

    if (text.empty()) {
        load_sample_defaults(app);
        return;
    }

    json j = json::parse(text, nullptr, false);
    if (!j.is_discarded()) {
        deserialise_app(app, j);
    }
}
