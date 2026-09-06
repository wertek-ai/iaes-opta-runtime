#include "IaesCanonical.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace iaes {
namespace {

// A payload with more keys than this is refused rather than reordered wrongly.
constexpr size_t MAX_KEYS = 24;

struct Out {
    char*  buf;
    size_t cap;
    size_t len;
    bool   ok;

    void put(char c) {
        if (!ok) return;
        if (len + 1 >= cap) { ok = false; return; }
        buf[len++] = c;
    }
    void put(const char* s) { while (*s && ok) put(*s++); }
};

void writeString(Out& o, const char* s) {
    o.put('"');
    for (const unsigned char* p = (const unsigned char*)s; *p && o.ok; p++) {
        switch (*p) {
            case '"':  o.put("\\\""); break;
            case '\\': o.put("\\\\"); break;
            case '\b': o.put("\\b");  break;
            case '\f': o.put("\\f");  break;
            case '\n': o.put("\\n");  break;
            case '\r': o.put("\\r");  break;
            case '\t': o.put("\\t");  break;
            default:
                if (*p < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", (unsigned)*p);
                    o.put(esc);
                } else {
                    // Bytes >= 0x80 pass through as UTF-8. Python's json.dumps
                    // escapes them by default, so a non-ASCII string still
                    // hashes differently across languages. That is a gap in the
                    // specification, recorded rather than papered over here.
                    o.put((char)*p);
                }
        }
    }
    o.put('"');
}

void writeNumber(Out& o, double v) {
    if (isnan(v) || isinf(v)) { o.put("null"); return; }
    char buf[32];
    // Whole numbers lose the decimal point, matching the normalization both
    // other SDKs apply before hashing: 25600.0 must canonicalize as 25600.
    if (v == (double)(long long)v && v < 1e15 && v > -1e15) {
        snprintf(buf, sizeof(buf), "%lld", (long long)v);
    } else {
        // Shortest form that still round-trips, so that 0.1 does not become
        // 0.10000000000000001 here and 0.1 everywhere else -- and so that a
        // value written as 0.80 hashes the same as one written as 0.8.
        snprintf(buf, sizeof(buf), "%.17g", v);
        for (int prec = 1; prec <= 17; prec++) {
            char cand[32];
            snprintf(cand, sizeof(cand), "%.*g", prec, v);
            double back = 0;
            if (sscanf(cand, "%lf", &back) == 1 && back == v) {
                strcpy(buf, cand);
                break;
            }
        }
    }
    o.put(buf);
}

void write(Out& o, JsonVariantConst v);

void writeObject(Out& o, JsonObjectConst obj) {
    const char* keys[MAX_KEYS];
    size_t n = 0;
    for (JsonPairConst kv : obj) {
        if (n >= MAX_KEYS) { o.ok = false; return; }
        keys[n++] = kv.key().c_str();
    }
    // Insertion sort: n is small, and this costs no allocation.
    for (size_t i = 1; i < n; i++) {
        const char* k = keys[i];
        size_t j = i;
        while (j > 0 && strcmp(keys[j - 1], k) > 0) { keys[j] = keys[j - 1]; j--; }
        keys[j] = k;
    }
    o.put('{');
    for (size_t i = 0; i < n && o.ok; i++) {
        if (i) o.put(',');
        writeString(o, keys[i]);
        o.put(':');
        write(o, obj[keys[i]]);
    }
    o.put('}');
}

void write(Out& o, JsonVariantConst v) {
    if (v.is<JsonObjectConst>()) {
        writeObject(o, v.as<JsonObjectConst>());
    } else if (v.is<JsonArrayConst>()) {
        o.put('[');
        bool first = true;
        for (JsonVariantConst e : v.as<JsonArrayConst>()) {
            if (!first) o.put(',');
            first = false;
            write(o, e);
        }
        o.put(']');
    } else if (v.isNull()) {
        o.put("null");
    } else if (v.is<bool>()) {
        o.put(v.as<bool>() ? "true" : "false");
    } else if (v.is<const char*>()) {
        writeString(o, v.as<const char*>());
    } else if (v.is<double>()) {
        writeNumber(o, v.as<double>());
    } else {
        o.ok = false;
    }
}

}  // namespace

size_t canonicalize(JsonVariantConst value, char* out, size_t len) {
    if (!out || len == 0) return 0;
    Out o{out, len, 0, true};
    write(o, value);
    if (!o.ok) { out[0] = '\0'; return 0; }
    out[o.len] = '\0';
    return o.len;
}

}  // namespace iaes
