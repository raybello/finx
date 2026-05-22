#include "data/stream_store.h"
#include "io/csv_parser.h"
#include "io/http_client.h"
#include <algorithm>

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
