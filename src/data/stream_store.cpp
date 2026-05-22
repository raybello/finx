#include "data/stream_store.h"
#include "io/csv_parser.h"
#include "io/http_client.h"
#include "io/yfinance_client.h"
#include "expr/expr_parser.h"
#include "expr/expr_evaluator.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>

// ── Window function helpers ───────────────────────────────────────────────────

static const double kWinNaN = std::numeric_limits<double>::quiet_NaN();

static bool is_window_fn(const std::string& n) {
    return n == "sma" || n == "ema"   || n == "stddev" ||
           n == "rmin" || n == "rmax" || n == "roc";
}

static std::vector<double> compute_sma(const std::vector<double>& col, int p) {
    std::vector<double> out(col.size(), kWinNaN);
    if (p <= 0) return out;
    for (size_t i = (size_t)(p - 1); i < col.size(); ++i) {
        double s = 0.0;
        for (int k = 0; k < p; ++k) s += col[i - k];
        out[i] = s / p;
    }
    return out;
}

static std::vector<double> compute_ema(const std::vector<double>& col, int p) {
    std::vector<double> out(col.size(), kWinNaN);
    if (p <= 0 || (int)col.size() < p) return out;
    double k = 2.0 / (p + 1.0);
    double seed = 0.0;
    for (int i = 0; i < p; ++i) seed += col[i];
    out[p - 1] = seed / p;
    for (size_t i = (size_t)p; i < col.size(); ++i)
        out[i] = col[i] * k + out[i - 1] * (1.0 - k);
    return out;
}

static std::vector<double> compute_stddev(const std::vector<double>& col, int p) {
    std::vector<double> out(col.size(), kWinNaN);
    if (p <= 1) return out;
    for (size_t i = (size_t)(p - 1); i < col.size(); ++i) {
        double s = 0.0;
        for (int k = 0; k < p; ++k) s += col[i - k];
        double mean = s / p;
        double sq = 0.0;
        for (int k = 0; k < p; ++k) { double d = col[i - k] - mean; sq += d * d; }
        out[i] = std::sqrt(sq / p);
    }
    return out;
}

static std::vector<double> compute_rmin(const std::vector<double>& col, int p) {
    std::vector<double> out(col.size(), kWinNaN);
    if (p <= 0) return out;
    for (size_t i = (size_t)(p - 1); i < col.size(); ++i) {
        double m = col[i];
        for (int k = 1; k < p; ++k) m = std::min(m, col[i - k]);
        out[i] = m;
    }
    return out;
}

static std::vector<double> compute_rmax(const std::vector<double>& col, int p) {
    std::vector<double> out(col.size(), kWinNaN);
    if (p <= 0) return out;
    for (size_t i = (size_t)(p - 1); i < col.size(); ++i) {
        double m = col[i];
        for (int k = 1; k < p; ++k) m = std::max(m, col[i - k]);
        out[i] = m;
    }
    return out;
}

static std::vector<double> compute_roc(const std::vector<double>& col, int p) {
    std::vector<double> out(col.size(), kWinNaN);
    if (p <= 0) return out;
    for (size_t i = (size_t)p; i < col.size(); ++i) {
        double prev = col[i - p];
        out[i] = (prev == 0.0) ? kWinNaN : (col[i] - prev) / prev * 100.0;
    }
    return out;
}

struct WinCallInfo { std::string fn, alias, key; int period = 0; };

static bool collect_window_calls(const ExprNode& node,
                                  std::vector<WinCallInfo>& out,
                                  std::string& error) {
    if (node.type == ExprNodeType::CALL && is_window_fn(node.name)) {
        if (node.children.size() != 2 ||
            node.children[0].type != ExprNodeType::VARIABLE ||
            node.children[1].type != ExprNodeType::LITERAL) {
            error = "Window function '" + node.name +
                    "': first arg must be a field alias, second must be a literal period "
                    "(e.g., sma(close, 20))";
            return false;
        }
        int p = (int)node.children[1].literal;
        if (p <= 0) {
            error = "Window function '" + node.name + "': period must be > 0";
            return false;
        }
        WinCallInfo w;
        w.fn     = node.name;
        w.alias  = node.children[0].name;
        w.period = p;
        w.key    = w.fn + ":" + w.alias + ":" + std::to_string(p);
        bool dup = false;
        for (const auto& e : out) { if (e.key == w.key) { dup = true; break; } }
        if (!dup) out.push_back(w);
    }
    for (const auto& c : node.children)
        if (!collect_window_calls(c, out, error)) return false;
    return true;
}

uint32_t StreamStore::add_csv(const std::string& name,
                               const std::string& filename,
                               const std::string& raw_text,
                               const std::string& path)
{
    DataStream ds;
    ds.id          = next_id_++;
    ds.name        = name;
    ds.source_type = SourceType::CSV_FILE;
    ds.csv_source.filename = filename;
    ds.csv_source.path     = path;
    ds.csv_source.raw_text = raw_text;
    ds.status = StreamStatus::LOADING;
    streams_.push_back(std::move(ds));

    uint32_t id = streams_.back().id;
    ParsedTable t = parse_csv(raw_text);
    apply_parsed(id, std::move(t));
    return id;
}

uint32_t StreamStore::add_csv_placeholder(const std::string& name, const std::string& filename, const std::string& path) {
    DataStream ds;
    ds.id          = next_id_++;
    ds.name        = name;
    ds.source_type = SourceType::CSV_FILE;
    ds.csv_source.filename = filename;
    ds.csv_source.path     = path;
    ds.status      = StreamStatus::ERROR_STATE;
    ds.error_msg   = "CSV data not available — please re-upload the file.";
    streams_.push_back(std::move(ds));
    return streams_.back().id;
}

uint32_t StreamStore::add_http(const std::string& name, const HttpSource& src) {
    DataStream ds;
    ds.id          = next_id_++;
    ds.name        = name;
    ds.source_type = SourceType::HTTP_GET;
    ds.http_source = src;
    ds.status      = StreamStatus::LOADING;
    streams_.push_back(std::move(ds));
    uint32_t id = streams_.back().id;

    std::string url = build_url(src.url_template, src.params);
    http_get_async(id, url, [this, id, src](bool ok, std::string body) {
        DataStream* ds = find(id);
        if (!ds) return;
        if (!ok) {
            ds->status    = StreamStatus::ERROR_STATE;
            ds->error_msg = body;
            return;
        }
        ParsedTable t = extract_response(body, src);
        apply_parsed(id, std::move(t));
    });

    return id;
}

void StreamStore::update_http(uint32_t id, const std::string& name, const HttpSource& src) {
    DataStream* ds = find(id);
    if (!ds || ds->source_type != SourceType::HTTP_GET) return;
    ds->name        = name;
    ds->http_source = src;
    refresh(id);
}

uint32_t StreamStore::add_yfinance(const std::string& name, const YFinanceSource& src) {
    DataStream ds;
    ds.id          = next_id_++;
    ds.name        = name;
    ds.source_type = SourceType::YFINANCE;
    ds.yf_source   = src;
    ds.status      = StreamStatus::LOADING;
    streams_.push_back(std::move(ds));
    uint32_t id = streams_.back().id;

    yfinance_fetch_async(id, src, [this, id](bool /*ok*/, ParsedTable table) {
        apply_parsed(id, std::move(table));
        reevaluate_dependents(id);
    });
    return id;
}

void StreamStore::update_yfinance(uint32_t id, const std::string& name, const YFinanceSource& src) {
    DataStream* ds = find(id);
    if (!ds || ds->source_type != SourceType::YFINANCE) return;
    ds->name     = name;
    ds->yf_source = src;
    refresh(id);
}

void StreamStore::refresh(uint32_t id) {
    DataStream* ds = find(id);
    if (!ds) return;

    if (ds->source_type == SourceType::HTTP_GET) {
        ds->status = StreamStatus::LOADING;
        std::string url = build_url(ds->http_source.url_template, ds->http_source.params);
        HttpSource src_copy = ds->http_source;
        http_get_async(id, url, [this, id, src_copy](bool ok, std::string body) {
            DataStream* d = find(id);
            if (!d) return;
            if (!ok) {
                d->status    = StreamStatus::ERROR_STATE;
                d->error_msg = body;
                return;
            }
            ParsedTable t = extract_response(body, src_copy);
            apply_parsed(id, std::move(t));
        });
    } else if (ds->source_type == SourceType::YFINANCE) {
        ds->status = StreamStatus::LOADING;
        YFinanceSource src_copy = ds->yf_source;
        yfinance_fetch_async(id, src_copy, [this, id](bool /*ok*/, ParsedTable table) {
            apply_parsed(id, std::move(table));
            reevaluate_dependents(id);
        });
    } else if (ds->source_type == SourceType::CSV_FILE) {
        if (!ds->csv_source.raw_text.empty()) {
            ds->status = StreamStatus::LOADING;
            ParsedTable t = parse_csv(ds->csv_source.raw_text);
            apply_parsed(id, std::move(t));
        } else {
            ds->status    = StreamStatus::ERROR_STATE;
            ds->error_msg = "CSV data not available; please re-upload the file.";
        }
    }
}

void StreamStore::remove(uint32_t id) {
    streams_.erase(
        std::remove_if(streams_.begin(), streams_.end(),
                       [id](const DataStream& ds){ return ds.id == id; }),
        streams_.end());
}

DataStream* StreamStore::find(uint32_t id) {
    for (auto& ds : streams_) {
        if (ds.id == id) return &ds;
    }
    return nullptr;
}

void StreamStore::poll() {
    http_poll_results();
    yfinance_poll_results();
}

uint32_t StreamStore::add_formula(const std::string& name, const FormulaSource& src) {
    DataStream ds;
    ds.id             = next_id_++;
    ds.name           = name;
    ds.source_type    = SourceType::FORMULA;
    ds.formula_source = src;
    ds.status         = StreamStatus::LOADING;
    streams_.push_back(std::move(ds));
    uint32_t id = streams_.back().id;
    evaluate_formula(id);
    return id;
}

bool StreamStore::evaluate_formula(uint32_t id) {
    DataStream* ds = find(id);
    if (!ds || ds->source_type != SourceType::FORMULA) return false;

    const FormulaSource& src = ds->formula_source;

    if (src.expression.empty()) {
        ds->status    = StreamStatus::ERROR_STATE;
        ds->error_msg = "Expression is empty";
        return false;
    }

    ParseResult pr = parse_expr(src.expression);
    if (!pr.ok) {
        ds->status    = StreamStatus::ERROR_STATE;
        ds->error_msg = "Parse error: " + pr.error;
        return false;
    }

    DataStream* x_ds = find(src.x_stream_id);
    if (!x_ds) {
        ds->status    = StreamStatus::ERROR_STATE;
        ds->error_msg = "X-axis stream not found";
        return false;
    }
    auto x_col_it = x_ds->columns.find(src.x_field);
    if (x_col_it == x_ds->columns.end()) {
        ds->status    = StreamStatus::ERROR_STATE;
        ds->error_msg = "X-axis field '" + src.x_field + "' not found";
        return false;
    }
    const std::vector<double>& x_col = x_col_it->second;

    // Pre-compute window functions found in the expression
    WinCols win_cols;
    {
        std::vector<WinCallInfo> win_calls;
        std::string wc_err;
        if (!collect_window_calls(pr.root, win_calls, wc_err)) {
            ds->status    = StreamStatus::ERROR_STATE;
            ds->error_msg = wc_err;
            return false;
        }
        for (const auto& wc : win_calls) {
            const FormulaBinding* binding = nullptr;
            for (const auto& b : src.bindings)
                if (b.alias == wc.alias) { binding = &b; break; }
            if (!binding) {
                ds->status    = StreamStatus::ERROR_STATE;
                ds->error_msg = "Window function '" + wc.fn + "': alias '" +
                                wc.alias + "' not bound";
                return false;
            }
            DataStream* bds = find(binding->stream_id);
            if (!bds) {
                ds->status    = StreamStatus::ERROR_STATE;
                ds->error_msg = "Window function '" + wc.fn + "': source stream not found";
                return false;
            }
            auto col_it = bds->columns.find(binding->field_name);
            if (col_it == bds->columns.end()) {
                ds->status    = StreamStatus::ERROR_STATE;
                ds->error_msg = "Window function '" + wc.fn + "': field '" +
                                binding->field_name + "' not found";
                return false;
            }
            const auto& col = col_it->second;
            if      (wc.fn == "sma")    win_cols[wc.key] = compute_sma(col, wc.period);
            else if (wc.fn == "ema")    win_cols[wc.key] = compute_ema(col, wc.period);
            else if (wc.fn == "stddev") win_cols[wc.key] = compute_stddev(col, wc.period);
            else if (wc.fn == "rmin")   win_cols[wc.key] = compute_rmin(col, wc.period);
            else if (wc.fn == "rmax")   win_cols[wc.key] = compute_rmax(col, wc.period);
            else if (wc.fn == "roc")    win_cols[wc.key] = compute_roc(col, wc.period);
        }
    }

    // Build x-value -> row-index maps for streams that differ from x_stream
    std::map<uint32_t, std::unordered_map<double, size_t>> x_index_maps;
    for (const auto& b : src.bindings) {
        if (b.stream_id == src.x_stream_id) continue;
        if (x_index_maps.count(b.stream_id)) continue;

        DataStream* bs = find(b.stream_id);
        if (!bs) {
            ds->status    = StreamStatus::ERROR_STATE;
            ds->error_msg = "Binding stream not found for alias '" + b.alias + "'";
            return false;
        }
        auto xf_it = bs->columns.find(src.x_field);
        if (xf_it == bs->columns.end()) {
            ds->status    = StreamStatus::ERROR_STATE;
            ds->error_msg = "X-field '" + src.x_field +
                            "' not found in stream '" + bs->name + "' for cross-stream alignment";
            return false;
        }
        auto& xmap = x_index_maps[b.stream_id];
        const auto& xcol = xf_it->second;
        for (size_t i = 0; i < xcol.size(); ++i) {
            xmap[xcol[i]] = i;
        }
    }

    // Evaluate row by row
    std::vector<double> x_out, result_out;
    x_out.reserve(x_col.size());
    result_out.reserve(x_col.size());

    for (size_t i = 0; i < x_col.size(); ++i) {
        double x_val = x_col[i];
        VarMap vars;
        bool all_found = true;

        for (const auto& b : src.bindings) {
            DataStream* bs = find(b.stream_id);
            if (!bs) { all_found = false; break; }

            auto col_it = bs->columns.find(b.field_name);
            if (col_it == bs->columns.end()) { all_found = false; break; }
            const auto& col = col_it->second;

            if (b.stream_id == src.x_stream_id) {
                if (i < col.size()) {
                    vars[b.alias] = col[i];
                } else {
                    all_found = false;
                }
            } else {
                auto xm_it = x_index_maps.find(b.stream_id);
                if (xm_it == x_index_maps.end()) { all_found = false; break; }
                auto idx_it = xm_it->second.find(x_val);
                if (idx_it == xm_it->second.end()) { all_found = false; break; }
                size_t row = idx_it->second;
                if (row < col.size()) {
                    vars[b.alias] = col[row];
                } else {
                    all_found = false;
                }
            }
            if (!all_found) break;
        }

        if (all_found) {
            x_out.push_back(x_val);
            double res = win_cols.empty()
                ? eval_node(pr.root, vars)
                : eval_node(pr.root, vars, i, win_cols);
            result_out.push_back(res);
        }
    }

    // Determine x-field type from x_stream schema
    FieldType x_field_type = FieldType::NUMBER;
    for (const auto& fd : x_ds->schema) {
        if (fd.name == src.x_field) { x_field_type = fd.type; break; }
    }

    ds->columns.clear();
    ds->str_columns.clear();
    ds->schema.clear();
    ds->columns[src.x_field]     = std::move(x_out);
    ds->columns[src.result_name] = std::move(result_out);
    ds->schema.push_back({ src.x_field,     x_field_type });
    ds->schema.push_back({ src.result_name, FieldType::NUMBER });
    ds->row_count  = ds->columns[src.x_field].size();
    ds->status     = StreamStatus::OK;
    ds->error_msg.clear();
    return true;
}

void StreamStore::reevaluate_dependents(uint32_t upstream_id) {
    for (auto& ds : streams_) {
        if (ds.source_type != SourceType::FORMULA) continue;
        bool depends = (ds.formula_source.x_stream_id == upstream_id);
        if (!depends) {
            for (const auto& b : ds.formula_source.bindings) {
                if (b.stream_id == upstream_id) { depends = true; break; }
            }
        }
        if (depends) evaluate_formula(ds.id);
    }
}

void StreamStore::apply_parsed(uint32_t id, ParsedTable&& t) {
    DataStream* ds = find(id);
    if (!ds) return;

    if (!t.error.empty()) {
        ds->status    = StreamStatus::ERROR_STATE;
        ds->error_msg = t.error;
        return;
    }

    ds->schema        = std::move(t.schema);
    ds->columns       = std::move(t.columns);
    ds->str_columns   = std::move(t.str_columns);
    ds->row_count     = t.row_count;
    ds->status        = StreamStatus::OK;
    ds->error_msg.clear();
    ds->data_changed  = true;
}
