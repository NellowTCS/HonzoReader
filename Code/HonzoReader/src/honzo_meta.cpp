#include "honzo_meta.h"
#include <cpp/free_functions.hpp>
#include <cstring>

static const char* skip_ws(const char* p) {
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    return p;
}

static const char* scan_quoted(const char* p, char* out, int max) {
    if (*p != '"') return p;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < max - 1) {
        if (*p == '\\' && *(p + 1)) p++;
        out[i++] = *p;
        p++;
    }
    out[i] = '\0';
    if (*p == '"') p++;
    return p;
}

// Find "key": in JSON and return pointer to the value start (after ':')
// Returns nullptr if key not found.
static const char* find_key(const char* json, const char* key) {
    size_t klen = strlen(key);
    const char* p = json;
    while (*p) {
        if (*p == '"') {
            // Check if this is our key
            if (strncmp(p + 1, key, klen) == 0 && p[1 + klen] == '"') {
                p += klen + 2;
                p = skip_ws(p);
                if (*p == ':') {
                    p++;
                    p = skip_ws(p);
                    return p;
                }
            }
            // Skip past this string value
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && *(p + 1)) p++;
                p++;
            }
            if (*p) p++;
        } else {
            p++;
        }
    }
    return nullptr;
}

bool honzo_scan_meta_json(const char* json,
                          char* title, int title_max,
                          char* author, int author_max)
{
    if (title && title_max > 0) title[0] = '\0';
    if (author && author_max > 0) author[0] = '\0';
    if (!json) return false;

    const char* p = json;

    // Extract title
    if (title && title_max > 0) {
        p = find_key(json, "title");
        if (p) {
            if (*p == '{') {
                p++;
                while (*p && *p != '}') {
                    p = skip_ws(p);
                    if (*p != '"') { p++; continue; }
                    char lang[8] = {};
                    const char* after = scan_quoted(p, lang, sizeof(lang));
                    if (after == p) { p++; continue; }
                    p = skip_ws(after);
                    if (*p == ':') p = skip_ws(p + 1);
                    if (strcmp(lang, "en") == 0 || title[0] == '\0') {
                        char tmp[128];
                        p = scan_quoted(p, tmp, sizeof(tmp));
                        strncpy(title, tmp, title_max - 1);
                        if (strcmp(lang, "en") == 0) break;
                    } else {
                        if (*p == '"') { char d[8]; p = scan_quoted(p, d, sizeof(d)); }
                    }
                    p = skip_ws(p);
                    if (*p == ',') p++;
                }
            } else if (*p == '"') {
                scan_quoted(p, title, title_max);
            }
        }
    }

    // Extract first author
    if (author && author_max > 0) {
        p = find_key(json, "authors");
        if (p && *p == '[') {
            p++;
            p = skip_ws(p);
            if (*p == '"') {
                scan_quoted(p, author, author_max);
            }
        }
    }

    return title[0] != '\0' || author[0] != '\0';
}

bool honzo_extract_meta(const char* file_path,
                         char* title, int title_max,
                         char* author, int author_max)
{
    if (title && title_max > 0) title[0] = '\0';
    if (author && author_max > 0) author[0] = '\0';

    auto result = hzo_extract_meta_from_file(std::string_view(file_path), 1);
    if (!result.is_ok()) return false;
    auto inner_opt = std::move(result).ok();
    if (!inner_opt.has_value()) return false;
    auto inner_result = std::move(*inner_opt);
    if (!inner_result.is_ok()) return false;
    auto json_opt = std::move(inner_result).ok();
    if (!json_opt.has_value()) return false;
    std::string json = std::move(*json_opt);

    return honzo_scan_meta_json(json.c_str(), title, title_max, author, author_max);
}

void honzo_extract_heading(const char* text, size_t len, uint8_t markup,
                            char* out, int max_len)
{
    out[0] = '\0';
    if (!text || len == 0 || max_len <= 1) return;

    const char* end = text + len;

    if (markup == 0) {
        // Markdown: line starting with #, ##, etc.
        const char* p = text;
        while (p < end) {
            if (*p == '\r' || *p == '\n') { p++; continue; }
            if (*p == '#') {
                while (p < end && *p == '#') p++;
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                const char* start = p;
                while (p < end && *p != '\n' && *p != '\r') p++;
                int tlen = (int)(p - start);
                if (tlen > 0 && tlen < max_len) {
                    memcpy(out, start, tlen);
                    out[tlen] = '\0';
                }
                return;
            }
            while (p < end && *p != '\n') p++;
        }
    } else {
        // HTML: <h1>, <H1>, <h2>, etc.
        const char* p = text;
        while (p < end) {
            if (*p == '<' && (p + 2 < end) &&
                (p[1] == 'h' || p[1] == 'H') &&
                p[2] >= '1' && p[2] <= '6')
            {
                const char* tag_end = (const char*)memchr(p, '>', (size_t)(end - p));
                if (!tag_end || tag_end + 1 >= end) return;
                const char* content = tag_end + 1;
                const char* close = (const char*)memchr(content, '<', (size_t)(end - content));
                if (!close) return;
                int tlen = (int)(close - content);
                if (tlen > 0 && tlen < max_len) {
                    memcpy(out, content, tlen);
                    out[tlen] = '\0';
                }
                return;
            }
            p++;
        }
    }
}
