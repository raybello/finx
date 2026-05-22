#include "io/http_client.h"
#include "io/csv_parser.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>

// nlohmann JSON
#include "json.hpp"
using json = nlohmann::json;

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fetch.h>
#else
#include <curl/curl.h>
#include <thread>
#include <mutex>
#endif

// ── URL builder ────────────────────────────────────────────────────────────

std::string build_url(const std::string& url_template,
                      const std::vector<std::pair<std::string,std::string>>& params)
{
    std::string result = url_template;
    for (const auto& kv : params) {
        std::string placeholder = "{" + kv.first + "}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.size(), kv.second);
            pos += kv.second.size();
        }
    }
    // Also build query string for any params not embedded as {key}
    std::string query;
    for (const auto& kv : params) {
        std::string placeholder = "{" + kv.first + "}";
        if (result.find(placeholder) == std::string::npos) {
            // param not used as template var — append to query string
            if (!query.empty()) query += "&";
            query += kv.first + "=" + kv.second;
        }
    }
    if (!query.empty()) {
        result += (result.find('?') == std::string::npos ? "?" : "&");
        result += query;
    }
    return result;
}

// ── JSON extraction ────────────────────────────────────────────────────────

static double parse_timestamp_value(const std::string& s) {
    char* end;
    double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str()) {
        if (v > 1e12) return v / 1000.0;
        return v;
    }
    // Try ISO date
    if (s.size() >= 10 && s[4] == '-' && s[7] == '-') {
        struct tm t{};
        t.tm_year = (int)std::strtol(s.substr(0,4).c_str(), nullptr, 10) - 1900;
        t.tm_mon  = (int)std::strtol(s.substr(5,2).c_str(), nullptr, 10) - 1;
        t.tm_mday = (int)std::strtol(s.substr(8,2).c_str(), nullptr, 10);
        if (s.size() >= 19 && s[10] == 'T') {
            t.tm_hour = (int)std::strtol(s.substr(11,2).c_str(), nullptr, 10);
            t.tm_min  = (int)std::strtol(s.substr(14,2).c_str(), nullptr, 10);
            t.tm_sec  = (int)std::strtol(s.substr(17,2).c_str(), nullptr, 10);
        }
        t.tm_isdst = -1;
#ifdef _WIN32
        time_t epoch = _mkgmtime(&t);
#else
        time_t epoch = timegm(&t);
#endif
        if (epoch != -1) return static_cast<double>(epoch);
    }
    return std::numeric_limits<double>::quiet_NaN();
}

ParsedTable extract_json(const std::string& json_text,
                         const std::string& json_path,
                         const std::vector<FieldMapEntry>& field_map)
{
    ParsedTable result;

    json root = json::parse(json_text, nullptr, false);
    if (root.is_discarded()) {
        result.error = "JSON parse error";
        return result;
    }

    // Traverse json_path (dot-separated)
    json* node = &root;
    if (!json_path.empty()) {
        std::string path = json_path;
        size_t pos = 0;
        while (pos < path.size()) {
            size_t dot = path.find('.', pos);
            std::string key = (dot == std::string::npos) ? path.substr(pos) : path.substr(pos, dot - pos);
            pos = (dot == std::string::npos) ? path.size() : dot + 1;
            if (!key.empty()) {
                if (node->is_object() && node->contains(key)) {
                    node = &(*node)[key];
                } else {
                    result.error = "JSON path key not found: " + key;
                    return result;
                }
            }
        }
    }

    if (!node->is_array()) {
        result.error = "JSON path does not point to an array";
        return result;
    }

    // Build schema from field_map
    for (const auto& fm : field_map) {
        FieldDef fd;
        fd.name = fm.output_name;
        fd.type = fm.type;
        result.schema.push_back(fd);
    }

    // Parse rows
    for (const auto& item : *node) {
        if (!item.is_object()) continue;
        for (const auto& fm : field_map) {
            if (!item.contains(fm.json_key)) {
                // Insert default
                if (fm.type == FieldType::STRING) {
                    result.str_columns[fm.output_name].push_back("");
                } else {
                    result.columns[fm.output_name].push_back(0.0);
                }
                continue;
            }
            const auto& val = item[fm.json_key];

            if (fm.type == FieldType::STRING) {
                std::string sv;
                if (val.is_string()) sv = val.get<std::string>();
                else if (!val.is_null()) sv = val.dump();
                result.str_columns[fm.output_name].push_back(sv);
            } else if (fm.type == FieldType::TIMESTAMP) {
                double ts = std::numeric_limits<double>::quiet_NaN();
                if (val.is_number()) {
                    double v = val.get<double>();
                    ts = (v > 1e12) ? v / 1000.0 : v;
                } else if (val.is_string()) {
                    ts = parse_timestamp_value(val.get<std::string>());
                }
                result.columns[fm.output_name].push_back(std::isnan(ts) ? 0.0 : ts);
            } else {
                // NUMBER
                double v = 0.0;
                if (val.is_number()) {
                    v = val.get<double>();
                } else if (val.is_string()) {
                    const std::string sv = val.get<std::string>();
                    char* endp;
                    v = std::strtod(sv.c_str(), &endp);
                    if (endp == sv.c_str()) v = 0.0;
                } else if (val.is_boolean()) {
                    v = val.get<bool>() ? 1.0 : 0.0;
                }
                result.columns[fm.output_name].push_back(v);
            }
        }
        ++result.row_count;
    }

    return result;
}

// ── Platform-specific HTTP ─────────────────────────────────────────────────

#ifdef __EMSCRIPTEN__

struct FetchUD {
    uint32_t    sid;
    FetchCallback cb;
};

static void on_success(emscripten_fetch_t* f) {
    auto* ud = reinterpret_cast<FetchUD*>(f->userData);
    ud->cb(true, std::string(f->data, f->numBytes));
    delete ud;
    emscripten_fetch_close(f);
}

static void on_error(emscripten_fetch_t* f) {
    auto* ud = reinterpret_cast<FetchUD*>(f->userData);
    ud->cb(false, "HTTP error " + std::to_string(f->status));
    delete ud;
    emscripten_fetch_close(f);
}

void http_get_async(uint32_t stream_id, const std::string& url, FetchCallback cb) {
    auto* ud = new FetchUD{stream_id, std::move(cb)};
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    std::strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess  = on_success;
    attr.onerror    = on_error;
    attr.userData   = ud;
    emscripten_fetch(&attr, url.c_str());
}

void http_poll_results() {
    // No-op on Emscripten; callbacks fire directly
}

#else // Native (libcurl)

struct PendingResult {
    FetchCallback cb;
    bool ok;
    std::string body;
};

static std::vector<PendingResult> g_result_queue;
static std::mutex                 g_queue_mutex;

static size_t curl_write(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

void http_get_async(uint32_t /*stream_id*/, const std::string& url, FetchCallback cb) {
    std::thread([url, cb = std::move(cb)]() mutable {
        CURL* curl = curl_easy_init();
        if (!curl) {
            std::lock_guard<std::mutex> lock(g_queue_mutex);
            g_result_queue.push_back({std::move(cb), false, "curl_easy_init failed"});
            return;
        }

        std::string body;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_cleanup(curl);

        bool ok = (res == CURLE_OK && http_code >= 200 && http_code < 300);
        std::string err_body = ok ? body : ("HTTP " + std::to_string(http_code) + ": " + curl_easy_strerror(res));

        std::lock_guard<std::mutex> lock(g_queue_mutex);
        g_result_queue.push_back({std::move(cb), ok, ok ? std::move(body) : std::move(err_body)});
    }).detach();
}

void http_poll_results() {
    std::vector<PendingResult> batch;
    {
        std::lock_guard<std::mutex> lock(g_queue_mutex);
        batch = std::move(g_result_queue);
        g_result_queue.clear();
    }
    for (auto& r : batch) {
        r.cb(r.ok, std::move(r.body));
    }
}

#endif
