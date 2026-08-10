#include "boards/frontpanel-link.h"

#include <charconv>
#include <cstdio>

namespace altair::fplink {

namespace {

// One space-delimited token, and the rest of the line after it. Leading spaces (and
// '\r') are skipped before the token; the tail is returned trimmed of its leading
// separator so it can be tokenised again. Empty token => end of line.
struct Token {
    std::string_view tok;   // the field
    std::string_view rest;  // everything after it, ready for the next split
};

bool isSep(char c) { return c == ' ' || c == '\t' || c == '\r'; }

Token nextToken(std::string_view s) {
    size_t i = 0;
    while (i < s.size() && isSep(s[i])) ++i;
    size_t start = i;
    while (i < s.size() && !isSep(s[i])) ++i;
    return {s.substr(start, i - start), s.substr(i)};
}

// A fixed-width lowercase hex field, EXACTLY `width` digits. Returns false on any
// non-hex character or a wrong length -- the spec's fields are fixed width, so a
// short or long field is malformed, not merely lenient. Value out through `out`.
bool parseHex(std::string_view t, size_t width, uint32_t& out) {
    if (t.size() != width) return false;
    uint32_t v = 0;
    auto [p, ec] = std::from_chars(t.data(), t.data() + t.size(), v, 16);
    if (ec != std::errc{} || p != t.data() + t.size()) return false;
    out = v;
    return true;
}

// A plain decimal non-negative integer (the HELLO version). Any leftover character
// fails it.
bool parseDec(std::string_view t, int& out) {
    if (t.empty()) return false;
    int v = 0;
    auto [p, ec] = std::from_chars(t.data(), t.data() + t.size(), v, 10);
    if (ec != std::errc{} || p != t.data() + t.size()) return false;
    out = v;
    return true;
}

} // namespace

std::string encodeL(uint16_t addr, uint8_t data, uint8_t status, uint8_t flags) {
    char buf[32];
    // Lowercase hex, zero-padded, fixed width -- the spec's exact grammar.
    int n = std::snprintf(buf, sizeof(buf), "L %04x %02x %02x %02x\n", addr, data,
                          status, flags);
    return std::string(buf, (n > 0) ? (size_t)n : 0);
}

std::string encodeHello(int ver) {
    char buf[48];
    int n = std::snprintf(buf, sizeof(buf), "HELLO altairsim-fp %d\n", ver);
    return std::string(buf, (n > 0) ? (size_t)n : 0);
}

PanelMsg parseLine(std::string_view line) {
    Token t0 = nextToken(line);
    if (t0.tok.empty()) return {};  // blank line

    // HELLO altairsim-fp <ver>
    if (t0.tok == "HELLO") {
        Token t1 = nextToken(t0.rest);
        if (t1.tok != "altairsim-fp") return {};  // some other HELLO -- ignore
        Token t2 = nextToken(t1.rest);
        int ver = 0;
        if (!parseDec(t2.tok, ver) || ver < 0) return {};
        PanelMsg m;
        m.kind  = PanelMsg::Kind::Hello;
        m.value = (uint16_t)ver;
        return m;
    }

    // W <sw:04x> -- the full 16-bit switch word.
    if (t0.tok == "W") {
        Token t1 = nextToken(t0.rest);
        uint32_t v = 0;
        if (!parseHex(t1.tok, 4, v)) return {};
        PanelMsg m;
        m.kind  = PanelMsg::Kind::Switches;
        m.value = (uint16_t)v;
        return m;
    }

    // S <sense:02x> -- the sense byte alone (the switch word's high half).
    if (t0.tok == "S") {
        Token t1 = nextToken(t0.rest);
        uint32_t v = 0;
        if (!parseHex(t1.tok, 2, v)) return {};
        PanelMsg m;
        m.kind  = PanelMsg::Kind::Sense;
        m.value = (uint16_t)v;
        return m;
    }

    // Unknown frame type -- ignore it (forward compatibility).
    return {};
}

} // namespace altair::fplink
