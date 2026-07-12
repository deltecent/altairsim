#include "config/toml.h"

#include "boards/memory.h"
#include "boards/registry.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace altair {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

// Strip quotes and the underscores TOML allows in numbers.
std::string unquote(const std::string& s) {
    std::string t = trim(s);
    if (t.size() >= 2 && ((t.front() == '"' && t.back() == '"') ||
                          (t.front() == '\'' && t.back() == '\'')))
        return t.substr(1, t.size() - 2);
    std::string o;
    for (char c : t)
        if (c != '_') o += c;
    return o;
}

// One [table] or [[array-of-table]] and the key/values under it.
struct Table {
    std::string name;
    bool array = false;
    KeyValues kv;
    std::vector<std::string> list;  // for `startup = [...]`
    bool hasList = false;
};

bool parse(const std::string& text, std::vector<Table>& out, std::string& err) {
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    Table* cur = nullptr;
    out.push_back({"", false, {}, {}, false});  // the root table
    cur = &out.back();

    while (std::getline(in, line)) {
        ++lineNo;
        // Strip comments, honoring quotes so a path with a '#' survives.
        std::string s;
        bool q = false;
        for (char c : line) {
            if (c == '"') q = !q;
            if (c == '#' && !q) break;
            s += c;
        }
        s = trim(s);
        if (s.empty()) continue;

        if (s.front() == '[') {
            bool arr = s.compare(0, 2, "[[") == 0;
            size_t close = s.find(arr ? "]]" : "]");
            if (close == std::string::npos) {
                err = "line " + std::to_string(lineNo) + ": unterminated table header";
                return false;
            }
            std::string nm = trim(s.substr(arr ? 2 : 1, close - (arr ? 2 : 1)));
            out.push_back({nm, arr, {}, {}, false});
            cur = &out.back();
            continue;
        }

        size_t eq = s.find('=');
        if (eq == std::string::npos) {
            err = "line " + std::to_string(lineNo) + ": expected key = value";
            return false;
        }
        std::string k = trim(s.substr(0, eq));
        std::string v = trim(s.substr(eq + 1));

        if (!v.empty() && v.front() == '[') {
            // An inline array. We only use it for `startup`, and multi-line
            // arrays are common there, so keep reading until the bracket closes.
            std::string acc = v;
            while (acc.find(']') == std::string::npos && std::getline(in, line)) {
                ++lineNo;
                std::string t2;
                bool q2 = false;
                for (char c : line) {
                    if (c == '"') q2 = !q2;
                    if (c == '#' && !q2) break;
                    t2 += c;
                }
                acc += " " + trim(t2);
            }
            size_t lb = acc.find('['), rb = acc.rfind(']');
            std::string body = acc.substr(lb + 1, rb - lb - 1);
            std::string item;
            bool inq = false;
            for (char c : body) {
                if (c == '"') {
                    inq = !inq;
                    if (!inq) {
                        cur->list.push_back(item);
                        item.clear();
                    }
                    continue;
                }
                if (inq) item += c;
            }
            cur->hasList = true;
            cur->kv.push_back({k, "[]"});
            continue;
        }

        cur->kv.push_back({k, unquote(v)});
    }
    return true;
}

} // namespace

// A BUILT-IN MACHINE IS A TOML FILE THAT HAPPENS TO LIVE IN .rodata.
//
// This is the same bargain as the built-in ROMs, and for the same reason: there
// is ONE machine language, and a built-in cannot drift from a file because it
// travels the identical parser. `source` is only ever used to name the thing in
// an error message -- "builtin:turnkey: ..." reads exactly like a path would.
bool loadTomlText(const std::string& text, const std::string& source, Machine& m,
                  std::string& err) {
    const std::string& path = source;

    std::vector<Table> tabs;
    if (!parse(text, tabs, err)) {
        err = path + ": " + err;
        return false;
    }

    Board* current = nullptr;

    for (auto& t : tabs) {
        if (t.name == "machine" || t.name.empty()) {
            for (auto& [k, v] : t.kv) {
                if (k == "name") m.name = v;
                else if (k == "clock_hz") {
                    // THE CLOCK IS THE CPU CARD'S, because that is where the crystal
                    // physically is (DESIGN.md 3, 8). While there was no CPU this key
                    // sat here doing nothing; now that there is one, keeping it would
                    // mean two places to say one thing -- and the day they disagreed,
                    // the machine would run at whichever the last writer won.
                    //
                    // So it is an ERROR, not an ignored key. A setting that is quietly
                    // dropped is worse than one that is refused: the config LOOKS like
                    // it slowed the machine down, and it did not.
                    err = path + ": clock_hz belongs to the CPU CARD, not to [machine] --\n"
                          "  the crystal is on the card. Put it in the CPU's [[board]]:\n"
                          "      [[board]]\n"
                          "      type     = \"8080\"\n"
                          "      id       = \"cpu0\"\n"
                          "      clock_hz = " + v;
                    return false;
                } else if (k == "sense") {
                    // The front-panel sense switches, which the guest reads through
                    // port FF. It is a byte on the wire, so it is hex.
                    long long n;
                    if (!parseNumber(v, n, err, 16)) return false;
                    m.sense = (uint8_t)n;
                } else if (k == "startup") {
                    m.startup = t.list;
                } else if (!t.name.empty()) {
                    err = path + ": unknown [machine] key '" + k + "'";
                    return false;
                }
            }
            continue;
        }

        if (t.name == "board") {
            std::string type, id;
            for (auto& [k, v] : t.kv) {
                if (k == "type") type = v;
                if (k == "id") id = v;
            }
            if (type.empty() || id.empty()) {
                err = path + ": every [[board]] needs `type` and `id`";
                return false;
            }
            current = m.add(type, id, err);
            if (!current) {
                err = path + ": " + err;
                return false;
            }
            // Everything else is a PROPERTY, resolved against the board's own
            // properties(). The loader knows nothing about phantom straps or
            // baud rates and never will.
            for (auto& [k, v] : t.kv) {
                if (k == "type" || k == "id") continue;
                if (!setProperty(*current, k, v, m.running, err)) {
                    err = path + ": [[board]] " + id + ": " + err;
                    return false;
                }
            }
            continue;
        }

        // A sub-unit table: [[board.region]], [[board.drive]], [board.unit.a].
        if (t.name.rfind("board.", 0) == 0) {
            if (!current) {
                err = path + ": [[" + t.name + "]] before any [[board]]";
                return false;
            }
            std::string sub = t.name.substr(6);
            size_t dot = sub.find('.');
            std::string table = dot == std::string::npos ? sub : sub.substr(0, dot);

            auto accepted = current->subUnitTables();
            bool ok = false;
            for (const auto& x : accepted)
                if (x == table) ok = true;
            if (!ok) {
                err = path + ": board '" + current->id + "' (" + current->type() +
                      ") has no [[board." + table + "]] table";
                return false;
            }
            KeyValues kv = t.kv;
            if (dot != std::string::npos) kv.push_back({"unit", sub.substr(dot + 1)});
            if (!current->addSubUnit(table, kv, err)) {
                err = path + ": [[" + t.name + "]] on " + current->id + ": " + err;
                return false;
            }
            continue;
        }

        if (t.name == "console") continue;  // no console board yet (milestone 1b)

        err = path + ": unknown table [" + t.name + "]";
        return false;
    }

    m.power();  // a machine that has just been built has just been switched on
    return true;
}

bool loadToml(const std::string& path, Machine& m, std::string& err) {
    std::ifstream f(path);
    if (!f) {
        err = "cannot open '" + path + "'";
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return loadTomlText(ss.str(), path, m, err);
}

bool saveToml(const std::string& path, Machine& m, std::string& err) {
    std::ofstream f(path);
    if (!f) {
        err = "cannot write '" + path + "'";
        return false;
    }
    char buf[256];

    f << "[machine]\n";
    f << "name     = \"" << m.name << "\"\n";
    // No clock_hz here. It is written out as the CPU board's property, by the same
    // generic properties() walk that writes every other board's -- which is why
    // CONFIG SAVE round-trips and cannot drift from what SET accepts.
    std::snprintf(buf, sizeof buf, "sense    = 0x%02X\n", m.sense);
    f << buf;
    if (!m.startup.empty()) {
        f << "startup  = [\n";
        for (const auto& s : m.startup) f << "  \"" << s << "\",\n";
        f << "]\n";
    }

    for (const auto& b : m.boards()) {
        f << "\n[[board]]\n";
        f << "type = \"" << b->type() << "\"\n";
        f << "id   = \"" << b->id << "\"\n";
        // Straight out of properties() -- the same list SHOW prints and SET
        // writes. Round-trip is therefore structural, not something we maintain.
        for (const auto& p : b->properties()) {
            if (!p.set) continue;
            std::string e2;
            Value probe = p.get();
            // Derived / read-only properties reject their own value; those are
            // not config and must not be written.
            if (!p.set(probe, e2)) continue;
            if (p.kind == Kind::Str || p.kind == Kind::Enum)
                f << p.name << " = \"" << probe.text(p.radix) << "\"\n";
            else
                f << p.name << " = " << probe.text(p.radix) << "\n";
        }
        // Sub-units.
        if (auto* mem = dynamic_cast<MemoryBoard*>(b.get())) {
            for (const auto& r : mem->regions()) {
                f << "\n  [[board.region]]\n";
                f << "  type = \"" << (r.kind == RegionKind::Rom ? "rom" : "ram") << "\"\n";
                std::snprintf(buf, sizeof buf, "  at   = 0x%04X\n", r.at);
                f << buf;
                if (r.kind == RegionKind::Rom)
                    f << "  mount = \"" << r.mount << "\"\n";
                else {
                    if (r.size % 1024 == 0)
                        f << "  size = \"" << (r.size / 1024) << "K\"\n";
                    else {
                        std::snprintf(buf, sizeof buf, "  size = 0x%X\n", r.size);
                        f << buf;
                    }
                }
            }
        }
    }
    return true;
}

} // namespace altair
