#include "core/symbols.h"

#include <cctype>

namespace altair {

namespace {

std::string upcase(const std::string& s) {
    std::string u;
    u.reserve(s.size());
    for (char c : s) u += (char)std::toupper((unsigned char)c);
    return u;
}

int hexNib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

bool isHex4(const std::string& s, size_t at, uint32_t& out) {
    // Exactly four hex digits at [at, at+4). The .PRN address field is four wide.
    if (at + 4 > s.size()) return false;
    uint32_t v = 0;
    for (size_t i = at; i < at + 4; ++i) {
        int d = hexNib(s[i]);
        if (d < 0) return false;
        v = v * 16 + (uint32_t)d;
    }
    out = v;
    return true;
}

// A whole token as a hex number (1..8 digits). The value field of a .SYM record.
bool hexToken(const std::string& s, uint32_t& out) {
    if (s.empty() || s.size() > 8) return false;
    uint32_t v = 0;
    for (char c : s) {
        int d = hexNib(c);
        if (d < 0) return false;
        v = v * 16 + (uint32_t)d;
    }
    out = v;
    return true;
}

bool isBlank(char c) { return c == ' ' || c == '\t'; }

// The payload of a text file, minus the CP/M ^Z padding that fills the last record.
std::span<const uint8_t> untilEof(std::span<const uint8_t> d) {
    for (size_t i = 0; i < d.size(); ++i)
        if (d[i] == 0x1A) return d.subspan(0, i);
    return d;
}

}  // namespace

bool SymbolTable::lookup(const std::string& name, uint32_t& out) const {
    auto it = byName.find(upcase(name));
    if (it == byName.end()) return false;
    out = it->second.value;
    return true;
}

void SymbolTable::clear() {
    byName.clear();
    byAddr.clear();
    loadOrder.clear();
}

void SymbolTable::put(const std::string& rawName, uint32_t value, bool isAddr,
                      const std::string& source, LoadStats& st) {
    std::string name = upcase(rawName);
    if (name.empty()) return;

    auto it = byName.find(name);
    if (it != byName.end()) {
        st.redefined++;
        if (st.redefinedNames.size() < 8) st.redefinedNames.push_back(name);
        // Drop the old reverse entry so a redefined label does not linger at its old
        // address (equal_range because two names can share a value).
        if (it->second.isAddr) {
            auto range = byAddr.equal_range(it->second.value);
            for (auto r = range.first; r != range.second; ++r)
                if (r->second == name) { byAddr.erase(r); break; }
        }
    } else {
        st.added++;
    }

    byName[name] = Sym{value, isAddr, source};
    if (isAddr) byAddr.insert({value, name});
}

bool looksLikeSym(std::span<const uint8_t> data) {
    // The first byte that carries content decides. A .SYM opens with its value --
    // `0005 BDOS` -- so a hex digit in column 1. A .PRN opens with a space (column 1 is
    // blank, the address is at column 2) or a form-feed page break.
    for (uint8_t b : data) {
        if (b == '\r' || b == '\n') continue;
        return std::isxdigit(b) != 0;
    }
    return false;  // empty -- neither, and the caller reports zero symbols
}

// One physical line, form-feeds stripped from the front so a page break does not shift
// the columns. Returns false at end of input.
static bool nextLine(std::span<const uint8_t> d, size_t& i, std::string& line) {
    if (i >= d.size()) return false;
    while (i < d.size() && d[i] == 0x0C) ++i;   // ^L page break
    line.clear();
    while (i < d.size() && d[i] != '\n') {
        if (d[i] != '\r') line += (char)d[i];
        ++i;
    }
    if (i < d.size()) ++i;  // consume the '\n'
    return true;
}

bool loadPrn(std::span<const uint8_t> raw, const std::string& source, SymbolTable& t,
             bool replace, SymbolTable::LoadStats& st, std::string& err) {
    if (replace) t.clear();

    std::span<const uint8_t> d = untilEof(raw);
    size_t i = 0;
    std::string line;
    int lineNo = 0;

    while (nextLine(d, i, line)) {
        ++lineNo;

        // The address lives in columns 2-5 (index 1-4). No address there -- a blank line,
        // a bare comment, an ORG with no label -- means no symbol on this line.
        uint32_t value;
        if (!isHex4(line, 1, value)) continue;

        // RELOCATION GUARD. An M80 relocatable value carries a trailing apostrophe in the
        // machine-generated field (columns 1-16). Source apostrophes ('HELLO', a comment)
        // live at column 17+ and are not scanned, so this fires only on real relocation.
        size_t scanEnd = line.size() < 16 ? line.size() : 16;
        for (size_t c = 0; c < scanEnd; ++c)
            if (line[c] == '\'') {
                err = source + ": line " + std::to_string(lineNo) +
                      " has a relocatable address -- link it and load the .SYM, or "
                      "assemble to an absolute origin";
                return false;
            }

        // The source field is column 17 (index 16). A label is present only when that
        // column is non-blank; a code/continuation line has whitespace there.
        if (line.size() <= 16 || isBlank(line[16])) continue;

        // The label is the first whitespace-delimited token of the source field, minus a
        // trailing ':'. Handles `START:` (colon) and `monit` (tabs, no colon) alike.
        size_t s = 16, e = 16;
        while (e < line.size() && !isBlank(line[e])) ++e;
        std::string label = line.substr(s, e - s);
        if (!label.empty() && label.back() == ':') label.pop_back();
        if (label.empty()) continue;

        // '=' in column 7 (index 6) marks an EQU. An EQU's value may be a BDOS function
        // number as easily as an address, and nothing tells them apart, so it feeds
        // name->value ONLY -- never the reverse map (else 0015 renders as some FWRITE).
        bool isEqu = line.size() > 6 && line[6] == '=';
        t.put(label, value, /*isAddr=*/!isEqu, source, st);
    }

    if (st.added > 0 || replace) t.loadOrder.push_back(source);
    return true;
}

bool loadSym(std::span<const uint8_t> raw, const std::string& source, SymbolTable& t,
             bool replace, SymbolTable::LoadStats& st, std::string& err) {
    (void)err;  // a .SYM has no format error to report -- no relocation, no records to fail
    if (replace) t.clear();

    std::span<const uint8_t> d = untilEof(raw);

    // Records are `HHHH NAME`, several per line, separated by one-or-more TABs and by the
    // line breaks. Split on any whitespace run and read pairs: value, name, value, name...
    // A .SYM has no EQU/label distinction, so every symbol is name->value only (isAddr=false).
    std::vector<std::string> tok;
    std::string cur;
    for (uint8_t b : d) {
        if (b == ' ' || b == '\t' || b == '\r' || b == '\n') {
            if (!cur.empty()) { tok.push_back(cur); cur.clear(); }
        } else {
            cur += (char)b;
        }
    }
    if (!cur.empty()) tok.push_back(cur);

    for (size_t k = 0; k + 1 < tok.size(); ) {
        uint32_t value;
        // A value token is hex; if it is not, this is a header/garbage token -- skip one and
        // resync rather than pairing a name with the wrong number.
        if (!hexToken(tok[k], value)) { ++k; continue; }
        t.put(tok[k + 1], value, /*isAddr=*/false, source, st);
        k += 2;
    }

    if (st.added > 0 || replace) t.loadOrder.push_back(source);
    return true;
}

}  // namespace altair
