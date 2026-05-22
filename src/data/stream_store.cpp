#include "data/stream_store.h"
#include "io/csv_parser.h"
#include "io/http_client.h"
#include "expr/expr_parser.h"
#include "expr/expr_evaluator.h"
#include <algorithm>
#include <map>

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
        ParsedTable t = extract_json(body, src.json_path, src.field_map);
        apply_parsed(id, std::move(t));
    });

    return id;
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
            ParsedTable t = extract_json(body, src_copy.json_path, src_copy.field_map);
            apply_parsed(id, std::move(t));
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
            result_out.push_back(eval_node(pr.root, vars));
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

    ds->schema      = std::move(t.schema);
    ds->columns     = std::move(t.columns);
    ds->str_columns = std::move(t.str_columns);
    ds->row_count   = t.row_count;
    ds->status      = StreamStatus::OK;
    ds->error_msg.clear();
}
