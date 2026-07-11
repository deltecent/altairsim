#include "core/value.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace altair {

Value Value::ofInt(long long v) {
    Value x;
    x.kind_ = Kind::Int;
    x.i_ = v;
    return x;
}
Value Value::ofBool(bool v) {
    Value x;
    x.kind_ = Kind::Bool;
    x.i_ = v ? 1 : 0;
    return x;
}
Value Value::ofStr(std::string v) {
    Value x;
    x.kind_ = Kind::Str;
    x.s_ = std::move(v);
    return x;
}

std::string Value::text(int radix) const {
    switch (kind_) {
    case Kind::Bool:
        return i_ ? "true" : "false";
    case Kind::Str:
    case Kind::Enum:
        return s_;
    case Kind::Int: {
        char buf[32];
        if (radix == 16) {
            std::snprintf(buf, sizeof buf, "0x%llX", (unsigned long long)i_);
        } else {
            std::snprintf(buf, sizeof buf, "%lld", i_);
        }
        return buf;
    }
    }
    return {};
}

static bool ieq(const std::string& a, const char* b) {
    size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t k = 0; k < n; ++k)
        if (std::tolower((unsigned char)a[k]) != std::tolower((unsigned char)b[k])) return false;
    return true;
}

bool parseNumber(const std::string& in, long long& out, std::string& err, int base) {
    std::string t;
    for (char c : in)
        if (c != '_') t += c;
    if (t.empty()) {
        err = "empty number";
        return false;
    }

    size_t i = 0;
    bool neg = false;
    if (t[i] == '-') {
        neg = true;
        ++i;
    }

    // A K/M SUFFIX IS ALWAYS BEHIND A DECIMAL NUMBER (Patrick, 2026-07-11). `10K`
    // is 10,240 -- which is why in fifty years nobody has had to ask. The suffix
    // therefore carries its own base and overrides the caller's default.
    unsigned long long mult = 1;
    if (t.back() == 'K' || t.back() == 'k') mult = 1024ULL;
    else if (t.back() == 'M' || t.back() == 'm') mult = 1024ULL * 1024;
    if (mult > 1) {
        t.pop_back();
        base = 10;
    }

    bool forcedHex = false;
    if (t.compare(i, 2, "0x") == 0 || t.compare(i, 2, "0X") == 0) {
        base = 16;
        forcedHex = true;
        i += 2;
    } else if (t.compare(i, 2, "0b") == 0 || t.compare(i, 2, "0B") == 0) {
        base = 2;
        i += 2;
    } else if (t[i] == '$') {
        base = 16;
        forcedHex = true;
        i += 1;
    } else if (t[i] == '#') {
        base = 10;
        i += 1;
    } else if (t.size() > i + 1 && (t.back() == 'h' || t.back() == 'H')) {
        base = 16;
        forcedHex = true;
        t.pop_back();
    }

    // `0x10K` demands hex and appends a suffix that is decimal by definition.
    // There is no right answer, so there is no guess.
    if (mult > 1 && forcedHex) {
        err = "a K/M suffix is always decimal -- drop the hex marker: '" + in + "'";
        return false;
    }

    if (i >= t.size()) {
        err = "number has no digits: '" + in + "'";
        return false;
    }

    const char* p = t.c_str() + i;
    char* end = nullptr;
    errno = 0;
    unsigned long long v = std::strtoull(p, &end, base);
    if (end == p || (end && *end != '\0')) {
        err = "not a number: '" + in + "'";
        return false;
    }
    if (errno == ERANGE) {
        err = "number out of range: '" + in + "'";
        return false;
    }
    out = neg ? -(long long)(v * mult) : (long long)(v * mult);
    return true;
}

bool parseValue(const std::string& text, Kind kind, Value& out, std::string& err, int radix) {
    switch (kind) {
    case Kind::Bool: {
        if (ieq(text, "true") || ieq(text, "yes") || ieq(text, "on") || text == "1") {
            out = Value::ofBool(true);
            return true;
        }
        if (ieq(text, "false") || ieq(text, "no") || ieq(text, "off") || text == "0") {
            out = Value::ofBool(false);
            return true;
        }
        err = "expected true/false, got '" + text + "'";
        return false;
    }
    case Kind::Int: {
        long long v = 0;
        if (!parseNumber(text, v, err, radix)) return false;
        out = Value::ofInt(v);
        return true;
    }
    case Kind::Str:
    case Kind::Enum:
        out = Value::ofStr(text);
        return true;
    }
    err = "bad kind";
    return false;
}

bool validate(const Property& p, const Value& v, std::string& err) {
    if (p.kind == Kind::Int && !(p.min == 0 && p.max == 0)) {
        if (v.i() < p.min || v.i() > p.max) {
            char buf[128];
            if (p.radix == 16)
                std::snprintf(buf, sizeof buf, "%s must be 0x%llX..0x%llX", p.name.c_str(),
                              (unsigned long long)p.min, (unsigned long long)p.max);
            else
                std::snprintf(buf, sizeof buf, "%s must be %lld..%lld", p.name.c_str(), p.min, p.max);
            err = buf;
            return false;
        }
    }
    if (p.kind == Kind::Enum) {
        for (const auto& c : p.choices)
            if (ieq(v.s(), c.c_str())) return true;
        err = p.name + " must be one of:";
        for (const auto& c : p.choices) err += " " + c;
        err += " (got '" + v.s() + "')";
        return false;
    }
    return true;
}

} // namespace altair
