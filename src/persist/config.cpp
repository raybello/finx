#include "persist/config.h"
#include "app.h"
#include "data/stream_store.h"
#include "data/plot_store.h"
#include "data/sample_data.h"
#include "data/types.h"
#include "ui/modals.h"
#include "json.hpp"
#include <string>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <unordered_map>

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
        const char* src_type_str = "csv";
        if (ds.source_type == SourceType::HTTP_GET) src_type_str = "http";
        if (ds.source_type == SourceType::FORMULA)  src_type_str = "formula";
        if (ds.source_type == SourceType::YFINANCE) src_type_str = "yfinance";
        s["source_type"] = src_type_str;

        if (ds.source_type == SourceType::FORMULA) {
            json f;
            f["expression"]  = ds.formula_source.expression;
            f["result_name"] = ds.formula_source.result_name;
            f["x_stream_id"] = ds.formula_source.x_stream_id;
            f["x_field"]     = ds.formula_source.x_field;
            json jb = json::array();
            for (const auto& b : ds.formula_source.bindings) {
                jb.push_back({ {"alias", b.alias},
                               {"stream_id", b.stream_id},
                               {"field_name", b.field_name} });
            }
            f["bindings"] = jb;
            s["formula"]  = f;
        } else if (ds.source_type == SourceType::HTTP_GET) {
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
            switch (ds.http_source.response_format) {
                case ResponseFormat::JSON: h["response_format"] = "json"; break;
                case ResponseFormat::CSV:  h["response_format"] = "csv";  break;
                default:                   h["response_format"] = "auto"; break;
            }
            s["http"]      = h;
        } else if (ds.source_type == SourceType::YFINANCE) {
            json yf;
            yf["ticker"]   = ds.yf_source.ticker;
            yf["period"]   = ds.yf_source.period;
            yf["interval"] = ds.yf_source.interval;
            s["yfinance"]  = yf;
        } else if (ds.source_type == SourceType::CSV_FILE) {
            s["csv_filename"] = ds.csv_source.filename;
            s["csv_path"]     = ds.csv_source.path;
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

    // HTTP draft — last-used URL/params so the modal pre-fills on next open
    HttpSource draft = modals_get_http_draft();
    if (!draft.url_template.empty()) {
        json jd;
        jd["url_template"] = draft.url_template;
        jd["json_path"]    = draft.json_path;
        json dparams = json::array();
        for (const auto& kv : draft.params)
            dparams.push_back({ {"key", kv.first}, {"val", kv.second} });
        jd["params"] = dparams;
        json dfmap = json::array();
        for (const auto& fm : draft.field_map) {
            json f;
            f["output_name"] = fm.output_name;
            f["json_key"]    = fm.json_key;
            switch (fm.type) {
                case FieldType::TIMESTAMP: f["type"] = "ts";  break;
                case FieldType::STRING:    f["type"] = "str"; break;
                default:                   f["type"] = "num"; break;
            }
            dfmap.push_back(f);
        }
        jd["field_map"] = dfmap;
        switch (draft.response_format) {
            case ResponseFormat::JSON: jd["response_format"] = "json"; break;
            case ResponseFormat::CSV:  jd["response_format"] = "csv";  break;
            default:                   jd["response_format"] = "auto"; break;
        }
        j["http_draft"] = jd;
    }

    return j;
}

// ── Deserialise ─────────────────────────────────────────────────────────────

static void deserialise_app(App& app, const json& j) {
    // saved_id -> new_id remap built while deserialising streams
    std::unordered_map<uint32_t, uint32_t> id_remap;

    // Pass 1: load CSV, HTTP, and yfinance streams, building id_remap
    if (j.contains("streams") && j["streams"].is_array()) {
        for (const auto& s : j["streams"]) {
            std::string source_type = s.value("source_type", "csv");
            if (source_type == "formula") continue; // handled in pass 2

            uint32_t saved_id = s.value("id", 0u);
            std::string name  = s.value("name", "");
            uint32_t new_id   = 0;

            if (source_type == "yfinance" && s.contains("yfinance")) {
#ifndef __EMSCRIPTEN__
                const auto& yf = s["yfinance"];
                YFinanceSource src;
                src.ticker   = yf.value("ticker",   "AAPL");
                src.period   = yf.value("period",   "1mo");
                src.interval = yf.value("interval", "1d");
                new_id = app.stream_store.add_yfinance(name, src);
#endif
            } else if (source_type == "http" && s.contains("http")) {
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
                {
                    std::string rfmt = h.value("response_format", "auto");
                    if      (rfmt == "json") src.response_format = ResponseFormat::JSON;
                    else if (rfmt == "csv")  src.response_format = ResponseFormat::CSV;
                    else                     src.response_format = ResponseFormat::AUTO;
                }
                new_id = app.stream_store.add_http(name, src);
            } else {
                std::string fname = s.value("csv_filename", "");
                std::string fpath = s.value("csv_path", "");
                bool loaded = false;

#ifndef __EMSCRIPTEN__
                if (!fpath.empty()) {
                    std::ifstream f(fpath);
                    if (f.is_open()) {
                        std::ostringstream oss;
                        oss << f.rdbuf();
                        std::string raw = oss.str();
                        if (!raw.empty()) {
                            new_id = app.stream_store.add_csv(name, fname, raw, fpath);
                            loaded = true;
                        }
                    }
                }
#endif

                if (!loaded) {
                    const char* embedded = get_embedded_csv(fname.c_str());
                    if (embedded) {
                        new_id = app.stream_store.add_csv(name, fname, embedded);
                        loaded = true;
                    }
                }

                if (!loaded) {
                    new_id = app.stream_store.add_csv_placeholder(name, fname, fpath);
                }
            }

            id_remap[saved_id] = new_id;
        }
    }

    // Pass 2: load formula streams with remapped IDs
    if (j.contains("streams") && j["streams"].is_array()) {
        for (const auto& s : j["streams"]) {
            std::string source_type = s.value("source_type", "csv");
            if (source_type != "formula" || !s.contains("formula")) continue;

            uint32_t saved_id = s.value("id", 0u);
            std::string name  = s.value("name", "");

            const auto& f = s["formula"];
            FormulaSource src;
            src.expression  = f.value("expression", "");
            src.result_name = f.value("result_name", "result");
            src.x_field     = f.value("x_field", "");

            uint32_t saved_xid = f.value("x_stream_id", 0u);
            auto xid_it = id_remap.find(saved_xid);
            src.x_stream_id = (xid_it != id_remap.end()) ? xid_it->second : saved_xid;

            if (f.contains("bindings") && f["bindings"].is_array()) {
                for (const auto& b : f["bindings"]) {
                    FormulaBinding fb;
                    fb.alias      = b.value("alias", "");
                    fb.field_name = b.value("field_name", "");
                    uint32_t saved_bid = b.value("stream_id", 0u);
                    auto bid_it = id_remap.find(saved_bid);
                    fb.stream_id = (bid_it != id_remap.end()) ? bid_it->second : saved_bid;
                    src.bindings.push_back(fb);
                }
            }

            uint32_t new_id = app.stream_store.add_formula(name, src);
            id_remap[saved_id] = new_id;
        }
    }

    // HTTP draft
    if (j.contains("http_draft")) {
        const auto& jd = j["http_draft"];
        HttpSource draft;
        draft.url_template = jd.value("url_template", "");
        draft.json_path    = jd.value("json_path", "");
        if (jd.contains("params") && jd["params"].is_array()) {
            for (const auto& p : jd["params"])
                draft.params.emplace_back(p.value("key",""), p.value("val",""));
        }
        if (jd.contains("field_map") && jd["field_map"].is_array()) {
            for (const auto& fm : jd["field_map"]) {
                FieldMapEntry e;
                e.output_name = fm.value("output_name","");
                e.json_key    = fm.value("json_key","");
                std::string type = fm.value("type","num");
                if      (type == "ts")  e.type = FieldType::TIMESTAMP;
                else if (type == "str") e.type = FieldType::STRING;
                else                    e.type = FieldType::NUMBER;
                draft.field_map.push_back(e);
            }
        }
        {
            std::string rfmt = jd.value("response_format", "auto");
            if      (rfmt == "json") draft.response_format = ResponseFormat::JSON;
            else if (rfmt == "csv")  draft.response_format = ResponseFormat::CSV;
            else                     draft.response_format = ResponseFormat::AUTO;
        }
        modals_set_http_draft(draft);
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
                    uint32_t saved_sid = js.value("stream_id", 0u);
                    auto it = id_remap.find(saved_sid);
                    ser.stream_id  = (it != id_remap.end()) ? it->second : saved_sid;
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
