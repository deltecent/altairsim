#include "cli/monitor.h"

#include "cli/commands.h"
#include "cli/lineedit.h"

#include "boards/memory.h"
#include "boards/registry.h"
#include "config/toml.h"
#include "core/crc32.h"
#include "core/hex.h"
#include "core/roms.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

namespace altair {

// ---------------------------------------------------------------------------
// Lexing
// ---------------------------------------------------------------------------

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> t;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace((unsigned char)line[i])) ++i;
        if (i >= line.size()) break;
        if (line[i] == ';' || line[i] == '#') break;  // comment to end of line
        if (line[i] == '"') {
            std::string s;
            ++i;
            while (i < line.size() && line[i] != '"') s += line[i++];
            if (i < line.size()) ++i;
            t.push_back("\"" + s);  // keep the quote so SEARCH knows it was a string
            continue;
        }
        std::string w;
        while (i < line.size() && !std::isspace((unsigned char)line[i])) w += line[i++];
        t.push_back(w);
    }
    return t;
}

static std::string upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
static bool is(const std::string& tok, const char* kw) { return upper(tok) == kw; }

// ---------------------------------------------------------------------------
// NUMBERS. Settled 2026-07-11 by Patrick.
//
//   ON THE WIRE -> HEX.   NEVER ON THE WIRE -> DECIMAL.
//
// The base is a property of the OPERAND, not of the command line. An address, a
// data byte and a port number are things the machine itself sees, they are hex
// in every listing and on every front panel, and they are hex here. A step
// count, a history depth, a dump width and a baud rate are things only the
// operator ever sees -- the 8080 never holds one -- and they are decimal.
//
// This is not two rules fighting. It is one rule, and the alternative was
// forced to break anyway: `SET sio0 baud=9600` cannot mean 38400, so a single
// global base was never actually on the table. Given that, the line is drawn
// where it means something instead of where it fell.
//
// `0x`, `$` and a trailing `h` force hex; `#` forces decimal. Everywhere, both
// directions, so nothing here is a trap you cannot type your way out of.
//
// A `K` or `M` SUFFIX IS ALWAYS BEHIND A DECIMAL NUMBER (Patrick, 2026-07-11).
// `10K` is 10,240 bytes, never 16K -- which is exactly why nobody has ever had to
// think about it. So the suffix carries its own base, and `0x10K` is a
// contradiction: it is REJECTED, not quietly resolved one way or the other.
// ---------------------------------------------------------------------------

// One parser (core/value.cpp). The monitor supplies only the DEFAULT BASE, which
// is the one thing it knows and the parser cannot: what kind of quantity this is.
static bool parseNum(const std::string& in, uint32_t& out, int base, std::string& err) {
    long long v = 0;
    if (!parseNumber(in, v, err, base)) return false;
    if (v < 0) {
        err = "negative: '" + in + "'";
        return false;
    }
    out = (uint32_t)v;
    return true;
}

// Something the machine sees: an address, a port, a byte. HEX.
bool Monitor::addr(const std::string& t, uint32_t& out, std::ostream& err) {
    std::string e;
    if (!parseNum(t, out, 16, e)) {
        err << e << "  (this one is HEX -- the machine sees it. #123 forces decimal.)\n";
        failed_ = true;
        return false;
    }
    return true;
}

// Something only the operator sees: a count, a width, a depth. DECIMAL.
bool Monitor::count(const std::string& t, uint32_t& out, std::ostream& err) {
    std::string e;
    if (!parseNum(t, out, 10, e)) {
        err << e << "  (this one is DECIMAL -- it never reaches the machine. 0x20 forces hex.)\n";
        failed_ = true;
        return false;
    }
    return true;
}

// LO-HI, or LO/LEN (LEN bytes), or a bare address meaning one byte.
bool Monitor::range(const std::string& t, uint32_t& lo, uint32_t& hi, std::ostream& err) {
    size_t d = t.find('-');
    size_t sl = t.find('/');
    if (d != std::string::npos && d > 0) {
        if (!addr(t.substr(0, d), lo, err) || !addr(t.substr(d + 1), hi, err)) return false;
    } else if (sl != std::string::npos) {
        uint32_t len;
        if (!addr(t.substr(0, sl), lo, err) || !addr(t.substr(sl + 1), len, err)) return false;
        if (len == 0) len = 1;
        hi = lo + len - 1;
    } else {
        if (!addr(t, lo, err)) return false;
        hi = lo;
    }
    if (hi < lo) {
        err << "range ends before it starts: " << t << "\n";
        failed_ = true;
        return false;
    }
    return true;
}

Board* Monitor::board(const std::string& id, std::ostream& err) {
    Board* b = m_.find(id);
    if (!b) {
        err << "no board '" << id << "'. BOARD LIST shows what is in the machine.\n";
        failed_ = true;
    }
    return b;
}

// Resolve `id:unit` to a board and a NAMED unit, and check the unit is the kind
// the command can actually act on.
//
// The kind check is the whole reason units are named. `MOUNT dj:tty disk.dsk` is a
// mistake with a cause, and this can say what the cause was; under the old integer
// scheme `MOUNT dj:4` could only fail, because nothing distinguished 4-the-drive
// from 4-the-serial-port.
bool Monitor::subunit(const std::string& spec, Board*& b, UnitDef& u, bool wantMountable,
                      std::ostream& err) {
    size_t c = spec.find(':');
    if (c == std::string::npos) {
        err << "expected <id>:<unit>, got '" << spec << "'. SHOW <id> lists the units.\n";
        failed_ = true;
        return false;
    }
    b = board(spec.substr(0, c), err);
    if (!b) return false;

    std::string name = spec.substr(c + 1);
    if (!b->findUnit(name, u)) {
        auto all = b->units();
        err << b->id << " has no unit '" << name << "'.";
        if (all.empty()) {
            err << " This card has no units at all.\n";
        } else {
            err << " It has:";
            for (const auto& x : all) err << " " << x.name;
            err << "\n";
        }
        failed_ = true;
        return false;
    }

    if (wantMountable && !isMountable(u.kind)) {
        err << b->id << ":" << u.name << " is a " << unitKindName(u.kind)
            << " unit -- there is nothing to mount into it. Use CONNECT.\n";
        failed_ = true;
        return false;
    }
    if (!wantMountable && u.kind != UnitKind::Serial) {
        err << b->id << ":" << u.name << " is a " << unitKindName(u.kind)
            << " unit, not a serial port. Use MOUNT.\n";
        failed_ = true;
        return false;
    }
    return true;
}

void Monitor::flush(std::ostream& out) {
    for (const auto& s : m_.bus.drain()) out << s << "\n";
    m_.bus.clearLog();
    for (const auto& s : m_.drainBoardLog()) out << s << "\n";
}

// ---------------------------------------------------------------------------
// SHOW
// ---------------------------------------------------------------------------

void Monitor::showBoard(Board* b, std::ostream& out) {
    char buf[256];
    out << b->id << "  (" << b->type() << ")" << (b->enabled() ? "" : "  [DISABLED]") << "\n";

    if (auto* mem = dynamic_cast<MemoryBoard*>(b)) {
        const auto& rs = mem->regions();
        if (rs.empty()) {
            out << "  regions: (none -- this card is unpopulated and decodes nothing)\n";
        } else {
            out << "  regions:\n";
            for (size_t i = 0; i < rs.size(); ++i) {
                std::snprintf(buf, sizeof buf, "    %zu  %s", i, rs[i].describe().c_str());
                out << buf << "\n";
            }
        }
    }

    // The units, by the board's own names -- this is the list you type at MOUNT and
    // CONNECT, so it has to come from the same place they read (Board::units()).
    auto us = b->units();
    if (!us.empty()) {
        out << "\n  unit     kind    holds\n";
        for (const auto& u : us) {
            std::snprintf(buf, sizeof buf, "    %-7s %-7s %s", u.name.c_str(),
                          unitKindName(u.kind), u.state.c_str());
            out << buf << "\n";
        }
    }

    out << "\n  property         value            runtime?  legal\n";
    for (const auto& p : b->properties()) {
        std::string legal;
        if (p.kind == Kind::Enum) {
            for (const auto& c : p.choices) legal += (legal.empty() ? "" : "|") + c;
        } else if (p.kind == Kind::Int && !(p.min == 0 && p.max == 0)) {
            std::snprintf(buf, sizeof buf, "%lld..%lld", p.min, p.max);
            legal = buf;
        } else if (p.kind == Kind::Bool) {
            legal = "true|false";
        }
        std::snprintf(buf, sizeof buf, "  %-16s %-16s %-9s %s", p.name.c_str(),
                      p.get().text(p.radix).c_str(), p.runtime ? "yes" : "config", legal.c_str());
        out << buf << "\n";
    }
}

void Monitor::showBus(const std::vector<std::string>& a, std::ostream& out) {
    char buf[200];
    std::string what = a.size() > 2 ? upper(a[2]) : "";

    if (what == "MAP" || what.empty()) {
        out << "MEMORY\n";
        struct Row {
            uint32_t lo, hi;
            std::string id, what, note;
        };
        std::vector<Row> rows;
        for (const auto& b : m_.boards())
            for (const auto& e : b->memMap())
                rows.push_back({e.lo, e.hi, b->id, e.what, e.note});
        std::sort(rows.begin(), rows.end(), [](const Row& x, const Row& y) {
            return x.lo < y.lo || (x.lo == y.lo && x.id < y.id);
        });
        if (rows.empty()) out << "  (nothing -- every address floats to FF)\n";
        for (const auto& r : rows) {
            std::snprintf(buf, sizeof buf, "  %04X-%04X  %-8s %-4s %s", r.lo, r.hi, r.id.c_str(),
                          r.what.c_str(), r.note.c_str());
            out << buf << "\n";
        }
        // A hole is not an error. It is an empty socket, and it reads FF.
        std::vector<char> covered(256, 0);
        for (const auto& r : rows)
            for (uint32_t p = r.lo >> 8; p <= (r.hi >> 8) && p < 256; ++p) covered[p] = 1;
        std::string holes;
        int run = -1;
        for (int i = 0; i <= 256; ++i) {
            bool c = (i < 256) && covered[i];
            if (!c && run < 0 && i < 256) run = i;
            if ((c || i == 256) && run >= 0) {
                std::snprintf(buf, sizeof buf, "%s%04X-%04X", holes.empty() ? "" : ",", run << 8,
                              (i << 8) - 1);
                holes += buf;
                run = -1;
            }
        }
        if (!holes.empty()) out << "  unmapped: " << holes << "  (floats to FF)\n";
        if (what == "MAP") return;
    }

    if (what == "IO" || what.empty()) {
        out << "I/O\n";
        bool any = false;
        for (const auto& b : m_.boards())
            for (const auto& e : b->ioMap()) {
                std::snprintf(buf, sizeof buf, "  %02X        %-8s %-5s %s", e.lo, b->id.c_str(),
                              e.what.c_str(), e.note.c_str());
                out << buf << "\n";
                any = true;
            }
        if (!any) out << "  (no board decodes any port)\n";
        if (what == "IO") return;
    }

    if (what == "CONTENTION") {
        // Walk every address and ask who ACTUALLY drives. A phantom overlay is
        // not contention -- the shadowed board returns false from decodes(), so
        // it never appears here. That distinction falls out of the model; it is
        // not a special case (DESIGN.md 4.6).
        int found = 0;
        for (uint32_t A = 0; A <= 0xFFFF; ++A) {
            for (Cycle t : {Cycle::MemRead, Cycle::MemWrite}) {
                BusCycle c;
                c.type = t;
                c.addr = (uint16_t)A;
                auto who = m_.bus.respondersTo(c);
                if (who.size() > 1) {
                    std::string ids;
                    for (auto* b : who) ids += " " + b->id;
                    std::snprintf(buf, sizeof buf, "  %04X %-5s driven by%s", A,
                                  t == Cycle::MemRead ? "read" : "write", ids.c_str());
                    out << buf << "\n";
                    ++found;
                }
            }
            if (found > 32) {
                out << "  ... (more)\n";
                break;
            }
        }
        for (uint32_t P = 0; P <= 0xFF; ++P) {
            BusCycle c;
            c.type = Cycle::IoWrite;
            c.addr = (uint16_t)P;
            auto who = m_.bus.respondersTo(c);
            if (who.size() > 1) {
                std::string ids;
                for (auto* b : who) ids += " " + b->id;
                std::snprintf(buf, sizeof buf, "  port %02X OUT driven by%s", P, ids.c_str());
                out << buf << "\n";
                ++found;
            }
        }
        if (!found) out << "  none.\n";
        return;
    }

    if (!what.empty() && what != "MAP" && what != "IO") {
        out << "SHOW BUS [MAP|IO|CONTENTION]\n";
        failed_ = true;
    }
}

void Monitor::showRoms(std::ostream& out) {
    char buf[200];
    out << "name      file         size  CRC32     decodes\n";
    for (const auto& r : builtinRoms()) {
        Image img;
        std::string err;
        std::string span = "(failed to decode)";
        std::string crc = "--------";
        if (decodeRom(r, 0, img, err) && !img.empty()) {
            auto flat = img.flat();
            std::snprintf(buf, sizeof buf, "%04X-%04X", img.lo(), img.hi());
            span = buf;
            std::snprintf(buf, sizeof buf, "%08X", crc32(flat));
            crc = buf;
        }
        std::snprintf(buf, sizeof buf, "%-9s %-12s %5zu  %s  %s", r.name, r.file, img.size(),
                      crc.c_str(), span.c_str());
        out << buf << "\n";
    }
    if (builtinRoms().empty()) out << "(none compiled in)\n";
    out << "\nUse as: mount = \"builtin:<name>\".  Provenance: docs/roms.md\n";
}

// ---------------------------------------------------------------------------
// exec
// ---------------------------------------------------------------------------

bool Monitor::exec(const std::string& line, std::ostream& out) {
    auto a = tokenize(line);
    if (a.empty()) return true;

    // Every command word goes through prefix resolution (cli/commands.cpp), so
    // `D`, `DU`, `DUM` and `DUMP` are the same command and nothing below this line
    // knows that abbreviation exists. From here on `cmd` is a full command name.
    std::string cmd;
    const CommandDef* c = nullptr;  // the resolved command; carries its own usage line
    if (a[0] == "?") {
        cmd = "HELP";
    } else {
        c = resolveCommand(a[0]);
        if (!c) {
            out << upper(a[0]) << ": unknown command. HELP lists them.\n";
            failed_ = true;
            return true;
        }
        if (!c->built) {
            // It RESOLVES but is not here yet, and says so. That is the whole
            // reason it is in the table: `S` means STEP from today, and will not
            // silently stop meaning SHOW the day the CPU lands.
            out << c->name << ": not implemented yet -- waiting on " << c->waiting << ".\n";
            failed_ = true;
            return true;
        }
        cmd = c->name;
    }
    char buf[256];

    auto need = [&](size_t n, const char* usage) {
        if (a.size() < n) {
            out << "usage: " << usage << "\n";
            failed_ = true;
            return false;
        }
        return true;
    };

    // --- RAW <id>: pull the qualifier out of anywhere in the line (DESIGN.md 10.2)
    // "Behind the bus, straight into one board's store." This is the PROM burner.
    Board* raw = nullptr;
    for (size_t i = 1; i + 1 < a.size(); ++i) {
        if (is(a[i], "RAW")) {
            raw = board(a[i + 1], out);
            if (!raw) return true;
            a.erase(a.begin() + i, a.begin() + i + 2);
            break;
        }
    }
    auto rd = [&](uint32_t x) -> uint8_t {
        return raw ? raw->rawRead(x) : m_.bus.memRead((uint16_t)x);
    };

    if (cmd == "QUIT") {
        quit_ = true;
        return false;
    }

    // ---------------- HELP ----------------
    //
    // BOTH FORMS ARE GENERATED FROM THE COMMAND TABLE. A hand-written help text is
    // a second list of commands, and a second list of commands is a list that is
    // wrong. The abbreviation is DERIVED (commands.cpp), never stored, so it is
    // right by construction and stays right when the table is reordered.
    //
    // Bare HELP lists the NAMES AND NOTHING ELSE. When you type HELP you are almost
    // always hunting for a name you half-remember, and a wall of usage lines is the
    // worst possible shape for that -- it does not fit on a screen, so the thing you
    // were looking for scrolls off the top. The whole set fits in a few lines now.
    // `HELP <cmd>` is where the usage and the examples live.
    if (cmd == "HELP") {
        if (a.size() >= 2) {
            const CommandDef* h = (a[1] == "?") ? resolveCommand("HELP") : resolveCommand(a[1]);
            if (!h) {
                out << upper(a[1]) << ": no such command. HELP lists them.\n";
                failed_ = true;
                return true;
            }
            out << "\n  " << abbreviation(*h) << "\n";
            out << "  " << h->usage << "\n";
            if (!h->built)
                out << "\n  NOT IMPLEMENTED YET -- waiting on " << h->waiting << ".\n"
                    << "  It resolves today so that its abbreviation cannot change under\n"
                    << "  your fingers once it lands.\n";
            if (h->detail) {
                out << "\n";
                // Indent every line of the detail block by two, including the examples.
                std::string d = h->detail;
                out << "  ";
                for (char ch : d) {
                    out << ch;
                    if (ch == '\n') out << "  ";
                }
                out << "\n";
            }
            out << "\n";
            return true;
        }

        // The list. Names only, in table order -- which IS the priority order, so it
        // doubles as the answer to "why does D mean DUMP".
        out << "\n";
        int col = 0;
        for (const CommandDef& c : commands()) {
            std::string shown = abbreviation(c);
            if (!c.built) shown += "*";
            std::snprintf(buf, sizeof buf, "  %-16s", shown.c_str());
            out << buf;
            if (++col == 4) {
                out << "\n";
                col = 0;
            }
        }
        if (col) out << "\n";
        out << "\n  Type the part before the [brackets]. * = not built yet; it will say so.\n"
               "  HELP <command> for the usage and examples -- e.g. HELP DUMP.\n\n"
               "  Numbers: on the wire is HEX (addresses, ports, bytes), never on the\n"
               "  wire is DECIMAL (counts, widths, sizes). 0x/$/h force hex, # forces\n"
               "  decimal, and a K/M suffix is always decimal.\n";
        return true;
    }

    // ---------------- BOARD ----------------
    if (cmd == "BOARD") {
        if (!need(2, "BOARD LIST|TYPES|ADD|REMOVE")) return true;
        std::string sub = upper(a[1]);

        if (sub == "TYPES") {
            for (const auto& t : boardTypes()) {
                std::snprintf(buf, sizeof buf, "  %-10s %s", t.name.c_str(),
                              t.description.c_str());
                out << buf << "\n";
                auto b = makeBoard(t.name);
                for (const auto& p : b->properties()) {
                    std::snprintf(buf, sizeof buf, "               %-16s %s", p.name.c_str(),
                                  p.help.c_str());
                    out << buf << "\n";
                }
            }
            return true;
        }
        if (sub == "LIST") {
            if (m_.boards().empty()) {
                out << "(empty backplane)\n";
                return true;
            }
            for (const auto& b : m_.boards()) {
                std::string mem, io;
                for (const auto& e : b->memMap()) {
                    std::snprintf(buf, sizeof buf, "%s%04X-%04X", mem.empty() ? "" : ",", e.lo,
                                  e.hi);
                    mem += buf;
                }
                for (const auto& e : b->ioMap()) {
                    std::snprintf(buf, sizeof buf, "%s%02X", io.empty() ? "" : ",", e.lo);
                    io += buf;
                }
                std::snprintf(buf, sizeof buf, "  %-8s %-8s mem:%-24s io:%-8s %s", b->id.c_str(),
                              b->type().c_str(), mem.empty() ? "-" : mem.c_str(),
                              io.empty() ? "-" : io.c_str(), b->enabled() ? "" : "[DISABLED]");
                out << buf << "\n";
            }
            return true;
        }
        if (sub == "ADD") {
            if (!need(4, "BOARD ADD <type> <id> [key=value ...]")) return true;
            std::string err;
            Board* b = m_.add(a[2], a[3], err);
            if (!b) {
                out << err << "\n";
                failed_ = true;
                return true;
            }
            for (size_t i = 4; i < a.size(); ++i) {
                size_t eq = a[i].find('=');
                if (eq == std::string::npos) continue;
                std::string k = a[i].substr(0, eq), v = a[i].substr(eq + 1);
                std::string e2;
                if (!setProperty(*b, k, v, m_.running, e2)) {
                    out << e2 << "\n";
                    failed_ = true;
                }
            }
            out << b->id << ": " << b->type() << " added\n";
            return true;
        }
        if (sub == "REMOVE") {
            if (!need(3, "BOARD REMOVE <id>")) return true;
            std::string err;
            if (!m_.remove(a[2], err)) {
                out << err << "\n";
                failed_ = true;
            } else {
                out << a[2] << ": removed\n";
            }
            return true;
        }
        out << "BOARD LIST|TYPES|ADD|REMOVE\n";
        failed_ = true;
        return true;
    }

    // ---------------- REGION ----------------
    // Populating a card interactively. Note this goes through the SAME generic
    // addSubUnit() hook the TOML loader uses, so the monitor learns nothing
    // about what a region is -- and a board that grows a different sub-unit
    // table next year needs no change here.
    if (cmd == "REGION") {
        if (!need(3, "REGION ADD <id> type=ram|rom at=<addr> [size=<n>|mount=<file>]")) return true;
        if (!is(a[1], "ADD")) {
            out << "REGION ADD <id> type=... at=... [size=...|mount=...]\n";
            failed_ = true;
            return true;
        }
        Board* b = board(a[2], out);
        if (!b) return true;
        KeyValues kv;
        for (size_t i = 3; i < a.size(); ++i) {
            size_t eq = a[i].find('=');
            if (eq == std::string::npos) {
                out << "expected key=value, got '" << a[i] << "'\n";
                failed_ = true;
                return true;
            }
            // No rewriting on the way through. `at` is hex and `size` is decimal
            // because of what they ARE, and the board's parser knows that -- the
            // CLI used to prepend "0x" to `at` here, which meant two places had an
            // opinion about the base and only one of them was ever right.
            kv.push_back({a[i].substr(0, eq), a[i].substr(eq + 1)});
        }
        std::string err;
        if (!b->addSubUnit("region", kv, err)) {
            out << b->id << ": " << err << "\n";
            failed_ = true;
            return true;
        }
        if (auto* mem = dynamic_cast<MemoryBoard*>(b)) {
            size_t u = mem->regions().size() - 1;
            out << b->id << ":" << u << ": " << mem->regions()[u].describe() << "\n";
        }
        return true;
    }

    // ---------------- SHOW / SET ----------------
    if (cmd == "SHOW") {
        if (!need(2, "SHOW <id> | SHOW BUS [MAP|IO|CONTENTION] | SHOW ROMS | SHOW MACHINE"))
            return true;
        std::string sub = upper(a[1]);
        if (sub == "BUS") {
            showBus(a, out);
            return true;
        }
        if (sub == "ROMS") {
            showRoms(out);
            return true;
        }
        if (sub == "MACHINE") {
            out << "name      " << m_.name << "\n";
            // The clock belongs to the CPU CARD (DESIGN.md 3) -- that is where the
            // crystal is. There is no CPU card yet, so this value is inert, and
            // saying so is cheaper than letting a config file look like it did
            // something. It moves to `SET cpu0 clock_hz=` when the 8080 lands.
            std::snprintf(buf, sizeof buf, "clock_hz  %lld%s", m_.clockHz,
                          m_.clockHz == 0 ? "  (flat out)" : "  (inert: no CPU card)");
            out << buf << "\n";
            std::snprintf(buf, sizeof buf, "sense     0x%02X  (port FF, front-panel switches)",
                          m_.sense);
            out << buf << "\n";
            out << "startup   " << (m_.startup.empty() ? "(none)" : "") << "\n";
            for (const auto& s : m_.startup) out << "            " << s << "\n";
            out << "cpu       (none -- milestone 1a: the monitor is the bus master)\n";
            return true;
        }
        Board* b = board(a[1], out);
        if (b) showBoard(b, out);
        return true;
    }

    if (cmd == "SET") {
        if (!need(3, "SET <id> <key>=<value>")) return true;
        if (is(a[1], "BUS")) {
            size_t eq = a[2].find('=');
            std::string v = eq == std::string::npos ? "" : upper(a[2].substr(eq + 1));
            if (upper(a[2]).rfind("CONTENTION", 0) == 0) {
                m_.bus.setContentionPolicy(v == "SILENT"  ? Contention::Silent
                                           : v == "ERROR" ? Contention::Error
                                                          : Contention::Warn);
                out << "bus: contention=" << v << "\n";
                return true;
            }
            out << "SET BUS CONTENTION=WARN|ERROR|SILENT\n";
            failed_ = true;
            return true;
        }
        Board* b = board(a[1], out);
        if (!b) return true;
        // key=value, or `key value`
        std::string k, v;
        size_t eq = a[2].find('=');
        if (eq != std::string::npos) {
            k = a[2].substr(0, eq);
            v = a[2].substr(eq + 1);
        } else if (a.size() >= 4) {
            k = a[2];
            v = a[3];
        } else {
            out << "usage: SET <id> <key>=<value>\n";
            failed_ = true;
            return true;
        }
        std::string err;
        if (!setProperty(*b, k, v, m_.running, err)) {
            out << err << "\n";
            failed_ = true;
        } else {
            out << b->id << ": " << k << "=" << v << "\n";
        }
        return true;
    }

    // ---------------- WHO ----------------
    if (cmd == "WHO") {
        if (!need(2, "WHO <addr> | WHO IO <port>")) return true;
        BusCycle c;
        if (is(a[1], "IO")) {
            if (!need(3, "WHO IO <port>")) return true;
            uint32_t p;
            if (!addr(a[2], p, out)) return true;
            c.type = Cycle::IoWrite;
            c.addr = (uint16_t)(p & 0xFF);
            auto who = m_.bus.respondersTo(c);
            std::snprintf(buf, sizeof buf, "port %02X OUT:", (unsigned)(p & 0xFF));
            out << buf;
            if (who.empty()) out << " nobody (an OUT here goes nowhere)";
            for (auto* b : who) out << " " << b->id;
            out << "\n";
            return true;
        }
        uint32_t A;
        if (!addr(a[1], A, out)) return true;

        for (Cycle t : {Cycle::MemRead, Cycle::MemWrite}) {
            c = BusCycle{};
            c.type = t;
            c.addr = (uint16_t)A;
            bool ph = false;
            for (const auto& b : m_.boards())
                if (b->enabled() && b->assertsPhantom(c)) ph = true;
            auto who = m_.bus.respondersTo(c);

            std::snprintf(buf, sizeof buf, "%04X %-5s ", A, t == Cycle::MemRead ? "read" : "write");
            out << buf;
            if (who.empty()) {
                out << "nobody -- floats to FF"
                    << (t == Cycle::MemWrite ? " (a write here is simply gone)" : "");
            } else {
                for (auto* b : who) out << b->id << " ";
                if (who.size() > 1) out << " *** CONTENTION: both drive ***";
            }
            if (ph) out << "  [PHANTOM* asserted]";
            out << "\n";
        }
        return true;
    }

    // ---------------- MOUNT ----------------
    if (cmd == "MOUNT") {
        if (!need(3, "MOUNT <id>:<unit> <file>")) return true;
        Board* b;
        UnitDef u;
        if (!subunit(a[1], b, u, true, out)) return true;
        std::string err;
        if (!b->mount(u.name, a[2], false, err)) {
            out << b->id << ": " << err << "\n";
            failed_ = true;
        } else {
            out << b->id << ":" << u.name << ": mounted " << a[2] << "\n";
        }
        return true;
    }
    if (cmd == "UNMOUNT") {
        if (!need(2, "UNMOUNT <id>:<unit>")) return true;
        Board* b;
        UnitDef u;
        if (!subunit(a[1], b, u, true, out)) return true;
        std::string err;
        if (!b->unmount(u.name, err)) {
            out << b->id << ": " << err << "\n";
            failed_ = true;
        } else {
            out << b->id << ":" << u.name
                << ": unmounted (the socket is now EMPTY -- those pages float to FF)\n";
        }
        return true;
    }

    // ---------------- MEMORY ----------------
    if (cmd == "DUMP") {
        // Bare DUMP walks forward: the next 256 bytes from wherever the last one
        // stopped. Type `D` again and you get the next page, which is how you read
        // memory in practice -- you almost never know the address of the thing you
        // are looking for, only that it is somewhere after the thing you just saw.
        // A range resets the mark, so `D 100` then `D` continues from 0101.
        // A BARE START ADDRESS DUMPS TO THE END OF ITS PAGE. `D 100` is not a request
        // to see one byte -- nobody has ever wanted that -- it is "show me what is at
        // 0100", and the answer is a page. `D 0001` runs 0001-00FF: it STOPS ON A PAGE
        // BOUNDARY rather than counting out 256 bytes from wherever you happened to
        // start, so the last line is a full one and the next bare DUMP opens cleanly
        // on 0100. Dumps stay page-aligned forever, and the rows line up as well as
        // the columns.
        //
        // A real range (`100-10F`, `100/20`) means exactly what it says. Only the
        // single-address form expands -- `range()` is shared with FILL, MOVE, SEARCH
        // and SAVE, where a bare address quietly meaning a page would be a footgun.
        uint32_t lo, hi;
        if (a.size() < 2) {
            lo = dumpNext_;  // already on a page boundary, so this is a full page
            hi = lo | 0xFF;
        } else if (a[1].find('-') == std::string::npos && a[1].find('/') == std::string::npos) {
            if (!addr(a[1], lo, out)) return true;
            if (lo > 0xFFFF) {
                out << "address is 16 bits: 0000-FFFF\n";
                failed_ = true;
                return true;
            }
            hi = lo | 0xFF;
        } else if (!range(a[1], lo, hi, out)) {
            return true;
        }
        if (hi > 0xFFFF) hi = 0xFFFF;
        dumpNext_ = (hi + 1) & 0xFFFF;  // wraps to 0000 off the top, like the CPU

        // WIDTH is bytes-per-line. It is a COUNT -- it never reaches the machine --
        // so it is DECIMAL: `WIDTH=8` is eight, and `WIDTH=10` is ten, not sixteen.
        uint32_t w = 16;
        for (size_t i = 2; i < a.size(); ++i) {
            std::string k = upper(a[i]);
            if (k.compare(0, 6, "WIDTH=") != 0) {
                out << "DUMP: don't know '" << a[i] << "'. " << c->usage << "\n";
                failed_ = true;
                return true;
            }
            if (!count(a[i].substr(6), w, out)) return true;
            if (w < 1 || w > 64) {
                out << "WIDTH is 1..64 (decimal)\n";
                failed_ = true;
                return true;
            }
        }

        // LINES ARE ALIGNED TO THE WIDTH, NOT TO THE START ADDRESS. `D 0001` opens on
        // the 0000 line with the 0000 column left BLANK, so 0001 sits under the "01"
        // heading where it belongs. A dump you have to count across to read is a dump
        // that will be misread -- and the whole reason to print a hex address on every
        // line is so the column position tells you the low nibble without counting.
        uint32_t base = lo - (lo % w);
        for (uint32_t A = base; A <= hi; A += w) {
            std::string hexs, asc;
            for (uint32_t k = 0; k < w; ++k) {
                uint32_t E = A + k;
                if (E < lo || E > hi) {
                    hexs += "   ";  // outside the range: hold the column, show nothing
                    asc += ' ';
                } else {
                    uint8_t v = rd(E);
                    std::snprintf(buf, sizeof buf, "%02X ", v);
                    hexs += buf;
                    asc += (v >= 0x20 && v < 0x7F) ? (char)v : '.';
                }
                // The traditional mid-line gutter. It is OUTSIDE the branch above:
                // a blank column still has to push it, or a padded line's gutter
                // lands in the wrong place and the alignment we just bought is lost.
                if (w == 16 && k == 7) hexs += ' ';
            }
            std::snprintf(buf, sizeof buf, "%04X  %s %s", A, hexs.c_str(), asc.c_str());
            out << buf << "\n";
        }
        flush(out);
        return true;
    }

    // ---------------- EXAMINE ----------------
    //
    // The other front-panel switch. DUMP answers "what is around here"; EXAMINE
    // answers "what is AT here", which is a different question and deserves its own
    // verb -- paging 256 bytes to read one is how you lose the byte in the noise.
    //
    // Bare EXAMINE is the panel's EXAMINE NEXT: it steps one byte. `EX 100` then
    // `EX`, `EX`, `EX` walks memory a byte at a time, exactly as the switch does.
    if (cmd == "EXAMINE") {
        uint32_t A;
        if (a.size() < 2) {
            A = examNext_;
        } else if (!addr(a[1], A, out)) {
            return true;
        }
        if (A > 0xFFFF) {
            out << "address is 16 bits: 0000-FFFF\n";
            failed_ = true;
            return true;
        }
        examNext_ = (A + 1) & 0xFFFF;  // wraps off the top, like the panel's latch

        uint8_t v = rd(A);
        std::snprintf(buf, sizeof buf, "%04X  %02X  %c  %c%c%c%c%c%c%c%c", A, v,
                      (v >= 0x20 && v < 0x7F) ? (char)v : '.', (v & 0x80) ? '1' : '0',
                      (v & 0x40) ? '1' : '0', (v & 0x20) ? '1' : '0', (v & 0x10) ? '1' : '0',
                      (v & 0x08) ? '1' : '0', (v & 0x04) ? '1' : '0', (v & 0x02) ? '1' : '0',
                      (v & 0x01) ? '1' : '0');
        out << buf;
        // Looking at ONE byte is exactly when you need to know whether it is a byte
        // at all. FF from a chip and FF from an empty slot read the same.
        if (!raw && m_.bus.lastUnclaimed()) out << "   (nobody drives this -- the bus floated it)";
        out << "\n";
        flush(out);
        return true;
    }

    if (cmd == "DEPOSIT") {
        if (!need(3, "DEPOSIT <addr> <byte...>")) return true;
        uint32_t A;
        if (!addr(a[1], A, out)) return true;
        for (size_t i = 2; i < a.size(); ++i) {
            uint32_t v;
            if (!addr(a[i], v, out)) return true;
            if (raw) {
                if (!raw->rawWrite(A, (uint8_t)v)) {
                    out << raw->id << ": offset past this board's store\n";
                    failed_ = true;
                    return true;
                }
            } else {
                m_.bus.memWrite((uint16_t)A, (uint8_t)v);
                // Silence here would be a bug that costs you an hour. If nobody
                // latched the byte, SAY SO.
                if (m_.bus.lastUnclaimed()) {
                    BusCycle c;
                    c.type = Cycle::MemRead;
                    c.addr = (uint16_t)A;
                    auto rdr = m_.bus.respondersTo(c);
                    std::snprintf(buf, sizeof buf, "%04X: no board decodes writes here", A);
                    out << buf;
                    if (!rdr.empty())
                        out << " (" << rdr.front()->id << " answers reads -- it is ROM)";
                    out << ". byte discarded.\n";
                }
            }
            ++A;
        }
        flush(out);
        return true;
    }

    // ---------------- IN / OUT ----------------
    //
    // These run a REAL bus cycle -- the same ioRead/ioWrite the CPU will run once
    // it exists. So they have REAL SIDE EFFECTS: an IN from a UART's data port
    // consumes the byte, and the guest will never see it. That is not a wart to be
    // papered over, it is the whole point. This is a monitor, and poking a live
    // port is the oldest way there is to find out whether a card is alive. If you
    // want to look without touching, that is what SHOW and WHO are for.
    if (cmd == "IN") {
        if (!need(2, "IN <port>")) return true;
        uint32_t p;
        if (!addr(a[1], p, out)) return true;  // a port is on the wire: hex
        if (p > 0xFF) {
            out << "port is one byte: 00-FF\n";
            failed_ = true;
            return true;
        }
        uint8_t v = m_.bus.ioRead((uint8_t)p);
        std::snprintf(buf, sizeof buf, "port %02X -> %02X", (unsigned)p, v);
        out << buf;
        // The distinction the bus exists to make. FF from a board and FF from an
        // empty slot are the same byte and completely different faults.
        if (m_.bus.lastUnclaimed()) out << "   (nobody answered -- the bus floated it)";
        out << "\n";
        flush(out);
        return true;
    }

    if (cmd == "OUT") {
        if (!need(3, "OUT <port> <byte>")) return true;
        uint32_t p, v;
        if (!addr(a[1], p, out) || !addr(a[2], v, out)) return true;
        if (p > 0xFF) {
            out << "port is one byte: 00-FF\n";
            failed_ = true;
            return true;
        }
        m_.bus.ioWrite((uint8_t)p, (uint8_t)v);
        std::snprintf(buf, sizeof buf, "port %02X <- %02X", (unsigned)p, (unsigned)(v & 0xFF));
        out << buf;
        if (m_.bus.lastUnclaimed()) out << "   (nobody decodes this port -- the byte is gone)";
        out << "\n";
        flush(out);
        return true;
    }

    if (cmd == "FILL") {
        if (!need(3, "FILL <range> <byte>")) return true;
        uint32_t lo, hi, v;
        if (!range(a[1], lo, hi, out) || !addr(a[2], v, out)) return true;
        for (uint32_t A = lo; A <= hi; ++A) {
            if (raw) raw->rawWrite(A, (uint8_t)v);
            else m_.bus.memWrite((uint16_t)A, (uint8_t)v);
        }
        std::snprintf(buf, sizeof buf, "filled %04X-%04X with %02X", lo, hi, v);
        out << buf << "\n";
        flush(out);
        return true;
    }

    if (cmd == "SEARCH") {
        if (!need(3, "SEARCH <range> <bytes...>|\"string\"")) return true;
        uint32_t lo, hi;
        if (!range(a[1], lo, hi, out)) return true;
        std::vector<uint8_t> pat;
        for (size_t i = 2; i < a.size(); ++i) {
            if (a[i][0] == '"') {
                for (size_t k = 1; k < a[i].size(); ++k) pat.push_back((uint8_t)a[i][k]);
            } else {
                uint32_t v;
                if (!addr(a[i], v, out)) return true;
                pat.push_back((uint8_t)v);
            }
        }
        int hits = 0;
        for (uint32_t A = lo; A + pat.size() - 1 <= hi; ++A) {
            bool ok = true;
            for (size_t k = 0; k < pat.size(); ++k)
                if (rd(A + k) != pat[k]) {
                    ok = false;
                    break;
                }
            if (ok) {
                std::snprintf(buf, sizeof buf, "%04X", A);
                out << buf << "\n";
                ++hits;
            }
        }
        out << hits << " match(es)\n";
        return true;
    }

    if (cmd == "COMPARE") {
        if (!need(3, "COMPARE <range> <addr>")) return true;
        uint32_t lo, hi, dst;
        if (!range(a[1], lo, hi, out) || !addr(a[2], dst, out)) return true;
        int diff = 0;
        for (uint32_t A = lo; A <= hi; ++A) {
            uint8_t x = rd(A), y = rd(dst + (A - lo));
            if (x != y) {
                std::snprintf(buf, sizeof buf, "%04X %02X != %04X %02X", A, x, dst + (A - lo), y);
                out << buf << "\n";
                if (++diff > 32) {
                    out << "... (more)\n";
                    break;
                }
            }
        }
        out << diff << " difference(s)\n";
        return true;
    }

    if (cmd == "MOVE") {
        if (!need(3, "MOVE <range> <dest>")) return true;
        uint32_t lo, hi, dst;
        if (!range(a[1], lo, hi, out) || !addr(a[2], dst, out)) return true;
        std::vector<uint8_t> tmp;
        for (uint32_t A = lo; A <= hi; ++A) tmp.push_back(rd(A));
        for (size_t k = 0; k < tmp.size(); ++k) {
            if (raw) raw->rawWrite(dst + (uint32_t)k, tmp[k]);
            else m_.bus.memWrite((uint16_t)(dst + k), tmp[k]);
        }
        out << tmp.size() << " bytes moved\n";
        flush(out);
        return true;
    }

    if (cmd == "LOAD") {
        if (!need(2, "LOAD <file> [AT <addr>] [RAW <id>]")) return true;
        uint32_t at = 0;
        bool haveAt = false;
        for (size_t i = 2; i + 1 < a.size(); ++i)
            if (is(a[i], "AT")) {
                if (!addr(a[i + 1], at, out)) return true;
                haveAt = true;
            }

        std::ifstream f(a[1], std::ios::binary);
        if (!f) {
            out << "cannot open '" << a[1] << "'\n";
            failed_ = true;
            return true;
        }
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
        Image img;
        std::string err;
        if (looksLikeHex(data)) {
            if (!loadHex(data, img, err)) {
                out << a[1] << ": " << err << "\n";  // names the record. loudly.
                failed_ = true;
                return true;
            }
            if (haveAt) {
                Image b;
                for (const auto& [A, v] : img.bytes) b.bytes[A + at] = v;
                img = b;
            }
        } else {
            if (!haveAt) {
                out << a[1] << " is a flat binary and carries no addresses -- it needs AT <addr>\n";
                failed_ = true;
                return true;
            }
            loadBin(data, at, img);
        }

        if (raw) {
            // THE PROM BURNER. Straight into the board's store, behind the bus.
            // This is why the operator can write ROM and the guest cannot.
            auto* mem = dynamic_cast<MemoryBoard*>(raw);
            if (!mem) {
                out << raw->id << ": no store to burn\n";
                failed_ = true;
                return true;
            }
            if (!mem->blit(img, err)) {
                out << raw->id << ": " << err << "\n";
                failed_ = true;
                return true;
            }
            std::snprintf(buf, sizeof buf, "%s: loaded %zu bytes from %s (%04X-%04X)",
                          raw->id.c_str(), img.size(), a[1].c_str(), img.lo(), img.hi());
            out << buf << "\n";
        } else {
            size_t gone = 0;
            for (const auto& [A, v] : img.bytes) {
                m_.bus.memWrite((uint16_t)A, v);
                if (m_.bus.lastUnclaimed()) ++gone;
            }
            std::snprintf(buf, sizeof buf, "loaded %zu bytes from %s (%04X-%04X)", img.size(),
                          a[1].c_str(), img.lo(), img.hi());
            out << buf << "\n";
            if (gone) {
                // Loading through the bus is a bus write, so ROM does not take it
                // -- exactly as a real machine would not. Say so; do not quietly
                // half-load.
                std::snprintf(buf, sizeof buf,
                              "  WARNING: %zu byte(s) landed nowhere (ROM, or unmapped). "
                              "To burn a ROM use: LOAD %s RAW <id>",
                              gone, a[1].c_str());
                out << buf << "\n";
            }
            if (img.hasStart) {
                std::snprintf(buf, sizeof buf, "  start address %04X (no CPU yet)", img.start);
                out << buf << "\n";
            }
        }
        flush(out);
        return true;
    }

    if (cmd == "SAVE") {
        if (!need(3, "SAVE <file> <range> [RAW <id>]")) return true;
        uint32_t lo, hi;
        if (!range(a[2], lo, hi, out)) return true;
        Image img;
        for (uint32_t A = lo; A <= hi; ++A) img.bytes[A] = rd(A);

        bool asHex = a[1].size() > 4 && upper(a[1]).rfind(".HEX") == a[1].size() - 4;
        std::ofstream f(a[1], std::ios::binary);
        if (!f) {
            out << "cannot write '" << a[1] << "'\n";
            failed_ = true;
            return true;
        }
        if (asHex) {
            f << saveHex(img);
        } else {
            for (auto v : img.flat()) f.put((char)v);
        }
        std::snprintf(buf, sizeof buf, "saved %04X-%04X to %s (%s)", lo, hi, a[1].c_str(),
                      asHex ? "hex" : "bin");
        out << buf << "\n";
        return true;
    }

    // ---------------- EXECUTION ----------------
    if (cmd == "RESET") {
        m_.reset(Reset::Bus);
        out << "RESET* pulsed. (Memory is UNTOUCHED -- only POWER loses RAM.)\n";
        return true;
    }
    if (cmd == "POWER") {
        m_.power();
        out << "power cycled: RAM re-filled, ROM images re-read, POC* (pin 76) pulsed.\n";
        flush(out);
        return true;
    }

    // ---------------- CONFIG ----------------
    if (cmd == "CONFIG") {
        if (!need(3, "CONFIG LOAD|SAVE <file.toml>")) return true;
        std::string err;
        if (is(a[1], "LOAD")) {
            if (!loadToml(a[2], m_, err)) {
                out << err << "\n";
                failed_ = true;
                return true;
            }
            out << "loaded " << a[2] << ": " << m_.boards().size() << " board(s)\n";
            flush(out);
            runStartup(out);
            return true;
        }
        if (is(a[1], "SAVE")) {
            if (!saveToml(a[2], m_, err)) {
                out << err << "\n";
                failed_ = true;
                return true;
            }
            out << "saved " << a[2] << "\n";
            return true;
        }
        out << "CONFIG LOAD|SAVE <file.toml>\n";
        failed_ = true;
        return true;
    }

    out << "unknown command '" << a[0] << "'. HELP lists them.\n";
    failed_ = true;
    return true;
}

void Monitor::runStartup(std::ostream& out) {
    // A startup entry is an ORDINARY MONITOR COMMAND. That is the whole idea:
    // the config language and the script language are one language, so anything
    // you can type, a config can do -- and no BOOT verb has to exist.
    for (const auto& s : m_.startup) {
        out << "startup> " << s << "\n";
        if (!exec(s, out)) break;
    }
}

int Monitor::repl(std::istream& in, std::ostream& out, bool interactive) {
    std::string line;
    LineEditor ed;
    while (!quit_) {
        if (interactive) {
            // The editor decides for itself whether stdin is really a terminal --
            // `altairsim < script` is `interactive` here but is not a tty, and it
            // must not have raw mode done to it.
            if (!ed.read("altairsim> ", line, in)) break;
        } else {
            if (!std::getline(in, line)) break;
            if (!line.empty()) out << "altairsim> " << line << "\n";  // echo the script
        }
        if (!exec(line, out)) break;
    }
    return exitCode();
}

} // namespace altair
