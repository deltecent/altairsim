#include "util/json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace altair {

static void escapeByte(unsigned char c, std::string& o) {
    char b[8];
    std::snprintf(b, sizeof b, "\\u%04x", c);
    o += b;
}

static void escape(const std::string& s, std::string& o) {
    o += '"';
    const size_t n = s.size();
    for (size_t i = 0; i < n;) {
        const unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':  o += "\\\""; ++i; continue;
        case '\\': o += "\\\\"; ++i; continue;
        case '\n': o += "\\n"; ++i; continue;
        case '\r': o += "\\r"; ++i; continue;
        case '\t': o += "\\t"; ++i; continue;
        }
        if (c < 0x20) { escapeByte(c, o); ++i; continue; }  // other control chars
        if (c < 0x80) { o += (char)c; ++i; continue; }      // plain ASCII

        // A high byte: JSON must be valid UTF-8, and the strings passing through here
        // are two kinds at once -- a host path that really is UTF-8, and 8-bit-clean
        // bytes off a serial line that are not text at all (a guest terminal can print
        // any byte). So pass a VALID multibyte sequence through untouched, and escape a
        // lone or malformed byte as \u00XX -- lossless per byte, and never invalid JSON.
        const int len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 0;
        bool ok = len > 0 && i + (size_t)len <= n;
        for (int k = 1; ok && k < len; ++k)
            if (((unsigned char)s[i + k] & 0xC0) != 0x80) ok = false;
        if (ok) { o.append(s, i, (size_t)len); i += (size_t)len; }
        else    { escapeByte(c, o); ++i; }
    }
    o += '"';
}

std::string Json::dump() const {
    std::string o;
    switch (t_) {
    case T::Null: o = "null"; break;
    case T::Bool: o = num_ != 0 ? "true" : "false"; break;
    case T::Num: {
        char b[40];
        if (num_ == std::floor(num_) && std::fabs(num_) < 1e15)
            std::snprintf(b, sizeof b, "%lld", (long long)num_);
        else
            std::snprintf(b, sizeof b, "%g", num_);
        o = b;
        break;
    }
    case T::Str: escape(str_, o); break;
    case T::Arr: {
        o = "[";
        bool first = true;
        for (const auto& v : arr_) {
            if (!first) o += ",";
            first = false;
            o += v.dump();
        }
        o += "]";
        break;
    }
    case T::Obj: {
        o = "{";
        bool first = true;
        for (const auto& [k, v] : obj_) {
            if (!first) o += ",";
            first = false;
            escape(k, o);
            o += ":";
            o += v.dump();
        }
        o += "}";
        break;
    }
    }
    return o;
}

namespace {

struct P {
    const std::string& s;
    size_t i = 0;
    std::string err;

    void ws() {
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    }
    bool lit(const char* t) {
        size_t n = std::strlen(t);
        if (s.compare(i, n, t) == 0) {
            i += n;
            return true;
        }
        return false;
    }
    bool value(Json& out);

    bool string(std::string& out) {
        if (i >= s.size() || s[i] != '"') {
            err = "expected string";
            return false;
        }
        ++i;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                ++i;
                switch (s[i]) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (i + 4 >= s.size()) {
                        err = "bad \\u";
                        return false;
                    }
                    int cp = (int)std::strtol(s.substr(i + 1, 4).c_str(), nullptr, 16);
                    i += 4;
                    // Enough UTF-8 for the BMP; MCP payloads are text.
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800) {
                        out += (char)(0xC0 | (cp >> 6));
                        out += (char)(0x80 | (cp & 0x3F));
                    } else {
                        out += (char)(0xE0 | (cp >> 12));
                        out += (char)(0x80 | ((cp >> 6) & 0x3F));
                        out += (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: out += s[i];
                }
                ++i;
            } else {
                out += s[i++];
            }
        }
        if (i >= s.size()) {
            err = "unterminated string";
            return false;
        }
        ++i;
        return true;
    }
};

bool P::value(Json& out) {
    ws();
    if (i >= s.size()) {
        err = "unexpected end";
        return false;
    }
    char c = s[i];

    if (c == '{') {
        ++i;
        out = Json::obj();
        ws();
        if (i < s.size() && s[i] == '}') {
            ++i;
            return true;
        }
        for (;;) {
            ws();
            std::string k;
            if (!string(k)) return false;
            ws();
            if (i >= s.size() || s[i] != ':') {
                err = "expected ':'";
                return false;
            }
            ++i;
            Json v;
            if (!value(v)) return false;
            out[k] = v;
            ws();
            if (i < s.size() && s[i] == ',') {
                ++i;
                continue;
            }
            if (i < s.size() && s[i] == '}') {
                ++i;
                return true;
            }
            err = "expected ',' or '}'";
            return false;
        }
    }
    if (c == '[') {
        ++i;
        out = Json::arr();
        ws();
        if (i < s.size() && s[i] == ']') {
            ++i;
            return true;
        }
        for (;;) {
            Json v;
            if (!value(v)) return false;
            out.push(v);
            ws();
            if (i < s.size() && s[i] == ',') {
                ++i;
                continue;
            }
            if (i < s.size() && s[i] == ']') {
                ++i;
                return true;
            }
            err = "expected ',' or ']'";
            return false;
        }
    }
    if (c == '"') {
        std::string v;
        if (!string(v)) return false;
        out = Json(v);
        return true;
    }
    if (lit("true")) {
        out = Json(true);
        return true;
    }
    if (lit("false")) {
        out = Json(false);
        return true;
    }
    if (lit("null")) {
        out = Json();
        return true;
    }
    {
        char* end = nullptr;
        double d = std::strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) {
            err = "bad value";
            return false;
        }
        i = (size_t)(end - s.c_str());
        out = Json(d);
        return true;
    }
}

} // namespace

bool Json::parse(const std::string& text, Json& out, std::string& err) {
    P p{text, 0, {}};
    if (!p.value(out)) {
        err = p.err;
        return false;
    }
    return true;
}

} // namespace altair
