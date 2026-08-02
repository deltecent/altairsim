#include "cli/monitor.h"

#include "cli/commands.h"
#include "cli/lineedit.h"
#include "cli/tape_counter_line.h"

#include "boards/s100-memory.h"
#include "boards/registry.h"
#include "config/toml.h"
#include "core/crc32.h"
#include "core/debug.h"
#include "core/debuglog.h"
#include "core/hex.h"
#include "core/machines.h"
#include "core/paths.h"
#include "core/roms.h"
#include "core/symbols.h"
#include "core/version.h"
#include "host/console.h"
#include "host/display.h"
#include "host/endpoint.h"
#include "host/imd.h"    // convertImdToRaw -- MOUNT foo.imd converts to a raw sibling .dsk
#include "host/media.h"  // writeHostFile -- MOUNT ... CREATE makes an empty file
#include "isa/isa.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

// SHOW VERSION probes the compiled-in video drivers to say whether this build can open a
// window (see showVersion). SDL3::SDL3 is linked PUBLIC to altair_core when SDL is enabled.
#ifdef ALTAIRSIM_ENABLE_SDL
#include <SDL3/SDL.h>
#endif

namespace altair {

// ---------------------------------------------------------------------------
// Lexing
// ---------------------------------------------------------------------------

// THE IDLE JUDGEMENT (monitor.h). Pure, so it can be tested -- and it needed to be.
//
// It ran nothing (a slice that retired no instructions is a stopped machine, not an idle
// one), it SAID nothing, it RECEIVED nothing, and it kept coming back to an empty line.
//
// `received` is the one that matters and the one that was wrong: a guest taking XMODEM down
// a wire prints nothing for a whole 128-byte block and polls exactly as a prompt does, so
// the ONLY thing that tells the two apart is that one of them IS GETTING BYTES.
bool guestIsWaiting(const SliceWork& w, uint64_t ratio) {
    if (w.steps == 0) return false;   // stopped, not waiting
    if (w.wrote != 0) return false;   // it had something to say
    if (w.received != 0) return false;  // IT IS RECEIVING. Whatever this is, it is not a prompt.
    return w.hungry * ratio >= w.steps;
}

bool shouldPace(bool anyConsole, bool tty, bool anyRemoteLine, bool free) {
    if (free) return false;  // no crystal asked for -> flat out, always
    return (anyConsole && tty) || anyRemoteLine;
}

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

// A quoted token keeps its opening `"` so SEARCH can tell a string from a byte list.
// A filename does not want that sentinel -- and a filename is the one place a quote is
// not decoration but the only way to write a path with a space in it, which the period
// artifacts ("4K BASIC Ver 3-1.tap") all have.
static std::string unquote(const std::string& s) {
    return (!s.empty() && s[0] == '"') ? s.substr(1) : s;
}

static std::string upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
static bool is(const std::string& tok, const char* kw) { return upper(tok) == kw; }

// Point the one global debug sink (SET CONSOLE DEBUG=<sink>). `stderr` and `stdout`
// are reserved words; anything else is a file path opened for APPEND (a transcript,
// not a truncate). A bad path leaves the prior sink in place and returns false.
static bool applyDebugSink(const std::string& v, std::string& err) {
    if (is(v, "STDERR")) return dbg::setSink(dbg::Sink::Stderr, "", err);
    if (is(v, "STDOUT")) return dbg::setSink(dbg::Sink::Stdout, "", err);
    return dbg::setSink(dbg::Sink::File, v, err);
}

// ---------------------------------------------------------------------------
// NUMBERS. Settled 2026-07-11 by Patrick.
//
//   ON THE WIRE -> HEX or OCTAL.   NEVER ON THE WIRE -> DECIMAL.
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
// The ONE operator choice (added later): how the WIRE class is spelled -- hex, or
// OCTAL, which is what the MITS manuals and the front panel used (split octal, a
// byte per 000..377 group). `SET CONSOLE base=octal` moves the default parse base
// and the display of the wire class, and NOTHING ELSE: the decimal class does not
// move, because octal-vs-hex was never its question. This is not the global base
// ruled out above -- that was hex-vs-decimal across both classes; this is one
// class, spelled two ways.
//
// `0x`, `$` and a trailing `h` force hex; `0o` and a trailing `q` force octal;
// `#` forces decimal. Everywhere, both directions, whatever the default, so
// nothing here is a trap you cannot type your way out of.
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

// Is the operator reading and writing wire quantities in octal? One question,
// asked here so no call site below has to name the enum.
static bool octalMode() {
    return Console::instance().base() == Console::NumBase::Octal;
}

// ---- WIRE QUANTITIES ON THE WAY OUT. One place per width honors the base. ----
//
// A byte is two hex digits or three octal (000..377); a 16-bit value is four hex
// digits or SPLIT octal -- its two bytes, hi then lo, each 000..377, one space
// between ("022 064"), the way the front-panel address lamps group and every MITS
// listing prints an address. Every address/port/byte the monitor prints goes
// through one of these, so the base is honored in exactly one place per width. In
// hex they reproduce the old %02X/%04X exactly, so hex output does not move.
//
// File-local free functions, not Monitor members: the stop-reason reporter is a
// free function too, and this way it reaches them the same as everyone else.
static int byteWidth() { return octalMode() ? 3 : 2; }  // columns fmtByte() fills

static std::string fmtByte(uint8_t v) {
    char b[8];
    std::snprintf(b, sizeof b, octalMode() ? "%03o" : "%02X", (unsigned)v);
    return b;
}

static std::string fmtWord(uint16_t v) {
    char b[16];
    if (octalMode())
        std::snprintf(b, sizeof b, "%03o %03o", (unsigned)(v >> 8), (unsigned)(v & 0xFF));
    else
        std::snprintf(b, sizeof b, "%04X", (unsigned)v);
    return b;
}

// Greedy word-wrap on spaces, for the last (prose) column of a table. A word longer than
// `width` is emitted whole rather than chopped, so an over-long token overhangs instead of
// being split mid-word. Always returns at least one line (empty input -> one empty line),
// so a caller can print line[0] on the row and lines[1..] as aligned continuations.
static std::vector<std::string> wrapText(const std::string& s, size_t width) {
    std::vector<std::string> lines;
    std::string line;
    std::istringstream words(s);
    std::string w;
    while (words >> w) {
        if (line.empty())
            line = w;
        else if (line.size() + 1 + w.size() <= width)
            line += " " + w;
        else {
            lines.push_back(std::move(line));
            line = w;
        }
    }
    lines.push_back(std::move(line));  // the tail, or "" if the input was blank
    return lines;
}

// What a property will ACCEPT, in plain terminal form -- the same facts the generated
// reference prints via gen-reference.cpp's legal(), so SHOW BOARD and ref/boards.md agree
// on a property's legal values. Empty when the kind imposes no listable set (free Int/Str),
// so the caller prints nothing rather than a bare "values:". Rendered in the property's own
// radix, matching how its default prints.
static std::string legalValues(const Property& p) {
    if (!p.values.empty()) return p.values;   // a hand-written hint (a free-form Str's grammar)
    if (p.kind == Kind::Bool) return "on | off";
    if (p.kind == Kind::Enum) {
        std::string o;
        for (size_t i = 0; i < p.choices.size(); ++i) {
            if (i) o += " | ";
            o += p.choices[i];
        }
        return o;
    }
    if (p.kind == Kind::Int && p.min != p.max)  // min==max means unbounded (value.h)
        return Value::ofInt(p.min).text(p.radix) + " .. " + Value::ofInt(p.max).text(p.radix);
    return "";
}

// Something the machine sees: an address, a port, a byte. HEX, or OCTAL when the
// operator has set that -- either way the WIRE base, never decimal.
bool Monitor::addr(const std::string& t, uint32_t& out, std::ostream& err) {
    std::string e;
    if (!parseNum(t, out, octalMode() ? 8 : 16, e)) {
        err << e << (octalMode()
                         ? "  (this one is OCTAL -- the machine sees it. 0x forces hex, #123 decimal.)\n"
                         : "  (this one is HEX -- the machine sees it. #123 forces decimal.)\n");
        failed_ = true;
        return false;
    }
    return true;
}

// An address, OR a loaded symbol. The symbol table is consulted FIRST, so a name that
// also spells a hex number (BEEF, FACE) resolves to the symbol -- and the same escapes
// that force a hex literal past a register name force it past a symbol (0BEEF is the
// number, $FACE is the number). Only TRUE-ADDRESS sites call this; a port and a byte
// value stay on addr(), where a symbol has no business (core/symbols.h).
bool Monitor::addrSym(const std::string& t, uint32_t& out, std::ostream& err) {
    if (m_.syms.lookup(t, out)) return true;
    return addr(t, out, err);
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
        // LO and HI are addresses, so a symbol names either end (DUMP START-END). A symbol
        // never contains '-' (not in the M80 charset), so this split cannot cut one in half.
        if (!addrSym(t.substr(0, d), lo, err) || !addrSym(t.substr(d + 1), hi, err)) return false;
    } else if (sl != std::string::npos) {
        uint32_t len = 0;  // = 0: addr() writes it only on success, which MSVC's flow
                           // analysis can't see (C4701). The path to line below is only
                           // reached when both calls succeeded, so it is always set.
        // LO is an address (a symbol resolves it); LEN is a length and stays on addr().
        if (!addrSym(t.substr(0, sl), lo, err) || !addr(t.substr(sl + 1), len, err)) return false;
        if (len == 0) len = 1;
        hi = lo + len - 1;
    } else {
        if (!addrSym(t, lo, err)) return false;
        hi = lo;
    }
    if (hi < lo) {
        err << "range ends before it starts: " << t << "\n";
        failed_ = true;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// THE TRAILING INDEX IS OPTIONAL: `ACR` IS acr0.
//
// The `0` in `acr0` is not an index. Nothing parses it; it is a character in a
// string the machine file happened to choose (`id = "acr0"`). It is there to tell
// two cassettes apart -- and when there is only ONE cassette in the machine, it
// tells nothing apart, and typing it is a tax the operator pays for a distinction
// that does not exist.
//
// So: exact match first (case-insensitively -- that is Machine::find()'s job, and
// it is an identity, not a guess). Failing that, a board is a candidate when its id
// is what you typed plus a run of DIGITS. `acr` -> acr0. `dsk` -> dsk0. `ac` -> no
// one, because this is not prefix matching: only the index may be dropped.
//
// AND IT LIVES HERE, IN THE MONITOR, NOT IN Machine::find(). This is a convenience
// for a human standing at the prompt. A machine file must say what it means -- an
// `[[board]] id = "acr"` that silently reached into the base and modified acr0
// would be a config that does something other than what it says -- and neither may
// MCP guess. Same line the project already draws for relative paths: what you TYPE
// and what a FILE says are resolved by different rules, on purpose.
// ---------------------------------------------------------------------------
Board* Monitor::board(const std::string& id, std::ostream& err) {
    if (Board* b = m_.find(id)) return b;

    std::string want = lowerAscii(id);
    std::vector<Board*> hits;
    for (const auto& b : m_.boards()) {
        std::string have = lowerAscii(b->id);
        if (have.size() <= want.size() || have.compare(0, want.size(), want) != 0) continue;
        bool allDigits = true;
        for (size_t i = want.size(); i < have.size(); i++)
            allDigits = allDigits && have[i] >= '0' && have[i] <= '9';
        if (allDigits) hits.push_back(b.get());
    }

    if (hits.size() == 1) return hits[0];

    // TWO CASSETTES IS NOT AN ERROR IN THE MACHINE -- it is an error in the
    // sentence. Say which ones, because the operator's next keystroke is one of them.
    if (hits.size() > 1) {
        err << id << ": ambiguous --";
        for (Board* b : hits) err << " " << b->id;
        err << ". Name the one you mean.\n";
        failed_ = true;
        return nullptr;
    }

    err << "no board '" << id << "'. BOARDS shows what is in the machine.\n";
    failed_ = true;
    return nullptr;
}

// Resolve `id:unit` to a board and a NAMED unit, and check the unit is the kind
// the command can actually act on.
//
// The kind check is the whole reason units are named. `MOUNT dj:tty disk.dsk` is a
// mistake with a cause, and this can say what the cause was; under the old integer
// scheme `MOUNT dj:4` could only fail, because nothing distinguished 4-the-drive
// from 4-the-serial-port.
bool Monitor::subunit(const std::string& spec, Board*& b, UnitDef& u, UnitUse use,
                      std::ostream& err) {
    size_t c = spec.find(':');

    // ---- A LONE UNIT NEEDS NO NAMING: `MOUNT acr x.tap` is acr0:tape. ----
    //
    // The 88-ACR has exactly one thing you can put a tape in, and it is called
    // `tape`. Naming it adds no information -- and the ONLY reason units are named
    // is to carry information (core/board.h: "a unit is a NAME, not an index").
    //
    // Filtered BY THE KIND THE VERB CAN ACT ON, which is what `use` already is. So a
    // 2SIO is unambiguous to nobody (`a` and `b` are both serial) but an 88-SIO is
    // (one `tty`), and a memory card with one ROM socket is unambiguous to MOUNT
    // even though it is a card with a lot else going on.
    //
    // SET IS NOT AFFECTED, and that is deliberate: it decides board-property vs
    // unit-property by the colon BEFORE it gets here (`SET acr0 x=y` is the board's
    // property and must never quietly become the tape's), so it only ever reaches
    // this function with a colon already in hand.
    if (c == std::string::npos) {
        b = board(spec, err);
        if (!b) return false;

        std::vector<UnitDef> fit;
        for (const auto& x : b->units()) {
            bool ok = use == UnitUse::Any ||
                      (use == UnitUse::Mount && isMountable(x.kind)) ||
                      (use == UnitUse::Connect && x.kind == UnitKind::Serial);
            if (ok) fit.push_back(x);
        }

        if (fit.size() == 1) {
            u = fit[0];
            return true;
        }

        const char* verb = use == UnitUse::Connect ? "connect to" : "mount into";
        if (fit.empty()) {
            err << b->id << " (" << b->type() << ") has nothing you can " << verb << ".";
            auto all = b->units();
            if (!all.empty()) {
                err << " Its units are:";
                for (const auto& x : all) err << " " << x.name << " (" << unitKindName(x.kind) << ")";
            }
            err << "\n";
        } else {
            err << b->id << " has " << fit.size() << " units you could " << verb << ":";
            for (const auto& x : fit) err << " " << x.name;
            err << ". Name one -- " << b->id << ":" << fit[0].name << "\n";
        }
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
            err << " This board has no units at all.\n";
        } else {
            err << " It has:";
            for (const auto& x : all) err << " " << x.name;
            err << "\n";
        }
        failed_ = true;
        return false;
    }

    if (use == UnitUse::Mount && !isMountable(u.kind)) {
        err << b->id << ":" << u.name << " is a " << unitKindName(u.kind)
            << " unit -- there is nothing to mount into it. Use CONNECT.\n";
        failed_ = true;
        return false;
    }
    if (use == UnitUse::Connect && u.kind != UnitKind::Serial) {
        err << b->id << ":" << u.name << " is a " << unitKindName(u.kind)
            << " unit, not a serial port. Use MOUNT.\n";
        failed_ = true;
        return false;
    }
    // UnitUse::Any asks nothing. `SET acr0:tape mode=record` is a property of a
    // recorder, and what KIND the unit is has no bearing on whether it has settings.
    return true;
}

// ---------------------------------------------------------------------------
// A VERB A CARD BROUGHT WITH IT (DESIGN.md 5.4).
//
// REACHED ONLY WHEN THE BUILT-IN TABLE HAS ALREADY SAID NO. That ordering is the
// whole safety property: the static menu is resolved first, in its own priority
// order, by code that has never heard of boards -- so plugging in a card cannot
// shorten, shadow or destabilize a single built-in abbreviation. `RE` is RESET in
// every machine ever booted, and `REW` reaches the cassette only because nothing
// built-in begins with those three letters.
//
// The price of that ordering is that a card can declare a verb no one can reach.
// We do not pay it here -- BOARDS ADD refuses such a card, where the message can
// still be about the CARD instead of about a word the user typed.
// ---------------------------------------------------------------------------
// Deduped by NAME and returned BY VALUE -- see the header for the crash that taught
// me the difference.
std::vector<std::pair<std::string, CommandDef>> Monitor::boardVerbs() const {
    std::vector<std::pair<std::string, CommandDef>> v;
    for (const auto& b : m_.boards())
        for (const CommandDef& d : b->commands()) {
            bool seen = false;
            for (auto& x : v) seen = seen || std::string(x.second.name) == d.name;
            if (!seen) v.emplace_back(b->type(), d);
        }
    return v;
}

// ---------------------------------------------------------------------------
// Tab completion (DESIGN.md 10.4). A pure read over the same reflection SET uses --
// see the header. The word under the cursor is the run of non-space bytes ending there;
// the tokens before it decide what its candidates are. Nothing here reports or mutates.
// ---------------------------------------------------------------------------
Completions Monitor::complete(const std::string& line) {
    Completions comp;

    // The fragment under the cursor: back up to the last space (or the line start).
    size_t wordStart = line.size();
    while (wordStart > 0 && !std::isspace((unsigned char)line[wordStart - 1])) --wordStart;
    std::string frag = line.substr(wordStart);
    comp.replaceFrom = wordStart;

    std::vector<std::string> toks = tokenize(line);
    bool atNewWord = line.empty() || std::isspace((unsigned char)line.back());
    size_t wordIdx = atNewWord ? toks.size() : (toks.empty() ? 0 : toks.size() - 1);

    const std::string U = upper(frag);
    auto keep = [&](const std::string& cand) {
        if (upper(cand).compare(0, U.size(), U) == 0) comp.matches.push_back(cand);
    };

    // ---- word 0: a command name (built-ins, then the verbs cards bring) ----
    if (wordIdx == 0) {
        for (const CommandDef& c : commands()) keep(c.name);
        for (const auto& bv : boardVerbs())    keep(bv.second.name);
        std::sort(comp.matches.begin(), comp.matches.end());
        comp.matches.erase(std::unique(comp.matches.begin(), comp.matches.end()),
                           comp.matches.end());
        comp.suffix = " ";
        return comp;
    }

    // Past word 0 is command-specific. Resolve the verb the way the dispatcher does --
    // a built-in (prefixes and all), or one a board brought (boardVerbs). An unknown
    // word matches nothing, so Tab stays inert there.
    std::string verbFrag = toks.empty() ? "" : upper(toks[0]);
    const CommandDef* cmd = toks.empty() ? nullptr : resolveCommand(toks[0]);
    std::string name = cmd ? cmd->name : "";
    bool boardVerb = false;
    if (!cmd)
        for (const auto& v : boardVerbs())
            if (std::string(v.second.name).compare(0, verbFrag.size(), verbFrag) == 0) {
                boardVerb = true;
                break;
            }

    // board() reports errors and trips failed_, which completion must never do. This is
    // its prefix scan with the reporting removed -- a unique hit or nothing.
    auto resolveQuiet = [&](const std::string& id) -> Board* {
        if (Board* b = m_.find(id)) return b;
        std::string want = lowerAscii(id);
        Board* hit = nullptr;
        int n = 0;
        for (const auto& b : m_.boards()) {
            std::string have = lowerAscii(b->id);
            if (have.size() <= want.size() || have.compare(0, want.size(), want) != 0) continue;
            bool allDigits = true;
            for (size_t i = want.size(); i < have.size(); i++)
                allDigits = allDigits && have[i] >= '0' && have[i] <= '9';
            if (allDigits) { hit = b.get(); ++n; }
        }
        return n == 1 ? hit : nullptr;
    };

    // A board-id / id:unit fragment -- the shape every target-taking command reads
    // (SET, MOUNT, CONNECT, a board verb...). With no colon it offers board ids (and,
    // for SET, the pseudo-targets); past a colon it offers that board's unit NAMES,
    // filtered to the kind the verb can act on, exactly as subunit() filters them.
    auto completeTarget = [&](const std::string& f, UnitUse use, bool wantPseudo) {
        size_t c = f.find(':');
        if (c == std::string::npos) {
            for (const auto& b : m_.boards()) keep(b->id);
            if (wantPseudo)
                for (const char* kw : {"CONSOLE", "DISPLAY", "REG", "BUS"}) keep(kw);
            comp.suffix = " ";
            return;
        }
        Board* b = resolveQuiet(f.substr(0, c));
        if (!b) return;  // an unresolved id has no units to offer
        const std::string unitFrag = upper(f.substr(c + 1));
        comp.replaceFrom = wordStart + c + 1;
        for (const auto& u : b->units()) {
            bool ok = use == UnitUse::Any ||
                      (use == UnitUse::Mount && isMountable(u.kind)) ||
                      (use == UnitUse::Connect && u.kind == UnitKind::Serial);
            if (ok && upper(u.name).compare(0, unitFrag.size(), unitFrag) == 0)
                comp.matches.push_back(u.name);
        }
        comp.suffix = " ";
    };

    // A property KEY (suffix '=' so the value types straight on), or -- past '=' -- that
    // property's legal enum values. A board's own props and a unit's props both flow
    // through here, so the two SET targets stay one copy of the grammar.
    auto completeProps = [&](const std::vector<Property>& props) {
        size_t eq = frag.find('=');
        if (eq == std::string::npos) {  // the KEY
            for (const Property& p : props) keep(p.name);
            comp.suffix = "=";
            return;
        }
        // the VALUE -- only the run after `=` is replaced, and only enum props offer one.
        const std::string Ukey = upper(frag.substr(0, eq));
        const std::string Uval = upper(frag.substr(eq + 1));
        comp.replaceFrom = wordStart + eq + 1;
        for (const Property& p : props) {
            bool match = Ukey == upper(p.name);
            for (const std::string& a : p.aliases) match = match || Ukey == upper(a);
            if (!match) continue;
            for (const std::string& ch : p.choices)
                if (upper(ch).compare(0, Uval.size(), Uval) == 0) comp.matches.push_back(ch);
            break;
        }
        comp.suffix = " ";
    };

    // ---- SET: the target (word 1), then a property key/value (word 2) ----
    if (name == "SET") {
        if (wordIdx == 1) {
            completeTarget(frag, UnitUse::Any, /*wantPseudo=*/true);
            // Debug channels are SET targets too. A board channel already appears as
            // its board id; a library channel (`6850`, `socket`) is no board, so it
            // is only offered here. Dedup keeps the board-id/channel overlap to one.
            if (frag.find(':') == std::string::npos) {
                for (dbg::Channel* c : dbg::channels()) keep(c->name());
                std::sort(comp.matches.begin(), comp.matches.end());
                comp.matches.erase(std::unique(comp.matches.begin(), comp.matches.end()),
                                   comp.matches.end());
            }
            return comp;
        }
        if (wordIdx == 2) {
            const std::string& target = toks[1];
            const size_t eq = frag.find('=');
            const bool consoleTarget = is(target, "CONSOLE");
            // A channel target is a board's (by id) or a library's -- never a unit's,
            // so a colon rules it out.
            dbg::Channel* dc = (!consoleTarget && target.find(':') == std::string::npos)
                                   ? dbg::find(target)
                                   : nullptr;

            // VALUE side of a facility key. These are NOT Property rows, so they are
            // completed here and we return before the Property path runs.
            if (eq != std::string::npos) {
                const std::string Ukey = upper(frag.substr(0, eq));
                if (consoleTarget && Ukey == "DEBUG") {  // the global sink
                    comp.replaceFrom = wordStart + eq + 1;
                    const std::string Uval = upper(frag.substr(eq + 1));
                    for (const char* s : {"stderr", "stdout"})
                        if (upper(s).compare(0, Uval.size(), Uval) == 0) comp.matches.push_back(s);
                    comp.suffix = " ";
                    return comp;
                }
                if (dc && (Ukey == "DEBUG" || Ukey == "NODEBUG")) {  // the channel's flags
                    // The value is a comma-separated flag list; complete only the run
                    // after the last comma, so `DEBUG=sector,se<Tab>` offers seek.
                    const std::string val = frag.substr(eq + 1);
                    const size_t comma = val.rfind(',');
                    const size_t runAt = comma == std::string::npos ? 0 : comma + 1;
                    const std::string Uval = upper(val.substr(runAt));
                    comp.replaceFrom = wordStart + eq + 1 + runAt;
                    std::vector<std::string> opts = dc->flags();
                    opts.push_back("all");
                    opts.push_back("none");
                    for (const std::string& o : opts)
                        if (upper(o).compare(0, Uval.size(), Uval) == 0) comp.matches.push_back(o);
                    comp.suffix = " ";
                    return comp;
                }
            }

            std::vector<Property> props;
            if (consoleTarget) {
                props = Console::instance().properties();
            } else if (is(target, "DISPLAY")) {
                props = Display::properties();
            } else {
                size_t c = target.find(':');
                if (c == std::string::npos) {
                    if (Board* b = resolveQuiet(target)) props = b->properties();
                } else if (Board* b = resolveQuiet(target.substr(0, c))) {
                    // A unit target: its properties are the unit's, not the board's
                    // (DESIGN.md 7.2) -- resolved the way the SET executor resolves it.
                    UnitDef u;
                    if (b->findUnit(target.substr(c + 1), u)) props = b->unitProperties(u.name);
                }
            }
            completeProps(props);

            // The facility's KEYS ride alongside the Property keys (they are not Property
            // rows): DEBUG on the console is the sink; DEBUG/NODEBUG on a channel are its
            // flags. Only on the key side -- the value side returned above.
            if (eq == std::string::npos) {
                if (consoleTarget) keep("DEBUG");
                if (dc) {
                    keep("DEBUG");
                    keep("NODEBUG");
                }
            }
            return comp;
        }
        return comp;
    }

    // ---- the other target-taking commands: word 1 is a board id or id:unit ----
    if (wordIdx == 1) {
        if (name == "MOUNT" || name == "UNMOUNT") {
            completeTarget(frag, UnitUse::Mount, /*wantPseudo=*/false);
        } else if (name == "CONNECT" || name == "DISCONNECT") {
            completeTarget(frag, UnitUse::Connect, /*wantPseudo=*/false);
        } else if (name == "SHOW") {
            // SHOW <id> inspects a board, but its argument is just as often a keyword
            // (SHOW BUS, SHOW MOUNTS, ...). Offer both: the board ids and the canonical
            // sub-command words the SHOW handler dispatches on. No id:unit -- SHOW <id>
            // already prints a board's unit tables, so there is no colon step.
            for (const auto& b : m_.boards()) keep(b->id);
            for (const char* kw : {"BOARD", "BOARDS", "BUS", "CONSOLE", "DEBUG", "DISPLAY",
                                   "MACHINE", "MACHINES", "MOUNTS", "PATHS", "ROMS", "SYMBOLS",
                                   "VERSION"})
                keep(kw);
            comp.suffix = " ";
        } else if (name == "NOBREAK") {
            // NOBREAK takes a bare board id -- no unit, so no colon step.
            for (const auto& b : m_.boards()) keep(b->id);
            comp.suffix = " ";
        } else if (name == "BOARDS") {
            for (const char* kw : {"LIST", "ADD", "REMOVE"}) keep(kw);
            comp.suffix = " ";
        } else if (boardVerb) {
            // A verb a card brought (REWIND, ...). The verb cannot say which unit kind
            // it acts on, so offer them all -- boardCommand() checks the kind itself.
            completeTarget(frag, UnitUse::Any, /*wantPseudo=*/false);
        }
        return comp;
    }

    // ---- BOARDS ADD <type> (word 2) -- the registry's type names, like SHOW BOARDS ----
    if (name == "BOARDS" && wordIdx == 2 && is(toks[1], "ADD")) {
        for (const BoardType& t : boardTypes()) keep(t.name);
        comp.suffix = " ";
        return comp;
    }

    // ---- BOARDS REMOVE <id> (word 2) ----
    if (name == "BOARDS" && wordIdx == 2 && is(toks[1], "REMOVE")) {
        for (const auto& b : m_.boards()) keep(b->id);
        comp.suffix = " ";
        return comp;
    }

    return comp;
}

bool Monitor::boardCommand(const std::vector<std::string>& a, std::ostream& out) {
    std::string w = upper(a[0]);

    // Which verbs start with what was typed? Two 88-ACRs both declare REWIND: that is
    // ONE verb on two cards, not an ambiguity -- boardVerbs() has already folded them
    // together. What WOULD be ambiguous is two DIFFERENT verb names sharing a prefix.
    std::vector<CommandDef> hits;
    for (auto& v : boardVerbs())
        if (std::string(v.second.name).compare(0, w.size(), w) == 0) hits.push_back(v.second);

    if (hits.empty()) return false;  // nobody answers to it -- and that is the truth
    if (hits.size() > 1) {
        out << upper(a[0]) << ": ambiguous --";
        for (const CommandDef& h : hits) out << " " << h.name;
        out << "\n";
        failed_ = true;
        return true;
    }

    const CommandDef* def = &hits[0];

    // WHICH CARD? The verb cannot say. Two cassettes both answer to REWIND, and the
    // tape that gets rewound is the one you name -- so a board verb's first argument
    // is `<id>` or `<id>:<unit>`, read exactly as MOUNT and CONNECT read it. The
    // convention is enforced here, once, so no board has to reimplement it.
    if (a.size() < 2) {
        out << "usage: " << def->usage << "\n";
        failed_ = true;
        return true;
    }
    std::string spec = a[1];
    std::string id   = spec.substr(0, spec.find(':'));

    Board* b = board(id, out);
    if (!b) return true;  // board() has already said so

    // The card exists -- but is it one of the cards that brought this verb? `REWIND
    // mem0:tape` is a mistake with a cause, and the cause is worth saying.
    bool declared = false;
    for (const CommandDef& d : b->commands()) declared = declared || std::string(d.name) == def->name;
    if (!declared) {
        out << b->id << " (" << b->type() << ") has no " << def->name
            << ". That verb is here because some OTHER board in the machine brought it.\n";
        failed_ = true;
        return true;
    }

    std::string err;
    if (!b->runCommand(def->name, a, out, err)) {
        out << b->id << ": " << err << "\n";
        failed_ = true;
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

// WHAT A PROTECTED MEDIUM IS CALLED. A floppy and a cassette can each be WRITE-PROTECTED --
// on a diskette by the notch in its jacket, on a cassette by knocking out its tab -- and
// that is the physical thing, and the word the 88-DCDD manual uses. A ROM has no such
// mechanism; it is read-only because of what it IS, and calling it write-protected would
// promise a switch that does not exist.
static const char* protectedWord(UnitKind k) {
    return k == UnitKind::Rom ? "read-only" : "write-protected";
}

// The protection, in words, for a unit that has something in it.
//
// FORCED IS THE ONE WORTH THE INK. Write-protected when the operator asked for it is them
// being told what they asked for; write-protected when they did NOT is the difference
// between what was typed and what happened, and CP/M will spend an afternoon bouncing
// every write off it. The board says so once at MOUNT via drainLog(); this is the same
// fact, still here an hour later when that message has scrolled away.
static std::string roNote(const UnitDef& u) {
    if (!u.readOnly || u.state == "(empty)") return "";
    std::string s = std::string("  (") + protectedWord(u.kind);
    if (u.readOnlyForced) s += " -- THE HOST WON'T LET US WRITE IT; you did not ask for this";
    return s + ")";
}

void Monitor::showBoard(Board* b, std::ostream& out) {
    char buf[256];
    out << b->id << "  (" << b->type() << ")" << (b->enabled() ? "" : "  [DISABLED]") << "\n";

    if (auto* mem = dynamic_cast<MemoryBoard*>(b)) {
        const auto& rs = mem->regions();
        if (rs.empty()) {
            out << "  regions: (none -- this board is unpopulated and decodes nothing)\n";
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
            std::snprintf(buf, sizeof buf, "    %-7s %-7s %s%s", u.name.c_str(),
                          unitKindName(u.kind), u.state.c_str(), roNote(u).c_str());
            out << buf << "\n";
        }
    }

    showProps(b->properties(), out);

    // A UNIT's properties are the unit's, not the board's (DESIGN.md 7.2). The
    // two 6850s on a 2SIO have independent baud rates and independent transforms
    // because they are two independent chips, and printing them in one flat list
    // would be printing the PCB instead of the parts on it.
    for (const auto& u : us) {
        auto up = b->unitProperties(u.name);
        if (up.empty()) continue;
        out << "\n  " << b->id << ":" << u.name << "\n";
        showProps(up, out);
    }

    // ...AND THE KEYS OF ITS SUB-UNIT TABLES, which is a question SHOW could not answer
    // until the tables had a schema to answer it from. `readonly` was real, it worked, and
    // the only way to find out it existed was to read the board's source -- which is how
    // this bug was found (Patrick asked me to file it as a MISSING FEATURE).
    //
    // These have no value column and cannot have one: they describe a drive that does not
    // exist yet. What is on the card ALREADY is above, in `units` -- this is what you may
    // write in a machine file to put something there.
    for (const auto& t : b->subUnitTables()) {
        auto sp = b->subUnitProperties(t);
        if (sp.empty()) continue;
        out << "\n  [[board." << t << "]]  (in a machine file)\n";
        showSchema(sp, out);
    }
}

// The same six facts as showProps(), minus the value -- see above for why there isn't one.
void Monitor::showSchema(const std::vector<Property>& ps, std::ostream& out) {
    char buf[256];
    out << "\n  key              type             legal\n";
    for (const auto& p : ps) {
        const char* kind = "string";
        std::string legal;
        switch (p.kind) {
        case Kind::Bool:
            kind  = "bool";
            legal = "true|false";
            break;
        case Kind::Enum:
            kind = "enum";
            for (const auto& c : p.choices) legal += (legal.empty() ? "" : "|") + c;
            break;
        case Kind::Int:
            kind = "int";
            // RADIX-AWARE, because `at` is an address: printing "0..65535" for a thing you
            // write as F800 would be answering in a base the reader does not use here.
            if (!(p.min == 0 && p.max == 0)) {
                if (p.radix == 16)
                    std::snprintf(buf, sizeof buf, "%04llX..%04llX", (unsigned long long)p.min,
                                  (unsigned long long)p.max);
                else
                    std::snprintf(buf, sizeof buf, "%lld..%lld", p.min, p.max);
                legal = buf;
            }
            break;
        case Kind::Str: break;
        }
        // A free-form Str can still advertise a grammar (the ACR's `stop`); showProps does
        // the same, so the schema view and the value view agree on that column.
        if (legal.empty() && !p.values.empty()) legal = p.values;
        std::snprintf(buf, sizeof buf, "  %-16s %-16s %s", p.name.c_str(), kind, legal.c_str());
        out << buf << "\n";

        // A SECOND SPELLING GETS ITS OWN LINE, under the real one. It cannot share the key
        // column: the name there is what CONFIG SAVE writes, and an operator reading a
        // slash-separated pair has no way to tell which of the two that is.
        for (const auto& a : p.aliases) {
            std::snprintf(buf, sizeof buf, "  %-16s (another spelling of %s)", a.c_str(),
                          p.name.c_str());
            out << buf << "\n";
        }
    }
}

void Monitor::showProps(const std::vector<Property>& ps, std::ostream& out) {
    if (ps.empty()) return;
    char buf[256];
    out << "\n  property         value            legal\n";
    for (const auto& p : ps) {
        std::string legal;
        if (p.kind == Kind::Enum) {
            for (const auto& c : p.choices) legal += (legal.empty() ? "" : "|") + c;
        } else if (p.kind == Kind::Int && !(p.min == 0 && p.max == 0)) {
            // RADIX-AWARE, for the reason showSchema() gives above -- and here the reason
            // is sharper, because the VALUE is in the next column along. Printed decimal,
            // this row read `port  0xFC  0..252`: two bases side by side in one line, and
            // typing the number the column showed was then refused by an error in a THIRD
            // spelling (`port must be 0x0..0xFC`). Value::text() is what the value column
            // and the error message both use, so all three now agree.
            legal = Value::ofInt(p.min).text(p.radix) + ".." + Value::ofInt(p.max).text(p.radix);
        } else if (p.kind == Kind::Bool) {
            legal = "true|false";
        }
        // A free-form Str with a hand-written grammar (the ACR's `stop`) has no computable
        // set, so it carries its own hint -- otherwise this column is blank and the reader
        // has no idea `end` or a mm:ss is even accepted.
        if (legal.empty() && !p.values.empty()) legal = p.values;
        // No setter -> it is a PIN, not a jumper. Say so in the column that tells you
        // what you may type, because that is the question being asked there.
        if (!p.set) legal = "(read-only)";
        // There is no "runtime?" column any more (Patrick, 2026-07-12). EVERY
        // property can be set, always: you can only type at the prompt when the
        // machine is stopped, and a real card being worked on sits on an EXTENDER
        // with its jumpers moved live anyway. A column that always said "yes" was
        // just a column, and the gate behind it never once fired.
        std::snprintf(buf, sizeof buf, "  %-16s %-16s %s", p.name.c_str(),
                      p.get().text(p.radix).c_str(), legal.c_str());
        out << buf << "\n";
    }
}

// ---------------------------------------------------------------------------
// BOARDS -- the backplane.
//
// The old listing printed `mem:0000-DFFF,FF00-FFFF` and stopped there, which is
// the one question it cannot answer: WHICH of those is the ROM, and WHICH ROM is
// it? Both facts were already in the MapEntry (`what` and `note`) and were being
// thrown away. So each decoded range gets its own line -- a card carries several,
// and squashing them into one comma list was what hid the difference.
// ---------------------------------------------------------------------------

// "56K", or "512 bytes" -- because a 256-byte region is not "0K", and integer
// division reporting a real region as nothing costs somebody an afternoon.
static std::string sizeText(uint32_t n) {
    char b[32];
    if (n >= 1024 && n % 1024 == 0)
        std::snprintf(b, sizeof b, "%uK", (unsigned)(n / 1024));
    else
        std::snprintf(b, sizeof b, "%u bytes", (unsigned)n);
    return b;
}

// The chip in the socket, by the name a person would say: `roms/dbl.hex` and
// `builtin:dbl` are both "dbl.hex" / "dbl". The full path is in SHOW <id>.
static std::string chipName(const std::string& note, std::string& extra) {
    std::string s = note;
    size_t gap = s.find("  ");  // the board separates its own trailing notes by two spaces
    if (gap != std::string::npos) {
        extra = s.substr(gap + 2);
        s = s.substr(0, gap);
    }
    if (s.rfind("builtin:", 0) == 0) s = s.substr(8);
    size_t slash = s.find_last_of("/\\");
    if (slash != std::string::npos) s = s.substr(slash + 1);
    return s;
}

// ---------------------------------------------------------------------------
// SHOW MOUNTS -- every mountable unit in the machine, on one screen.
//
// This is a question NO `SHOW <id>` can answer, because it spans boards: "what is in
// this machine, and where did it come from." BOARDS gets close -- it marks a drive
// `(empty)` -- but it will not tell you WHICH disk is in drive0, and that is the half
// people actually want.
//
// EMPTY UNITS ARE LISTED. "Which drives are free?" is asked as often as "what is in
// drive0", and a table that silently omitted three of four drives would be one you had
// to learn not to trust.
//
// It reads Board::units() -- the same source showBoard() reads and the same source
// MOUNT itself resolves names against, so this table cannot drift from either.
// ---------------------------------------------------------------------------
void Monitor::showMounts(std::ostream& out) {
    struct Row { std::string unit, kind, holds; };
    std::vector<Row> rows;
    size_t wUnit = 4, wKind = 4;

    for (const auto& b : m_.boards()) {
        for (const auto& u : b->units()) {
            if (!isMountable(u.kind)) continue;  // a serial port is CONNECTed, not mounted
            Row r;
            r.unit  = b->id + ":" + u.name;
            r.kind  = unitKindName(u.kind);
            r.holds = u.state + roNote(u);
            wUnit   = std::max(wUnit, r.unit.size());
            wKind   = std::max(wKind, r.kind.size());
            rows.push_back(std::move(r));
        }
    }

    // A machine with nothing to mount is a real machine -- machines/altmon.toml is one --
    // so this is a fact about it, not a failure to find anything.
    if (rows.empty()) {
        out << "  (no mountable units -- this machine has no disk, tape or ROM sockets)\n";
        return;
    }

    char buf[512];
    std::snprintf(buf, sizeof buf, "  %-*s  %-*s  %s", (int)wUnit, "UNIT", (int)wKind, "KIND",
                  "HOLDS");
    out << buf << "\n";
    for (const auto& r : rows) {
        std::snprintf(buf, sizeof buf, "  %-*s  %-*s  %s", (int)wUnit, r.unit.c_str(),
                      (int)wKind, r.kind.c_str(), r.holds.c_str());
        out << buf << "\n";
    }
    out << "\n  Paths are AS WRITTEN.  SHOW PATHS says what they are relative to.\n";
}

// ---------------------------------------------------------------------------
// SHOW PATHS -- what a path resolves against, and there is more than one answer.
//
// THIS COMMAND EXISTS BECAUSE "WHAT IS THE BASE DIRECTORY" HAS NO SINGLE ANSWER, and
// the question gets asked (twice by Patrick, once as a bug report) because the rule is
// invisible. A bare `pwd` would have had to pick one of these three and imply it was
// the rule -- which is the misunderstanding, printed with authority.
//
// The three are genuinely different things and only the third is a fence:
//   - the shell's cwd: what a path you TYPE resolves against (and a -s script's).
//   - the machine file's dir: what a path WRITTEN IN IT resolves against, and nothing
//     else. It governs `mount`, `base`, and the MOUNT/LOAD lines in `startup`.
//   - hostdir: the guest's sandbox -- R.COM/W.COM cannot leave it. Not a base at all.
// ---------------------------------------------------------------------------
void Monitor::showPaths(std::ostream& out) {
    char buf[512];
    // One column for the label so the three answers line up and read as three answers to
    // the same question -- which is the whole point of putting them on one screen.
    auto row = [&](const std::string& label, const std::string& value) {
        std::snprintf(buf, sizeof buf, "  %-17s  %s", label.c_str(), value.c_str());
        out << buf << "\n";
    };
    const char* pad = "                     ";  // under the value column, for the prose

    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);

    row("what you type", ec ? "(unknown)" : cwd.string());
    out << pad << "MOUNT, LOAD, SAVE and -s scripts resolve against this.\n";

    out << "\n";

    // BUILT-IN vs FILE is `fromFile`, NEVER `m_.dir.empty()`: a machine file named in the
    // cwd (`altairsim foo.toml` from foo.toml's folder) has an empty dirname, and reading
    // that as "built in" made this command lie in exactly the folder every example README
    // tells you to `cd` into (empty dirname != no file, the issue-#25 shape). Fall through
    // to the sandbox loop either way -- a built-in machine can carry a host bridge too.
    if (!m_.fromFile) {
        row("machine file", "(none -- this machine is built in to the binary)");
    } else {
        // ABSOLUTE, for the same reason the sandbox is: the machine was named on the command
        // line, so this is as relative as what you typed. An empty dirname is the cwd -- the
        // file was named with no directory component -- so resolve it to the cwd we already
        // have rather than leaving the row blank.
        std::string mdir = m_.dir;
        if (mdir.empty()) {
            mdir = ec ? std::string(".") : cwd.string();
        } else {
            std::error_code e2;
            auto abs = std::filesystem::absolute(mdir, e2);
            if (!e2) mdir = abs.lexically_normal().string();
        }

        row("machine file", mdir);
        out << pad << "`mount`, `base` and the MOUNT/LOAD lines in `startup`\n"
            << pad << "resolve against THIS, not the cwd -- so a machine file\n"
            << pad << "names the disks lying beside it and goes on naming them\n"
            << pad << "from wherever you launch it.\n";
    }

    // hostdir is the hostbridge's own property, so it is printed only if the card is in
    // the backplane. A machine with no host bridge has no sandbox to describe -- which is
    // not a missing value, it is the truth about that machine.
    for (const auto& b : m_.boards()) {
        for (const auto& p : b->properties()) {
            // The RESOLVED root, not the written one. "Which directory is the guest
            // actually fenced into" is the only version of this question worth asking,
            // and `hostdir` reads back what someone typed -- which is `xfer` on both
            // sides of the bug that made this column necessary.
            if (p.name != "hostdir_root") continue;
            out << "\n";
            row(b->id + " sandbox", p.get().s());
            out << pad << "THE GUEST'S SANDBOX, and the only real fence here:\n"
                << pad << "R.COM/W.COM cannot leave it. It is not a base for\n"
                << pad << "anything you type. Set with `hostdir`.\n";
        }
    }
}

// ---------------------------------------------------------------------------
// SHOW VERSION -- which build this is, and which commit it came from.
//
// THE VERSION NUMBER ALONE IS NOT AN ANSWER. It changes at a release and stands still
// between them, so every CI artifact, every local build and every binary handed to
// somebody to try reads `0.1.0` -- and a report against one of those cannot be traced
// to the source that produced it. The commit can be, and the build is the only thing
// that knows it (core/version.h, and the version block in CMakeLists.txt).
//
// THE TREE STATE IS PART OF THE ANSWER, because a modified tree makes the commit a
// half-truth: the sha is real and the binary is not what is at it. That is the normal
// state of a hand-built binary, so it is worth a line rather than a footnote.
//
// This is a SHOW subcommand and not its own verb, because "tell me about X" already
// has a verb and a second one would only be a second thing to remember.
// ---------------------------------------------------------------------------
void Monitor::showVersion(std::ostream& out) {
    char buf[512];
    auto row = [&](const char* label, const std::string& value) {
        std::snprintf(buf, sizeof buf, "  %-9s  %s", label, value.c_str());
        out << buf << "\n";
    };

    row("altairsim", versionNumber());

    // WHETHER THIS BUILD CAN OPEN A WINDOW, and it is here because NOTHING ELSE SAYS.
    // Through v0.2.0 every released binary was headless and no output distinguished one:
    // --version, --help, -l, SHOW DISPLAY and this command were byte-identical either way,
    // so the only way to tell was nm/otool/dumpbin on the file. That is a toolchain the
    // machine holding the package may not have -- Git Bash on Windows has no `nm` at all --
    // which is why the answer belongs in the binary rather than in a probe of it.
    //
    // It is a row here rather than a line on --version because 4.2 step 5 checks --version
    // for a BARE "AltairSim X.Y.Z" and a second line there invites a false STOP.
#ifdef ALTAIRSIM_ENABLE_SDL
    {
        // "SDL3 was compiled in" is NOT the same question as "a window can open", and on
        // Linux they come apart: an SDL3 built with no X11/Wayland headers present configures
        // happily with ONLY its dummy driver, and then find_package succeeds, ldd names no
        // SDL, and a bare "SDL3" row here would lie -- the v0.2.0 headless failure reached by
        // another road. So enumerate the drivers SDL actually built. SDL_GetVideoDriver(i)
        // reads the static bootstrap table WITHOUT initialising video, so it is safe to call
        // from a SHOW VERSION that starts nothing (unlike SDL_GetCurrentVideoDriver()).
        std::string list;
        bool windowing = false;
        int n = SDL_GetNumVideoDrivers();
        for (int i = 0; i < n; ++i) {
            const char* d = SDL_GetVideoDriver(i);
            if (!d) continue;
            if (!list.empty()) list += ", ";
            list += d;
            // "dummy" and "offscreen" render to memory and open no window; anything else
            // (x11, wayland, cocoa, kmsdrm, windows, ...) is a real windowing backend.
            std::string dn(d);
            if (dn != "dummy" && dn != "offscreen") windowing = true;
        }
        if (windowing)
            row("video", "SDL3 -- windowed (" + list + ")");
        else
            row("video", "SDL3 -- NO WINDOW BACKEND (dummy only; " + list + ")");
    }
#else
    row("video", "none -- headless (null display)");
#endif

    // "unknown" is a real answer here, not a failure to look. There is no .git in a
    // release tarball, so there is nothing to describe -- and printing the version
    // number again in this slot would be a guess wearing provenance's clothes.
    if (std::string(versionCommit()) == "unknown") {
        row("commit", "unknown -- built from a tree with no git in it");
        return;
    }

    row("commit", versionCommit());
    if (versionDirty())
        row("tree", "MODIFIED when built -- this binary is not that commit");
    else
        row("tree", "clean");
}

// A tiny glob: '*' matches any run, '?' any one character. Both operands are already
// uppercased by the caller, so the match is case-insensitive like every name lookup.
static bool globMatch(const std::string& pat, const std::string& s) {
    size_t p = 0, t = 0, star = std::string::npos, mark = 0;
    while (t < s.size()) {
        if (p < pat.size() && (pat[p] == '?' || pat[p] == s[t])) { ++p; ++t; }
        else if (p < pat.size() && pat[p] == '*') { star = p++; mark = t; }
        else if (star != std::string::npos) { p = star + 1; t = ++mark; }
        else return false;
    }
    while (p < pat.size() && pat[p] == '*') ++p;
    return p == pat.size();
}

void Monitor::showSymbols(const std::vector<std::string>& a, std::ostream& out) {
    const SymbolTable& t = m_.syms;
    if (t.empty()) {
        out << "no symbols loaded -- SYMBOLS LOAD <file.PRN|file.SYM>\n";
        return;
    }

    // An optional filter: a bare name, or a glob (`SIO*`). A name with no wildcard is just
    // a one-symbol glob, so the two paths are one.
    std::string pat = a.size() > 2 ? upper(a[2]) : "";

    char buf[128];
    int shown = 0;
    for (const auto& [name, s] : t.byName) {
        if (!pat.empty() && !globMatch(pat, name)) continue;
        // An EQU is flagged so a constant is not mistaken for a program address -- it is the
        // one thing SHOW cannot recover from the value alone (core/symbols.h, the EQU rule).
        std::snprintf(buf, sizeof buf, "%-14s %s  %-14s %s", name.c_str(),
                      fmtWord((uint16_t)s.value).c_str(), s.source.c_str(), s.isAddr ? "" : "=");
        out << buf << "\n";
        ++shown;
    }
    if (pat.empty())
        out << shown << " symbol(s), across " << t.loadOrder.size() << " file(s)\n";
    else if (shown == 0)
        out << "no symbol matches '" << a[2] << "'\n";
}

void Monitor::showBoards(std::ostream& out, const Machine& m) {
    char buf[256];

    struct Row {
        std::string id, type, io, units;
        std::vector<std::string> mem;  // one line per DECODED range
        bool disabled = false;
    };
    std::vector<Row> rows;
    bool anyConsole = false;

    for (const auto& b : m.boards()) {
        Row r;
        r.id = b->id;
        r.type = b->type();
        r.disabled = !b->enabled();

        for (const auto& e : b->ioMap()) {
            std::snprintf(buf, sizeof buf, "%s%s", r.io.empty() ? "" : ",", fmtByte(e.lo).c_str());
            r.io += buf;
        }

        for (const auto& e : b->memMap()) {
            std::string detail, extra;
            if (e.what == "rom") {
                detail = chipName(e.note, extra);
                if (!extra.empty()) detail += "  " + extra;
            } else {
                detail = sizeText(e.hi - e.lo + 1);
                if (!e.note.empty()) detail += "  " + e.note;  // "bank 3 of 8"
            }
            std::snprintf(buf, sizeof buf, "%s-%s  %-3s  %s", fmtWord(e.lo).c_str(),
                          fmtWord(e.hi).c_str(), e.what.c_str(), detail.c_str());
            r.mem.push_back(buf);
        }

        // Units, grouped by kind and IN THE BOARD'S OWN ORDER: "2 serial: a*, b".
        // The count is what Patrick asked for; the designations are what you have
        // to type at MOUNT and CONNECT, so both are here or neither is useful.
        std::vector<std::pair<UnitKind, std::vector<std::string>>> byKind;
        for (const auto& u : b->units()) {
            std::string name = u.name;
            if (isMountable(u.kind) && u.state == "(empty)") name += "(empty)";
            if (u.kind == UnitKind::Serial && u.state == "console") {
                name += "*";
                anyConsole = true;
            }
            auto it = std::find_if(byKind.begin(), byKind.end(),
                                   [&](const auto& g) { return g.first == u.kind; });
            if (it == byKind.end())
                byKind.push_back({u.kind, {name}});
            else
                it->second.push_back(name);
        }
        for (const auto& [kind, names] : byKind) {
            std::string list;
            for (const auto& n : names) list += (list.empty() ? "" : ", ") + n;
            if (!r.units.empty()) r.units += "; ";
            r.units += std::to_string(names.size()) + " " + unitKindName(kind) + ": " + list;
        }

        rows.push_back(std::move(r));
    }

    // Widths from the DATA, so nothing is ever truncated and nothing is padded to
    // a width that a longer id would have blown out anyway.
    size_t wId = 2, wType = 4, wIo = 3, wUn = 5, wMem = 6;
    for (const auto& r : rows) {
        wId = std::max(wId, r.id.size());
        wType = std::max(wType, r.type.size());
        wIo = std::max(wIo, r.io.empty() ? 1 : r.io.size());
        wUn = std::max(wUn, r.units.empty() ? 1 : r.units.size());
        for (const auto& m : r.mem) wMem = std::max(wMem, m.size());
    }

    std::snprintf(buf, sizeof buf, "  %-*s  %-*s  %-*s  %-*s  %s", (int)wId, "ID", (int)wType,
                  "TYPE", (int)wIo, "I/O", (int)wUn, "UNITS", "MEMORY");
    out << buf << "\n";

    std::string rule = "  " + std::string(wId, '-') + "  " + std::string(wType, '-') + "  " +
                       std::string(wIo, '-') + "  " + std::string(wUn, '-') + "  " +
                       std::string(wMem, '-');
    out << rule << "\n";

    // Where a continuation line has to start to sit under MEMORY.
    const size_t memCol = 2 + wId + 2 + wType + 2 + wIo + 2 + wUn + 2;

    for (const auto& r : rows) {
        std::snprintf(buf, sizeof buf, "  %-*s  %-*s  %-*s  %-*s  %s", (int)wId, r.id.c_str(),
                      (int)wType, r.type.c_str(), (int)wIo, r.io.empty() ? "-" : r.io.c_str(),
                      (int)wUn, r.units.empty() ? "-" : r.units.c_str(),
                      r.mem.empty() ? "-" : r.mem[0].c_str());
        out << buf;
        if (r.disabled) out << "   [DISABLED]";
        out << "\n";
        for (size_t i = 1; i < r.mem.size(); ++i)
            out << std::string(memCol, ' ') << r.mem[i] << "\n";
    }

    if (anyConsole) out << "\n  * holds the console\n";
}

static void reportStop(const RunResult& r, const Debugger& dbg, std::ostream& out);

// ---------------------------------------------------------------------------
// CONSOLE mode -- the guest owns the keyboard.
//
// THE ONE THING THAT MATTERS HERE IS THAT YOU CAN GET OUT. Once the guest has
// the keyboard it has ALL of it, and every period monitor, BASIC and CP/M prompt
// sits in a tight loop reading the console -- so the guest would happily swallow
// any key we tried to use as an escape. ATTN is intercepted by the Console
// itself, below the filter and below the board, and the guest is never offered
// the byte. See src/host/console.cpp.
//
// The loop also THROTTLES to the CPU card's actual clock (DESIGN.md 8). Two
// reasons, and the second is the real one:
//
//   1. It stops a guest's idle poll loop from pinning a host core at 100%.
//   2. IT MAKES THE MACHINE RUN AT THE SPEED IT ACTUALLY RAN AT. We emulate ~40
//      MHz worth of 8080; unthrottled, a 2 MHz machine would do everything
//      twenty times too fast and every timing-dependent thing on the screen --
//      a cursor, a banner, a Teletype's pace -- would be a lie.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ^C.
//
// The handler does ONE thing: set a lock-free flag. It does not print, it does
// not touch the machine, and it does not throw -- those are all undefined in a
// signal handler, and the bug they produce is a hang or a corrupted heap once in
// a hundred runs, which is the worst kind there is.
//
// It is installed only for the duration of a RUN or a STEP, and the previous
// handler is put back afterwards, so ^C at the monitor prompt still kills the
// process exactly as it did before there was a CPU.
//
// ON A TERMINAL IT NEVER FIRES, and that is deliberate (Patrick, 2026-07-12): raw
// mode clears ISIG, because Ctrl-C is a byte CP/M is entitled to read. ATTN is the
// stop key. This guard is what is left for a PIPED run, where there is no raw mode
// and no ATTN, and the signal is the only way to stop a program that never ends.
// ---------------------------------------------------------------------------
static void onSigint(int) { Debugger::interrupt(); }

namespace {
struct SigintGuard {
    void (*prev)(int) = nullptr;
    SigintGuard() { prev = std::signal(SIGINT, onSigint); }
    ~SigintGuard() { std::signal(SIGINT, prev); }
};

// The two opcodes NEXT steps OVER instead of into: a subroutine call leaves a
// return address to stop at, so NEXT runs to it. These are 8080 encodings and the
// Z80 shares them, so the test holds for the core that is coming. Everything else
// (a JMP, a PCHL, an ordinary instruction) has no return to wait for and is a plain
// single step -- so it is NOT listed here. The return address is the instruction's
// own length past PC, which the disassembler gives us; NEXT never hard-codes +3/+1.
bool isCall(uint8_t op) {
    switch (op) {
    case 0xCD:                                  // CALL
    case 0xDD: case 0xED: case 0xFD:            // undocumented CALL aliases
    case 0xC4: case 0xCC:                        // CNZ, CZ
    case 0xD4: case 0xDC:                        // CNC, CC
    case 0xE4: case 0xEC:                        // CPO, CPE
    case 0xF4: case 0xFC:                        // CP,  CM
        return true;
    default:
        return false;
    }
}
bool isRst(uint8_t op) { return (op & 0xC7) == 0xC7; }  // RST 0..7 (C7..FF), 1 byte
} // namespace

// ---------------------------------------------------------------------------
// RUN. The switch on the front panel, and the ONLY way to start the machine
// (Patrick, 2026-07-12 -- this absorbed GO, which was the same loop with the
// terminal left alone).
//
// THERE IS ONE BRANCH IN HERE AND IT IS NOT A MODE. Whether your keystrokes
// reach the guest is not a question for the operator: it is a fact about the
// backplane. If a unit is connected to the console, the guest has the keyboard
// and the machine runs at the CPU card's crystal, because that is what the
// hardware does. If nothing is, there is no keyboard to hand over, so it just
// runs -- and ^C is still yours, because no guest is competing for it.
//
// Both paths stop on a breakpoint, on a HLT nothing can wake, and on ^C, and
// both report through the same reportStop. That is why GO had nothing left to be.
// ---------------------------------------------------------------------------
// The window, injected by the composition root (monitor.h). Borrowed, and null on
// every build and every test that has no window.
static Display* g_display = nullptr;

void Monitor::setDisplay(Display* d) { g_display = d; }

void Monitor::runMachine(std::ostream& out, bool stepOver) {
    CpuCore* cpu = needCpu(out);
    if (!cpu) return;

    // Re-arm the unclaimed-port de-dup for this run, so an absent port that was
    // reported on the last run is reported again on this one (DESIGN.md 4.6.1).
    // Without this a guest polling an absent UART would warn once, ever.
    m_.bus.resetUnclaimedWarnings();

    // TWO QUESTIONS THAT ARE NOT THE SAME ONE, and conflating them was a bug (#6).
    //
    // anyConsole: is a line wired to the INTERACTIVE console -- the host keyboard? That is
    // what decides whether keystrokes reach the guest and whether ^E means anything.
    //
    // anyRemoteLine: is a line wired to something REAL-TIME that is not the host terminal --
    // a socket a person has telnetted into, or a real serial port with hardware on it? Such
    // a machine has NO console by the test above (its state is "socket:..."/"serial:..."),
    // and the throttle used to gate on anyConsole alone -- so a machine whose only line was a
    // real UART PACED AGAINST NOTHING: `clock_hz` was a divisor every board obeyed with no
    // wall-clock behind it, and a 2 MHz machine ran at whatever the host could do. Same root
    // cause as the nap: the run loop asked the CONSOLE a question that belongs to the LINE.
    bool anyConsole   = false;
    bool anyRemoteLine = false;
    for (const auto& b : m_.boards())
        for (const auto& u : b->units()) {
            if (u.kind != UnitKind::Serial) continue;
            if (u.state == "console") anyConsole = true;
            else if (u.state != "null") anyRemoteLine = true;  // socket:/serial:/etc -- a live wire
        }

    Console& con = Console::instance();
    char     buf[96];

    using clk = std::chrono::steady_clock;
    const long long hz     = m_.clock.hz();
    uint64_t        startT = m_.clock.now();  // the throttle's baseline -- and an idle
    auto            start  = clk::now();      // nap RE-BASES it. See the nap, below.
    const bool      tty    = con.isTty();
    const char      attn   = (char)('A' + con.attn() - 1);

    // THE ACHIEVED CRYSTAL, measured here and published to the CPU card for SHOW.
    //
    // Its own baseline, SEPARATE from the throttle's, and deliberately NOT re-based by
    // the idle nap: the throttle wants to forget the idle time ("it never happened"),
    // but the achieved rate wants to REMEMBER it -- a machine that naps through a prompt
    // really is retiring few T-states a second, and that is the honest reading. So this
    // window counts all real time, nap and throttle-sleep alike, and is sampled long
    // enough (kMeasWindow) that the divide is not noise, short enough that SHOW reflects
    // what the machine is doing now rather than an average over the whole run.
    CpuCard* const card = m_.cpuCard();  // never null: needCpu() passed above
    uint64_t       measT = m_.clock.now();
    auto           measW = clk::now();
    static constexpr double kMeasWindow = 0.25;   // seconds; ~4 updates/sec while running
    static constexpr double kMeasFloor  = 0.02;   // shortest run worth a reading at all

    // ATTN IS THE STOP KEY, CONSOLE OR NO CONSOLE -- ^C IS NOT (Patrick,
    // 2026-07-12). Ctrl-C belongs to the guest: CP/M reads it, and a stop key the
    // guest also wants is a stop key that either breaks the guest or gets eaten by
    // it. ATTN is a key on the FRONT PANEL. It is the same key whatever is in the
    // backplane, so there is one thing to know and it is always true.
    //
    // The host owns the keyboard (host/console.h), so watching for ATTN is just
    // polling the buffer every slice -- it does not matter whether a board is
    // reading, or whether one exists. Raw mode is what makes it instant, so we take
    // the terminal even with no console connected.
    //
    // UNDER A PIPE WITH NO CONSOLE WE MUST NOT TOUCH STDIN: it is the monitor's own
    // script there, not a keyboard, and draining it would eat the next command.
    const bool watchKeys = anyConsole || tty;
    const bool takeTty   = watchKeys;

    // NEXT (stepOver) runs the callee silently: the operator asked to step over one
    // instruction, not to start the machine, so the "running from ..." banner would
    // be noise on every step. The raw-mode/pump/pace paths below are unchanged, so
    // the callee is still live and interruptible -- only the announcement is gone.
    if (!stepOver) {
        if (anyConsole) {
            out << "[console -- ^" << attn << " returns to the monitor]\n";
        } else {
            // Not an error, and it must not read like one: a machine with nothing
            // connected to a terminal is a machine that runs perfectly well. It is how
            // you run a ROM that talks to a disk, or a CPU test that talks to nobody.
            std::string stop = tty ? std::string("^") + attn + " stops it." : "^C stops it.";
            std::snprintf(buf, sizeof buf, "running from %s.  %s  (no console connected)",
                          fmtWord(cpu->pc()).c_str(), stop.c_str());
            out << buf << "\n";
        }
        out.flush();
    }
    if (takeTty) con.enterRaw();

    // ^C still stops a PIPED run, because there raw mode never happened and the
    // signal is all there is. On a terminal ISIG is off and this never fires --
    // which is the point: the guest gets that byte.
    SigintGuard guard;

    // Whose screen this is. Pushed at the start of every run rather than wired once,
    // because CONFIG LOAD replaces the machine -- and it can do that with the window
    // still open, showing the machine that has just been thrown away (host/display.h).
    // Null headless, and a no-op on a machine that never opens a window.
    if (g_display) {
        g_display->setTitle(m_.name);
        g_display->setRunning(true);  // clears any "simulator stopped" from the last stop
    }

    RunResult r;
    uint64_t lastWritten = con.written();
    uint64_t lastStarved = con.starved();
    int      quiet       = 0;

    // A LIVE TAPE COUNTER, painted a few times a second while a deck loads in wall time.
    // `counter` is the little state machine that decides what to do with our \r-status
    // line each window (tape_counter_line.h); `seenWritten` remembers how much the guest
    // had written to the console at the previous paint, so we can tell a free terminal
    // from one the guest is using. See the paint site in the sample block.
    TapeCounterLine counter;
    uint64_t        seenWritten = con.written();

    // WHEN THE GUEST LAST DID ANYTHING BUT WAIT. Default-constructed means "it is doing
    // something" -- and it has to persist in doing nothing before we believe it (see the
    // nap, at the bottom of the loop).
    clk::time_point idleSince{};

    for (;;) {
        // What the guest did with its slice: did it SAY anything, did it RECEIVE
        // anything, and how often did it come to the keyboard and find nothing there.
        // Those three are the whole of the idle judgement at the bottom of the loop.
        const uint64_t wasWritten  = con.written();
        const uint64_t wasReceived = m_.rxBytes();  // the WHOLE backplane, not just the console
        const uint64_t wasHungry   = con.hungry();

        // A slice, then a look around. Short enough that ATTN feels instant and a
        // keystroke is picked up promptly; long enough that the per-slice overhead
        // is noise.
        r = m_.debug.run(2000);

        // Every board with a line on it gets its slice of wall time, console or no
        // console: a 2SIO wired to a socket is still moving bytes when nobody is
        // sitting at the terminal.
        m_.pump();

        // The video window's own keyboard and close box, once a slice, for the same
        // reason the console is polled below -- and NOT from inside a board's pump(),
        // where it used to be. Drained there it rode on frame production, so a key
        // typed into the window waited for the cursor to blink (host/display.h). Null
        // when there is no window at all: headless, a test, a piped script.
        if (g_display) g_display->pollEvents();

        // The keyboard, once, for everybody: keys land in the host's buffer and ATTN
        // is taken out of the stream before the guest is ever offered it. One line,
        // and it is the same line whether a 2SIO is reading or the backplane is empty.
        if (watchKeys) con.poll();
        if (con.takeAttn()) {
            r.why = StopReason::Attn;
            break;
        }

        // The close box on the video window, asked once a slice for the same reason
        // ATTN is: it is the operator talking, and this is the only place that can
        // act on it. pollEvents() above is what drained the window's event queue, so
        // the click is already known by the time we ask.
        if (g_display && g_display->takeQuitRequest()) {
            r.why = StopReason::WindowClosed;
            break;
        }

        if (r.why != StopReason::Steps) break;  // breakpoint, HLT, ^C

        // ---- Knowing when to stop, WITH NOBODY THERE TO TELL US ----
        //
        // A terminal never ends. A PIPE does, and a scripted run that did not
        // notice would run the guest's input poll loop until the heat death of the
        // universe -- which is exactly what the first version of this did.
        //
        // So: once input has genuinely ENDED (not merely gone quiet -- see
        // Console::pollByte), give the guest a few more slices to finish saying
        // whatever it was saying, and leave when it falls silent. That way the
        // last command in a script still gets its answer printed.
        //
        // BUT SILENT IS NOT THE SAME AS FINISHED, and the first version of this got
        // that wrong. It left as soon as the guest stopped PRINTING -- and a cassette
        // bootstrap prints nothing at all for the whole of a 4,439-byte tape. Under
        // `-s`, loading 4K BASIC died three slices in, at PC=0003, before the loader
        // had read its second byte. The machine was not finished; it was BUSY.
        //
        // The guest is finished when it has stopped talking AND started BEGGING: gone
        // to the keyboard, found the pipe empty and ended, and come back for more. A
        // guest that is reading a tape never asks, so it is never cut off, however
        // long it takes and however little it says.
        if (anyConsole && !tty && con.eof()) {
            uint64_t w = con.written();
            uint64_t s = con.starved();
            bool     spoke  = (w != lastWritten);
            bool     begged = (s != lastStarved);
            quiet       = (begged && !spoke) ? quiet + 1 : 0;
            lastWritten = w;
            lastStarved = s;
            if (quiet >= 3) {
                r.why = StopReason::InputEnded;
                break;
            }
        }

        // ---- IS THE GUEST WORKING, OR IS IT WAITING FOR YOU? ----
        //
        // Every prompt ever written -- CP/M's `A0>`, BASIC's `OK`, a monitor's `.` --
        // spends its life in a two-instruction spin on a UART status bit. There is
        // nothing to compute and nothing to print; the machine is waiting for a human,
        // and a human takes a hundred million T-states to find the H key. Running that
        // spin flat out pinned a host core at 100% for no work at all.
        //
        // So: if the guest SAID NOTHING this slice, and came to the keyboard and found
        // it empty at least once every kIdleRatio instructions, it is doing nothing but
        // waiting -- stand down for a moment. It is the same sleep the throttle does and
        // for the same reason: THE HOST is idle, not the machine. Emulated time is
        // untouched, no board behaves differently, and the guest cannot tell (clock.h).
        //
        // THE RATIO IS THE DISCRIMINATION, and it is not close. A CP/M CONIN loop is
        // three instructions -- it polls ~600 times in a 2,000-instruction slice, twenty
        // times over the bar. A program that computes and checks for an abort key every
        // few hundred instructions polls a handful of times and is never taken for idle,
        // and one that has anything to SAY is excluded before we even count. If something
        // does trip it falsely it still turns 2,000 instructions every nap -- around
        // 500 kHz, a quarter of a real Altair -- and `SET cpu0 idle=off` ends the
        // argument.
        //
        // A TRANSFER LOOKS EXACTLY LIKE A PROMPT, AND THAT IS THE TRAP (Patrick,
        // 2026-07-13). A guest receiving XMODEM down the console line at 76,800 bps is
        // waiting for a byte every 130 us; in the gap it polls an empty keyboard exactly
        // as CONIN does, and it prints nothing for the whole 128-byte block. By the two
        // signals above it IS a prompt -- and an early draft of this loop napped straight
        // through a transfer, 4 ms at a time, which would have dragged 7.7 kB/s down to
        // 250 B/s. It was not a theory; it was measured, at 4.3% of a core where flat out
        // should have been.
        //
        // The difference between the two machines is not how they poll. It is that ONE OF
        // THEM IS RECEIVING BYTES. So a byte crossing into the guest is what resets the
        // clock -- consumed(), host/console.h -- and no transfer can nap, however quiet
        // and however hungry it looks, because bytes keep arriving.
        //
        // AND THE NAP IS STILL EARNED ON TOP OF THAT. Idleness must PERSIST for the
        // warmup before it is believed: the gap between two bytes of a transfer is
        // sub-millisecond, the gap before a human finds a key is for ever, and 20 ms
        // tells them apart with orders of magnitude to spare. It costs a prompt nothing
        // anyone can perceive and a transfer nothing at all. Any byte, any output, any
        // real work resets it.
        //
        // Measured on 8 MB CP/M at `A0>`: 100% of a core before this, ~3.5% after.
        static constexpr uint64_t kIdleRatio  = 32;  // an empty poll every 32 instructions
        static constexpr auto     kIdleWarmup = std::chrono::milliseconds(20);
        static constexpr auto     kIdleNap    = std::chrono::milliseconds(4);

        const SliceWork work{r.steps, con.written() - wasWritten, m_.rxBytes() - wasReceived,
                             con.hungry() - wasHungry};

        const bool idling =
            m_.clock.idle() && anyConsole && tty && guestIsWaiting(work, kIdleRatio);

        if (!idling) {
            idleSince = clk::time_point{};  // it did something. The clock starts over.
        } else if (idleSince == clk::time_point{}) {
            idleSince = clk::now();         // the first quiet slice. Now we watch.
        } else if (clk::now() - idleSince >= kIdleWarmup) {
            std::this_thread::sleep_for(kIdleNap);

            // AND THE THROTTLE MUST NOT TRY TO WIN THAT TIME BACK. It paces emulated
            // time against a baseline taken at RUN, so a nap leaves emulated time
            // behind -- and a 2 MHz machine would then sprint flat out the instant you
            // typed a key, for as long as you had been sitting at the prompt. Re-basing
            // says what is true: the idle time never happened.
            startT = m_.clock.now();
            start  = clk::now();
            continue;
        }

        // Throttle to the crystal on the CPU card -- but ONLY if the card HAS one.
        //
        // FLAT OUT IS THE DEFAULT (Patrick, 2026-07-13). `clock_hz = 0` -- which is
        // what a machine gets unless it asks otherwise -- means the run loop never
        // sleeps, and a 3,200-byte cassette that a real Altair took 110 seconds to
        // read comes off the tape in about a second. Ask for `clock_hz = 2000000`
        // and you get the 110 seconds back, exactly, because you asked.
        //
        // The tape is still period-correct either way: the ACR still spends 66,666
        // T-states on every 300-baud byte (clock.h). We are not speeding up the
        // TAPE, we are declining to sit and wait for it.
        //
        // PACE IF THERE IS ANYTHING REAL-TIME TO PACE FOR. An interactive console (a human
        // at the host keyboard) is one such thing -- but so is a socket someone dialed into
        // and so is a real serial port, and those have no console and no host tty at all.
        // Gating on `anyConsole && tty` alone meant a machine whose only line was a real UART
        // ran flat out no matter what crystal you asked for (#6): its transfers then outran
        // the wire, or (with a fixed-rate peer) simply lied about the speed. A PIPED console
        // -- state "console" but no tty -- is deliberately still NOT paced: a script has no
        // wall clock to keep in step with, which is what a `-c` run and a CPU test want.
        const bool pace = shouldPace(anyConsole, tty, anyRemoteLine, m_.clock.free());
        if (pace) {
            double want = (double)(m_.clock.now() - startT) / (double)hz;
            double got  = std::chrono::duration<double>(clk::now() - start).count();
            if (want > got) {
                std::this_thread::sleep_for(std::chrono::duration<double>(want - got));
            }
        }

        // Sample the achieved crystal once the window has enough real time behind it
        // for the divide to mean something, then start the next window. Unconditional
        // -- paced or flat out, idle or busy, this is just "how many T-states per real
        // second is this machine turning right now."
        double measReal = std::chrono::duration<double>(clk::now() - measW).count();
        if (measReal >= kMeasWindow) {
            card->reportAchievedHz((long long)((double)(m_.clock.now() - measT) / measReal));
            measT = m_.clock.now();
            measW = clk::now();

            // THE LIVE TAPE COUNTER. A board that is loading a cassette in wall time hands
            // back a one-line label here (Board::activityLabel); the run loop paints it and
            // knows nothing about tapes. Only on a real terminal, and only when the guest is
            // NOT itself writing to it -- on a Sol the guest paints its video window and on a
            // bare-Altair load nothing is printed, so the line is free exactly where a load
            // is slow enough to watch. A machine whose guest owns stdout keeps its output;
            // the counter yields and SHOW still reports the position on demand.
            if (tty) {
                std::string label;
                for (const auto& b : m_.boards()) {
                    label = b->activityLabel();
                    if (!label.empty()) break;
                }
                const bool consoleQuiet = (con.written() == seenWritten);
                switch (counter.update(label, consoleQuiet)) {
                    case TapeCounterLine::Act::Paint:
                        out << '\r' << counter.label << "\x1b[K" << std::flush;
                        break;
                    case TapeCounterLine::Act::WipeClean:
                        out << "\r\x1b[K" << std::flush;
                        break;
                    case TapeCounterLine::Act::Abandon:  // the guest owns the line now
                    case TapeCounterLine::Act::None:
                        break;
                }
                seenWritten = con.written();
            }
        }
    }

    // A live counter still on the terminal when the run stops (ATTN mid-load) must not be
    // left for the prompt to print on top of.
    if (counter.shown) out << "\r\x1b[K" << std::flush;

    // The tail: capture the final partial window so SHOW reflects what the machine was
    // doing when you stopped it -- and so a run shorter than one window (a CPU test, a
    // GO to a HLT) still leaves a reading instead of a stale zero. Below the floor the
    // divide is noise, so leave the last good sample standing.
    {
        double measReal = std::chrono::duration<double>(clk::now() - measW).count();
        if (measReal >= kMeasFloor)
            card->reportAchievedHz((long long)((double)(m_.clock.now() - measT) / measReal));
    }

    if (takeTty) con.leaveRaw();

    // The terminal is the operator's again, so make sure the operator can actually
    // type into it. If a video window took the keyboard -- which it does the moment
    // you click it, because it IS a keyboard -- the prompt below would otherwise be
    // printed somewhere the next keystroke will not go (host/display.h). Costs nothing
    // and does nothing on a machine with no window, and on every host but macOS.
    if (g_display) {
        g_display->yieldFocus();
        g_display->setRunning(false);  // the guest is stopped; say so on the frozen frame
    }

    if (anyConsole) out << "\n";  // the guest was mid-line; do not print on top of it

    // EVERY STOP SAYS WHY, and there is now exactly one path that says it. This
    // used to guess -- `Interrupted && anyConsole` meant "probably ATTN" -- and a
    // guess is what you write when the reason was never carried. Now it is: ATTN,
    // a script's input running out, and a real ^C are three different words.
    //
    // Under NEXT, a clean step-over completion (StepTarget) is the expected outcome
    // and stays silent -- the NEXT handler shows the registers itself. But a REAL
    // stop reached mid-callee is exactly the surprise the operator needs told: a
    // user breakpoint fired, the callee halted, or ATTN/^C took it back. Say those.
    if (!stepOver || r.why != StopReason::StepTarget) reportStop(r, m_.debug, out);

    // The tally is about WORK DONE, and none of taking the keyboard back, closing
    // the window, or running out of script is a fault worth counting instructions
    // over. NEXT is a single logical step, so it never prints a tally either.
    if (!stepOver && r.why != StopReason::Attn && r.why != StopReason::InputEnded &&
        r.why != StopReason::WindowClosed) {
        std::snprintf(buf, sizeof buf, "%llu instructions, %llu T-states.",
                      (unsigned long long)r.steps, (unsigned long long)r.tStates);
        out << buf << "\n";
    }
}

// The console is the host's terminal, and this is everything about it: what it
// is, what it is set to, and WHO HOLDS IT -- which is the question you actually
// have, and which lives on the units, not on the console.
// The host's video window, and what the operator has said about it. A settings object
// like `console`, not a board: a board draws a picture, and everything here is about
// the WINDOW that picture ends up in (host/display.h).
//
// Answers in a build with no video at all, and says so. The setting is still real
// there -- a machine file that asks for it still loads, and still means it on a host
// that can show it -- and reporting "no video service" beats reporting nothing and
// leaving the reader to wonder whether the property took.
// THE TEST IS THE MACRO, NOT THE POINTER. This asked `!g_display` until 2026-07-20, which
// reads like the discriminator and is not one: main.cpp calls setDisplay() UNCONDITIONALLY,
// handing a headless build a NullDisplay -- a perfectly non-null pointer. So the message
// never fired in any shipping binary, and the one place that claimed to say "this build has
// no video" said nothing, on the builds where it was the only thing that would have.
// A null pointer still means something (an embedder that never called setDisplay, as the
// unit tests do), so it keeps its own answer rather than being folded into the other.
void Monitor::showDisplay(std::ostream& out) {
    out << "display  (the host video window";
#ifndef ALTAIRSIM_ENABLE_SDL
    out << " -- no video service in this build";
#else
    if (!g_display) out << " -- no video service wired up";
#endif
    out << ")\n";
    showProps(Display::properties(), out);
}

void Monitor::showConsole(std::ostream& out) {
    Console& con = Console::instance();
    out << "console  (the host keyboard and screen" << (con.isTty() ? "" : " -- not a tty")
        << ")\n";
    showProps(con.properties(), out);

    std::string holder;
    for (const auto& b : m_.boards())
        for (const auto& u : b->units())
            if (u.kind == UnitKind::Serial && u.state == "console") {
                if (!holder.empty()) holder += ", ";
                holder += b->id + ":" + u.name;
            }
    out << "\n  held by  " << (holder.empty() ? "(nobody -- CONNECT <id>:<unit> console)" : holder)
        << "\n";
    out << "\n  The transforms (UPPER, STRIP7OUT, CRLF, BSDEL...) are the CONSOLE's, and\n"
           "  nothing else's: SET CONSOLE UPPER=ON. A board's line is 8-bit clean whatever\n"
           "  is plugged into it -- a filter there would corrupt XMODEM, silently.\n"
           "  What a board has instead is line coding: SHOW sio0 (baud, data_bits...).\n";
}

// ---------------------------------------------------------------------------
// SHOW DEBUG -- the runtime diagnostic facility (core/debuglog.h): the one global
// sink, and every registered channel with its flags. A flag that is ON is printed
// in UPPER CASE, so the state reads at a glance and greps cleanly.
//
// A channel is a board (named by its id) or an internal library (the 6850, the
// socket layer). The sink and the enabled flags are the operator's session, not
// machine config: they do NOT round-trip through CONFIG SAVE.
// ---------------------------------------------------------------------------
void Monitor::showDebug(std::ostream& out) {
    out << "debug  (runtime diagnostics -- the sink and flags do not survive CONFIG SAVE)\n";
    out << "\n  sink  " << dbg::sinkName()
        << "   -- SET CONSOLE DEBUG=stderr|stdout|<file>\n";

    auto chans = dbg::channels();
    if (chans.empty()) {
        out << "\n  (no channels registered)\n";
        return;
    }

    auto pad = [](std::string s, size_t w) {
        if (s.size() < w) s.append(w - s.size(), ' ');
        return s;
    };
    size_t w = 7;  // "CHANNEL"
    for (dbg::Channel* c : chans) w = std::max(w, c->name().size());

    out << "\n  " << pad("CHANNEL", w) << "  FLAGS  (an enabled flag is UPPER-CASE)\n";
    out << "  " << std::string(w, '-') << "  " << std::string(38, '-') << "\n";
    for (dbg::Channel* c : chans) {
        out << "  " << pad(c->name(), w) << "  ";
        const auto& fl = c->flags();
        if (fl.empty()) out << "(none)";
        for (size_t i = 0; i < fl.size(); ++i) {
            if (i) out << ' ';
            out << (c->on((unsigned)i) ? upper(fl[i]) : fl[i]);
        }
        out << "\n";
    }
    out << "\n  SET <channel> DEBUG=<flag>[,<flag>]  enables;  NODEBUG=<flag> disables;\n"
           "  DEBUG=all / DEBUG=none turn every flag on / off.\n";
}

// ---------------------------------------------------------------------------
// SHOW BUS IRQ -- the only part of the backplane you cannot otherwise see.
//
// Memory and I/O decoding are visible: a wrong one collides, or reads FF, and either
// way something happens. The interrupt wiring is different. It is EIGHT WIRES AND A
// PIN, none of them addressable, and the two ways of getting it wrong both fail in
// total silence:
//
//   * A `vi*` strap with no 88-VI in the machine. Nothing watches the VI lines, so the
//     card pulls its wire and the wire goes nowhere. THIS TREE SHIPPED THAT FOR MONTHS:
//     a vi3 strap parsed, validated, saved, and drove nothing.
//   * An `int` strap WITH an 88-VI present. The manual forbids it outright, and the
//     failure is a wrong vector rather than an error.
//
// Neither shows up in SHOW BUS, SHOW <id>, or BOARDS. So this view reports the wiring
// AND, like SHOW BUS CONTENTION, tells you when your machine is wrong.
//
// IT IS READ-ONLY, and that is load-bearing: it reads the LATCHED wires (intWire(),
// viWire(), Bus::viLines()) and asks the priority encoder intWinner(), all of which are
// pure. A SHOW command that perturbed the machine it was describing would be a debugger
// that lies.
namespace {

// A strap: where it is soldered, and what to call the thing that has it.
struct Strap {
    std::string who;  // "sio0:a", "vi0.rtc_interrupt", "dsk0"
    IrqJumper   where = IrqJumper::None;
};

// EVERY strap in the machine, found generically. No board-type list, no dynamic_cast:
// a strap is any property flagged irqJumper (Property::irqJumper), and every one of
// them is born in irqJumperProperty(). A board added next year appears here for free.
//
// Both levels, because both are real: the 88-SIO has two straps on the BOARD (its input
// and output devices, at independent priorities) while the 2SIO has one per UNIT.
std::vector<Strap> strapsOf(Board* b) {
    std::vector<Strap> out;
    auto take = [&](const Property& p, const std::string& owner) {
        if (!p.irqJumper) return;
        // A card with one strap just calls it `interrupt`; naming it again would be
        // noise. A card with two has to say which.
        std::string who = owner + (p.name == "interrupt" ? "" : "." + p.name);
        out.push_back({who, irqJumperFromText(p.get().s())});
    };
    for (const auto& p : b->properties()) take(p, b->id);
    for (const auto& u : b->units())
        for (const auto& p : b->unitProperties(u.name)) take(p, b->id + ":" + u.name);
    return out;
}

// Read one of a board's live-state properties by name. Optional by design: a future
// priority encoder that does not publish these simply gets a shorter header line,
// rather than the monitor having to know what an 88-VI is.
bool propBool(Board* b, const char* name) {
    for (const auto& p : b->properties())
        if (p.name == name) return p.get().b();
    return false;
}
long long propInt(Board* b, const char* name) {
    for (const auto& p : b->properties())
        if (p.name == name) return p.get().i();
    return 0;
}

} // namespace

void Monitor::showBusIrq(std::ostream& out, bool table) {
    char buf[256];

    std::vector<Strap> straps;
    std::vector<Board*> watchers;  // an 88-VI. Nothing else says yes.
    std::vector<std::string> pin73;
    for (const auto& b : m_.boards()) {
        for (auto& s : strapsOf(b.get()))
            if (s.where != IrqJumper::None) straps.push_back(std::move(s));
        if (b->enabled() && b->watchesVi()) watchers.push_back(b.get());
        if (b->intWire()) pin73.push_back(b->id);  // the LATCHED wire, disabled-aware
    }

    // Who wins. The encoder decides; we only ask. -1 = nothing would be acknowledged.
    int win = -1;
    Board* encoder = nullptr;
    for (Board* w : watchers) {
        int v = w->intWinner();
        if (v >= 0) {
            win     = v;
            encoder = w;
            break;
        }
    }

    out << "INTERRUPTS\n";

    // INTE. A backplane with no CPU is legal (milestone 1a ran one) and still has
    // wiring worth printing, so this is the one thing that may be absent.
    if (CpuCore* c = m_.cpu())
        out << (c->interruptsEnabled()
                    ? "  CPU     INTE on          an acknowledged interrupt will be taken\n"
                    : "  CPU     INTE off         interrupts are DISABLED; nothing will be "
                      "acknowledged\n");
    else
        out << "  CPU     (none)          this backplane has no processor\n";

    if (pin73.empty()) {
        out << "  pINT    idle             pin 73\n";
    } else {
        std::string ids;
        for (const auto& i : pin73) ids += " " + i;
        std::snprintf(buf, sizeof buf, "  pINT    ASSERTED         pin 73, pulled by%s",
                      ids.c_str());
        out << buf << "\n";
    }

    for (Board* w : watchers) {
        bool on = propBool(w, "vi_enabled");
        std::snprintf(buf, sizeof buf, "  88-VI   %-8s         %s", w->id.c_str(),
                      on ? "enabled" : "DISABLED (POC leaves it so until the guest enables it)");
        out << buf;
        if (on) {
            std::snprintf(buf, sizeof buf, ", current level %lld%s", propInt(w, "current_level"),
                          propBool(w, "level_live") ? " (live)" : " (compare off)");
            out << buf;
        }
        out << "\n";
    }
    if (watchers.empty())
        out << "  88-VI   (none)          nothing watches VI0-VI7; an acknowledged interrupt\n"
               "                          floats to FF, which the 8080 executes as RST 7\n";

    // ---- the eight wires ----
    uint8_t lines = m_.bus.viLines();
    if (table) out << "\n  LINE  VECTOR             STRAPPED             NOW\n";

    for (int i = 0; i < 8; ++i) {
        std::string who;
        for (const auto& s : straps)
            if (s.where == (IrqJumper)((int)IrqJumper::Vi0 + i)) who += (who.empty() ? "" : ",") + s.who;
        bool pulling = (lines >> i) & 1;

        // A summary prints only the lines that exist; the table prints the backplane.
        if (!table && who.empty() && !pulling) continue;

        const char* now = "--";
        std::string tail;
        if (pulling) {
            if (win == i) {
                now = "WINS";
                std::snprintf(buf, sizeof buf, "  <-- %s will jam %s",
                              encoder ? encoder->id.c_str() : "?", fmtByte(rstOpcode(i)).c_str());
                tail = buf;
            } else if (win >= 0) {
                now = "pending";  // a higher-priority line is winning right now
            } else if (watchers.empty()) {
                now  = "PULLING";
                tail = "  <-- and nothing is listening";
            } else {
                // The lowest line asking IS this one (winner() takes the lowest set
                // bit), yet nothing wins -- so the current-level compare refused it.
                now = "MASKED";
            }
        }

        std::snprintf(buf, sizeof buf, "  VI%d   RST %d  %s -> %s  %-20s %s%s", i, i,
                      fmtByte(rstOpcode(i)).c_str(), fmtWord((uint16_t)(i * 8)).c_str(),
                      who.empty() ? "--" : who.c_str(), now, tail.c_str());
        out << buf << "\n";
    }

    // ---- and now the part that earns the command its keep ----
    std::vector<std::string> warn;
    for (const auto& s : straps) {
        if (s.where >= IrqJumper::Vi0 && watchers.empty()) {
            std::snprintf(buf, sizeof buf,
                          "%s is strapped to VI%d, but no board in this machine watches the VI\n"
                          "  lines. This interrupt goes nowhere.",
                          s.who.c_str(), (int)s.where - (int)IrqJumper::Vi0);
            warn.push_back(buf);
        }
        if (s.where == IrqJumper::Int && !watchers.empty()) {
            std::snprintf(buf, sizeof buf,
                          "%s is strapped to pINT while an 88-VI (%s) is present. The 88-VI\n"
                          "  manual forbids this: \"A system designed to use the 88-VI may not have\n"
                          "  any I/O board strapped for single level interrupt.\"",
                          s.who.c_str(), watchers.front()->id.c_str());
            warn.push_back(buf);
        }
    }
    if (!pin73.empty() && m_.cpu() && !m_.cpu()->interruptsEnabled())
        warn.push_back("pin 73 is asserted but the CPU has interrupts disabled (DI). Nothing\n"
                       "  will be acknowledged until the guest runs EI.");

    if (!warn.empty()) {
        out << "\nWARNINGS\n";
        for (const auto& w : warn) out << "  " << w << "\n";
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
            std::snprintf(buf, sizeof buf, "  %s-%s  %-8s %-4s %s", fmtWord((uint16_t)r.lo).c_str(),
                          fmtWord((uint16_t)r.hi).c_str(), r.id.c_str(), r.what.c_str(),
                          r.note.c_str());
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
                std::snprintf(buf, sizeof buf, "%s%s-%s", holes.empty() ? "" : ",",
                              fmtWord((uint16_t)(run << 8)).c_str(),
                              fmtWord((uint16_t)((i << 8) - 1)).c_str());
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
                // Direction is what the bus actually decodes, not the board's prose:
                // probe both sides for THIS board and give each its own column. A
                // board may answer IN without OUT (a sense/status port) or OUT
                // without IN (a write-only strobe) -- a merged column hid that.
                auto answers = [&](Cycle t) {
                    BusCycle c;
                    c.type = t;
                    c.addr = (uint16_t)e.lo;
                    for (auto* r : m_.bus.respondersTo(c))
                        if (r == b.get()) return true;
                    return false;
                };
                std::string ports = fmtByte((uint8_t)e.lo);
                if (e.hi != e.lo) ports += "-" + fmtByte((uint8_t)e.hi);
                std::snprintf(buf, sizeof buf, "  %-8s  %-8s %-3s %-3s    %s", ports.c_str(),
                              b->id.c_str(), answers(Cycle::IoRead) ? "IN" : "--",
                              answers(Cycle::IoWrite) ? "OUT" : "--", e.note.c_str());
                out << buf << "\n";
                any = true;
            }
        if (!any) out << "  (no board decodes any port)\n";
        if (what == "IO") return;
    }

    if (what == "IRQ" || what.empty()) {
        // Bare SHOW BUS gets the summary -- the wires that exist and anything wrong
        // with them. The full eight-row backplane is SHOW BUS IRQ's.
        showBusIrq(out, /*table=*/what == "IRQ");
        if (what == "IRQ") return;
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
                    std::snprintf(buf, sizeof buf, "  %s %-5s driven by%s", fmtWord((uint16_t)A).c_str(),
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
                std::snprintf(buf, sizeof buf, "  port %s OUT driven by%s", fmtByte((uint8_t)P).c_str(), ids.c_str());
                out << buf << "\n";
                ++found;
            }
        }
        if (!found) out << "  none.\n";
        return;
    }

    if (!what.empty() && what != "MAP" && what != "IO" && what != "IRQ") {
        out << "SHOW BUS [MAP|IO|IRQ|CONTENTION]\n";
        failed_ = true;
    }
}

void Monitor::showRoms(std::ostream& out) {
    char buf[200];
    // Build the header with the SAME field widths as the rows below, so it can never
    // drift a space out of alignment when a column changes.
    std::snprintf(buf, sizeof buf, "%-9s %-12s %5s  %-8s  %-11s  %s",
                  "name", "file", "size", "CRC32", "decodes", "description");
    out << buf << "\n";
    for (const auto& r : builtinRoms()) {
        Image img;
        std::string err;
        std::string span = "(failed to decode)";
        std::string crc = "--------";
        if (decodeRom(r, 0, img, err) && !img.empty()) {
            auto flat = img.flat();
            std::snprintf(buf, sizeof buf, "%s-%s", fmtWord(img.lo()).c_str(), fmtWord(img.hi()).c_str());
            span = buf;
            std::snprintf(buf, sizeof buf, "%08X", crc32(flat));
            crc = buf;
        }
        // Description last, so a long one never disturbs the columns before it.
        std::snprintf(buf, sizeof buf, "%-9s %-12s %5zu  %s  %-11s  %s", r.name, r.file, img.size(),
                      crc.c_str(), span.c_str(), (r.desc && *r.desc) ? r.desc : "");
        out << buf << "\n";
    }
    if (builtinRoms().empty()) out << "(none compiled in)\n";
    // WHERE IT CAME FROM is the DESCRIPTION column, which names the author, and the CRC32,
    // which identifies the image exactly. The line used to send the reader to docs/roms.md
    // -- a file in the source tree that is not in the release package, so for anyone holding
    // a package it was a pointer to nothing. Say where it is, and say it is not in here.
    out << "\nUse as: mount = \"builtin:<name>\".  The description names who wrote each one,\n"
           "and the CRC32 says exactly which image this is; full provenance is in the\n"
           "project's source tree, not in the package.\n";
}

// ---------------------------------------------------------------------------
// The CPU, as the monitor sees it.
//
// NOTHING BELOW KNOWS WHAT AN 8080 IS. It asks the machine for the active core,
// the core for its registers and its instruction set, and the registry for a
// disassembler by name. The day an 8085 or a Z80 card lands, REGS, SET REG, STEP,
// GO, BREAK and DISASM all work against it with no change here -- which is the
// entire payoff of making registers reflection (DESIGN.md 3.0.3).
// ---------------------------------------------------------------------------

CpuCore* Monitor::needCpu(std::ostream& err) {
    CpuCore* c = m_.cpu();
    if (!c) {
        // Not an internal error -- a fact about the machine. An empty backplane is
        // a machine you can build, and it is the one milestone 1a ran.
        err << "no CPU in this machine.  BOARDS ADD 8080 cpu0\n";
        failed_ = true;
    }
    return c;
}

// PEEK, never read: a disassembly must not consume a byte from a UART that happens
// to live in the range you asked about (DESIGN.md 10.2). The status line goes
// through here too -- and IT would be the one to eat the console's own input.
Insn Monitor::insnAt(uint32_t at, const Disassembler& d) {
    auto peek = [this](uint16_t a) { return m_.bus.peek(a); };
    return d.at((uint16_t)at, peek, octalMode() ? 8 : 16);
}

// Substitute a symbol for a 16-bit operand address: `CALL 0005` -> `CALL BDOS`. The
// decoder tells us the operand's VALUE (in.operand, when in.operandBits == 16) -- a
// JMP/CALL/LXI immediate or a JR target, the one operand a name can stand for -- so
// we look the symbol up by value and splice it in for the rendered number. Rendering
// the number the same way the decoder did (fmtWord) and finding THAT is what makes
// this work whether the operand reads `0005` (hex) or `000 005` (split octal). No
// symbols loaded, no word operand, or no match: the text is returned untouched, so a
// symbol-less machine disassembles exactly as before.
std::string Monitor::annotateOperands(const Insn& in) const {
    if (m_.syms.empty() || in.operandBits != 16) return in.text;
    std::string name = m_.syms.operandName(in.operand);
    if (name.empty()) return in.text;
    std::string rendered = fmtWord(in.operand);
    size_t p = in.text.rfind(rendered);  // the operand is the trailing token
    if (p == std::string::npos) return in.text;
    return in.text.substr(0, p) + name + in.text.substr(p + rendered.size());
}

uint8_t Monitor::disasmLine(uint32_t at, const Disassembler& d, std::ostream& out) {
    auto peek = [this](uint16_t a) { return m_.bus.peek(a); };
    Insn in = insnAt(at, d);

    // A program label at this address heads its own line, the way an assembler listing
    // prints it -- so a jump destination reads as a name in the body AND announces
    // itself where it lands. Labels only (labelsAt), never an EQU: a constant that
    // happens to equal a code address must not print a phantom header.
    for (const std::string& label : m_.syms.labelsAt(at)) out << label << ":\n";

    std::string bytes;
    for (int i = 0; i < in.len; ++i) {
        bytes += fmtByte(peek((uint16_t)(at + (uint32_t)i)));
        bytes += ' ';
    }

    // Pad the byte column to the widest instruction (three bytes), each byte being
    // its digits plus a trailing space -- so the mnemonics line up in octal (four
    // columns a byte) as they always did in hex (three).
    int w = 3 * (byteWidth() + 1);
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s  %-*s %s", fmtWord((uint16_t)at).c_str(), w, bytes.c_str(),
                  annotateOperands(in).c_str());
    out << buf << "\n";
    return in.len;
}

// ONE LINE, DDT/SID style -- because three lines is what you read when you wanted
// to glance:
//
//     C0Z1M0E1I0 A=3F B=0000 D=00FF H=8000 S=0100 IE=1 P=0102  MOV A,B
//
// Still generic over registers(). The core said which registers are lamps, what to
// call them, and in what order (RegShow, cpu.h); this code has never heard of an
// accumulator, and a Z80 or a 6502 gets its own line here on the day it lands.
//
// No address and no hex bytes on the instruction -- P= just told you the address,
// and the bytes are what DISASM is for.
std::string Monitor::regLine(const std::vector<RegDef>& regs,
                             const std::function<uint32_t(size_t)>& valueAt) {
    std::string flags, fields;
    char buf[32];
    for (size_t i = 0; i < regs.size(); ++i) {
        const RegDef& r = regs[i];
        switch (r.show) {
        case RegShow::Off:
            break;
        case RegShow::Flag:
            std::snprintf(buf, sizeof buf, "%s%u", r.shown().c_str(), valueAt(i) ? 1u : 0u);
            flags += buf;
            break;
        case RegShow::Field:
            // A register narrower than a nibble has no hex digit to print; say the
            // number. That is IE, and anything like it a later core brings.
            if (r.bits < 4)
                std::snprintf(buf, sizeof buf, "%s=%u ", r.shown().c_str(), valueAt(i));
            else if (octalMode()) {
                // A wire register in split octal -- a byte in three digits, a word in
                // two byte-groups. Hex keeps the exact %0*X below, so it does not move.
                std::string val = r.bits <= 8 ? fmtByte((uint8_t)valueAt(i))
                                              : fmtWord((uint16_t)valueAt(i));
                std::snprintf(buf, sizeof buf, "%s=%s ", r.shown().c_str(), val.c_str());
            } else
                std::snprintf(buf, sizeof buf, "%s=%0*X ", r.shown().c_str(), r.bits / 4,
                              valueAt(i));
            fields += buf;
            break;
        }
    }

    std::string s = flags;
    if (!flags.empty() && !fields.empty()) s += " ";
    s += fields;
    return s;
}

void Monitor::showRegs(std::ostream& out) {
    CpuCore* c = m_.cpu();
    if (!c) return;

    auto regs = c->registers();
    out << regLine(regs, [&regs](size_t i) { return regs[i].get(); });

    if (const Disassembler* d = disassemblerFor(c->isa()))
        out << " " << annotateOperands(insnAt(c->pc(), *d));
    out << "\n";
}

// The recorded twin of showRegs: the same DDT line, rebuilt from an InsnRec. The
// register line pairs the active core's register LAYOUT with the record's stored
// VALUES (index for index -- exact for the usual single-core machine). The mnemonic
// is disassembled from the bytes that ACTUALLY ran at that PC, handed to the decoder
// through a peek over the record, so overwritten code still reads as what executed.
std::string Monitor::renderInsn(const Debugger::InsnRec& rec) {
    CpuCore* c = m_.cpu();
    if (!c) return "";

    auto regs = c->registers();
    std::string s = regLine(
        regs, [&rec](size_t i) { return i < rec.regs.size() ? rec.regs[i] : 0u; });

    if (const Disassembler* d = disassemblerFor(c->isa())) {
        auto peek = [&rec](uint16_t a) -> uint8_t {
            uint16_t off = (uint16_t)(a - rec.pc);
            return off < rec.nbytes ? rec.bytes[off] : (uint8_t)0;
        };
        s += " " + annotateOperands(d->at(rec.pc, peek, octalMode() ? 8 : 16));
    }
    return s;
}

// What stopped it, said out loud. A run that just... comes back, with no reason
// given, is a debugger you cannot trust.
static void reportStop(const RunResult& r, const Debugger& dbg, std::ostream& out) {
    char buf[120];
    switch (r.why) {
    case StopReason::Breakpoint: {
        std::string what = "?";
        for (const Breakpoint& b : dbg.breakpoints())
            if (b.id == r.bp) what = b.describe();
        std::snprintf(buf, sizeof buf, "breakpoint %d (%s) -- stopped at %s", r.bp,
                      what.c_str(), fmtWord(r.pc).c_str());
        out << buf << "\n";
        break;
    }
    case StopReason::Halted:
        std::snprintf(buf, sizeof buf,
                      "HLT at %s, and nothing can interrupt it -- no board is pulling pINT.",
                      fmtWord(r.pc).c_str());
        out << buf << "\n";
        break;
    case StopReason::Attn:
        // ATTN IS NOT A FAULT. You asked for the keyboard back, and the machine is
        // exactly where you left it -- so say that, and say how to go on.
        std::snprintf(buf, sizeof buf, "ATTN -- the machine is still at %s. RUN resumes.",
                      fmtWord(r.pc).c_str());
        out << buf << "\n";
        break;
    case StopReason::InputEnded:
        std::snprintf(buf, sizeof buf,
                      "input ended -- the machine is still at %s. RUN resumes.", fmtWord(r.pc).c_str());
        out << buf << "\n";
        break;
    case StopReason::Interrupted:
        out << "^C -- stopped at the instruction boundary. The machine is intact.\n";
        break;
    case StopReason::WindowClosed:
        // CLOSING THE WINDOW IS NOT QUITTING. It stops the guest and gives you the
        // prompt -- the machine is untouched and the window is still there, so say
        // both, and say how to go on and how to actually leave.
        std::snprintf(buf, sizeof buf,
                      "window closed -- the machine is still at %s. RUN resumes; QUIT exits.",
                      fmtWord(r.pc).c_str());
        out << buf << "\n";
        break;
    case StopReason::NoCpu:
        out << "no CPU in this machine.  BOARDS ADD 8080 cpu0\n";
        break;
    case StopReason::StepTarget:
        // NEXT stepped over the CALL/RST and landed on the return address. There is
        // nothing to announce -- the NEXT handler shows the registers, exactly as a
        // single STEP would. runMachine also filters this out before calling here,
        // so this case only ever fires if some other path reports a StepTarget stop.
        break;
    case StopReason::Steps:
        break;
    case StopReason::Unclaimed:
        // SET BUS UNCLAIMED=HALT caught the guest reaching a port no board decodes --
        // the hang this diagnostic exists to find (DESIGN.md 4.6.1). The warning line
        // with the exact PC is in the bus log, flushed right after this.
        std::snprintf(buf, sizeof buf,
                      "stopped: %s port 0x%02X, which no board decodes -- see the warning "
                      "below. RUN resumes.",
                      r.write ? "OUT to" : "IN from", r.port);
        out << buf << "\n";
        break;
    case StopReason::TapeStop: {
        // BREAK TAPE STOP fired: a cassette deck reached its auto-stop mark, so the load
        // has landed and the head is parked. Name the breakpoint the same way the ordinary
        // Breakpoint case does -- describe() renders "tape stop" from the one table.
        std::string what = "tape stop";
        for (const Breakpoint& b : dbg.breakpoints())
            if (b.id == r.bp) what = b.describe();
        std::snprintf(buf, sizeof buf, "breakpoint %d (%s) -- tape auto-stop, stopped at %s",
                      r.bp, what.c_str(), fmtWord(r.pc).c_str());
        out << buf << "\n";
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// exec
// ---------------------------------------------------------------------------

bool Monitor::exec(const std::string& line, std::ostream& out) {
    // A leading `!` is the shell escape: everything after it is handed to the host shell
    // VERBATIM, spaces and all. It is caught HERE, ahead of tokenize(), because tokenize
    // would split the line on spaces and treat a `#` or `;` as a comment -- and none of
    // that is the monitor's business to do to a shell command. The terminal is already in
    // the operator's own cooked mode at this point (the line editor's raw mode is scoped
    // to its read() and long since given back, src/cli/lineedit.cpp), so an interactive
    // program like `vi` inherits a normal terminal with nothing for us to set up or undo.
    if (size_t bang = line.find_first_not_of(" \t");
        bang != std::string::npos && line[bang] == '!') {
        std::string sh = line.substr(bang + 1);
        if (sh.find_first_not_of(" \t") == std::string::npos) {
            out << "  !<command>  -- run <command> in your host shell"
                   "   e.g. !ls, !vi HELLO.PRN\n";
            return true;
        }
        if (std::system(nullptr) == 0) {  // no command processor to hand it to
            out << "! -- no host shell is available.\n";
            failed_ = true;
            return true;
        }
        out.flush();
        std::cout.flush();  // our text lands before the child's, not tangled with it
        int rc = std::system(sh.c_str());
        (void)rc;  // the child's own exit status is the user's business, not ours
        return true;
    }

    auto a = tokenize(line);
    if (a.empty()) return true;

    // `.` REPEATS THE LAST COMMAND. Caught here -- after tokenize, before
    // resolveCommand (which would call it unknown) -- so a single keystroke walks
    // forward through the continuing verbs: bare DISASM/DUMP resume their cursor and
    // STEP steps again, so `.` `.` `.` keeps going. It runs quietly, with no echo of
    // the line it repeats. A `.` is never recorded as lastLine_, so pressing it again
    // re-runs the ORIGINAL command, not the previous `.` -- and cannot loop.
    if (a[0] == ".") {
        if (lastLine_.empty()) {
            out << ".  -- nothing to repeat yet.\n";
            return true;  // benign, even in a script: not a failure
        }
        return exec(lastLine_, out);
    }
    lastLine_ = line;  // a real command -- remember it for the next `.`

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
            // THE STATIC MENU HAS SAID NO -- so now, and only now, ask the cards.
            // A verb like REWIND exists exactly while the card that brings it is in
            // a slot, which is why it cannot live in the table above.
            if (boardCommand(a, out)) return true;

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

    // --- ROM: pull the qualifier out of anywhere in the line (DESIGN.md 10.2)
    //
    // "This write may program a ROM." It is the PROM burner, and it is a WRITE-side
    // qualifier only -- LOAD, DEPOSIT, FILL, MOVE. Nothing on the read side takes it,
    // because a ROM decodes reads perfectly well and always did: EXAMINE, DUMP, SAVE,
    // SEARCH and COMPARE see a ROM through the bus without being asked twice.
    //
    // THIS USED TO BE `RAW <id>`, and it used to name a board and address that board's
    // store by a LOCAL OFFSET (Patrick, 2026-07-17: board-local offsets are out as too
    // confusing -- every address refers to the one 64K address space). Two
    // things fell out of that. The board id went, because through the bus you never
    // name a board -- the address picks it -- so naming one carried no information the
    // address did not already carry. And the read side went, because it existed only to
    // reach a store the bus could not see: a bank that is not selected, or a phantomed
    // board. Both of those are PROPERTIES (SET mem0 bank=3, SET mem0 phantom=...), and
    // selecting the thing you want to look at is what the guest has to do too.
    //
    // What is left is the one thing a bus cycle genuinely cannot do (§4.2): put a byte
    // into a ROM. You pull the chip and put it in a programmer; that is not a bus
    // operation, and this word is that programmer.
    bool romOverride = false;
    for (size_t i = 1; i < a.size(); ++i) {
        if (is(a[i], "ROM")) {
            romOverride = true;
            a.erase(a.begin() + i);
            break;
        }
    }
    auto rd = [&](uint32_t x) -> uint8_t { return m_.bus.memRead((uint16_t)x); };

    // The burner itself is Machine::burn -- MCP programs ROM through the same call, and
    // two copies of "which chip is this?" would be two copies that drift.
    auto burn = [&](uint32_t A, uint8_t v, std::string& why) -> bool {
        return m_.burn((uint16_t)A, v, why);
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

            // The cards, in the same order the resolver asks them: built-ins first,
            // always. `HELP REW` has to reach the cassette, or a verb you can type is
            // a verb you cannot look up.
            //
            // `found` OUTLIVES the pointer into it. See boardVerbs().
            bool                    fromBoard = false;
            std::vector<CommandDef> found;
            if (!h) {
                std::string w = upper(a[1]);
                for (auto& v : boardVerbs())
                    if (found.empty() && std::string(v.second.name).compare(0, w.size(), w) == 0)
                        found.push_back(v.second);
                if (!found.empty()) {
                    h         = &found[0];
                    fromBoard = true;
                }
            }
            if (!h) {
                out << upper(a[1]) << ": no such command. HELP lists them.\n";
                failed_ = true;
                return true;
            }

            // EXTRA WORDS NARROW THE ANSWER. `HELP SHOW BOARD` asked about one
            // sub-level, not the whole of SHOW -- so keep only the detail lines that
            // begin with the words you typed. We do NOT keep a second list to do this:
            // the detail block is already one line per sub-command (commands.cpp), each
            // starting `NAME <sub> ...`, so filtering it IS reading the one authoritative
            // source. A command whose detail is prose matches nothing and falls back to
            // the whole block -- the same text you get today, never an error.
            std::string detail   = h->detail ? h->detail : "";
            bool        narrowed = false;
            if (h->detail && a.size() > 2 && !fromBoard) {
                std::string       kept;
                std::string       block = h->detail;
                for (size_t start = 0; start <= block.size();) {
                    size_t      nl   = block.find('\n', start);
                    std::string line = block.substr(
                        start, nl == std::string::npos ? std::string::npos : nl - start);
                    start = (nl == std::string::npos) ? block.size() + 1 : nl + 1;

                    std::vector<std::string> words = tokenize(line);
                    // A real sub-command line leads with the command's own name.
                    bool match = !words.empty() && upper(words[0]) == upper(h->name);
                    for (size_t k = 2; match && k < a.size(); ++k) {
                        std::string typed = upper(a[k]);
                        match = (k - 1 < words.size()) &&
                                upper(words[k - 1]).compare(0, typed.size(), typed) == 0;
                    }
                    if (match) {
                        if (!kept.empty()) kept += "\n";
                        kept += line;
                    }
                }
                if (!kept.empty()) {
                    detail   = kept;
                    narrowed = true;
                }
            }

            out << "\n  " << (fromBoard ? boardAbbreviation(*h) : abbreviation(*h)) << "\n";
            // The synopsis lists every sibling, so it only helps at the top level; once
            // you have narrowed to a sub-level the kept detail line IS the grammar.
            if (!narrowed) out << "  " << h->usage << "\n";
            if (!h->built)
                out << "\n  NOT IMPLEMENTED YET -- waiting on " << h->waiting << ".\n"
                    << "  It resolves today so that its abbreviation cannot change under\n"
                    << "  your fingers once it lands.\n";
            if (!detail.empty()) {
                out << "\n";
                // Indent every line of the detail block by two, including the examples.
                std::string d = detail;

                // `{endpoints}` is the ONE thing a help string may not spell out for
                // itself. CommandDef::detail is a `const char*` literal -- it cannot
                // call endpointHelp() -- so CONNECT's help used to carry a hand-copied
                // list, and it rotted: it still said "socket: and serial: are coming"
                // long after resolveEndpoint() implemented both. A literal that
                // DUPLICATES a list someone else owns is a second schema, and it drifts.
                // So the literal names the token and the printer asks the owner.
                for (size_t at = d.find("{endpoints}"); at != std::string::npos;
                     at        = d.find("{endpoints}", at))
                    d.replace(at, 11, endpointHelp());

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

        // The list. Names only, ALPHABETICAL, reading left to right across the row.
        // The table itself is in priority order (commands.cpp), and printing it in
        // that order made the list unusable for its one job: you come here hunting a
        // name you half-remember, and hunting means scanning, and scanning needs the
        // alphabet. Priority order is a fact about the RESOLVER, not about the reader.
        // Sorting a copy of the pointers leaves the table -- and every abbreviation
        // derived from it -- untouched.
        std::vector<const CommandDef*> sorted;
        for (const CommandDef& c : commands()) sorted.push_back(&c);
        std::sort(sorted.begin(), sorted.end(),
                  [](const CommandDef* x, const CommandDef* y) {
                      return std::string(x->name) < std::string(y->name);
                  });

        out << "\n";
        int  col       = 0;
        bool anyUnbuilt = false;
        for (const CommandDef* c : sorted) {
            std::string shown = abbreviation(*c);
            if (!c->built) {
                shown += "*";
                anyUnbuilt = true;
            }
            std::snprintf(buf, sizeof buf, "  %-16s", shown.c_str());
            out << buf;
            if (++col == 4) {
                out << "\n";
                col = 0;
            }
        }
        if (col) out << "\n";
        out << "\n  Type the part before the [brackets].";
        // The `*` legend only earns its line when something is actually starred --
        // and nothing is today. It reappears by itself the day a command is reserved
        // built=false again.
        if (anyUnbuilt) out << "  * = not built yet; it will say so.";
        out << "\n  HELP <command> for the usage and examples -- e.g. HELP DUMP.\n"
               "  . repeats your last command -- e.g. DI to disassemble, then . . . to keep going.\n"
               "  !<command> runs a command in your host shell -- e.g. !vi HELLO.PRN.\n\n"
               "  Numbers: on the wire is HEX (addresses, ports, bytes) -- or OCTAL\n"
               "  under SET CONSOLE base=octal; never on the wire is DECIMAL (counts,\n"
               "  widths, sizes). 0x/$/h force hex, 0o/q force octal, # forces decimal,\n"
               "  and a K/M suffix is always decimal.\n";

        // ---- AND THE VERBS THE CARDS BROUGHT WITH THEM ----
        //
        // Listed SEPARATELY, and never folded into the table above, because they are
        // not the same kind of thing: these exist only while the card that brings
        // them is in a slot. Pull the 88-ACR and REWIND is gone -- correctly, because
        // there is then nothing in the machine that can rewind.
        auto verbs = boardVerbs();
        if (!verbs.empty()) {
            out << "\n  From the boards in the machine right now:\n\n";
            for (auto& v : verbs) {
                std::snprintf(buf, sizeof buf, "  %-16s %s", boardAbbreviation(v.second).c_str(),
                              v.second.usage);
                out << buf << "  (" << v.first << ")\n";
            }
            out << "\n";
        }
        return true;
    }

    // ---------------- BOARD ----------------
    if (cmd == "BOARDS") {
        // A bare BOARDS is the list. It is the question people actually ask, and
        // making them type the word LIST to ask it is a toll booth.
        std::string sub = (a.size() < 2) ? "LIST" : upper(a[1]);

        if (sub == "TYPES" || sub == "TYPE") {
            // The catalog of board TYPES moved to SHOW, next to SHOW MACHINES: both answer
            // "what can I build?", so they belong together. A bare BOARDS is the backplane
            // you have; the things you could add are one SHOW away.
            out << "the board types moved to SHOW BOARDS (and SHOW BOARD <type> for one).\n";
            return true;
        }
        if (sub == "LIST") {
            if (m_.boards().empty()) {
                out << "(empty backplane)\n";
                return true;
            }
            showBoards(out, m_);
            return true;
        }
        if (sub == "ADD") {
            if (!need(4, "BOARDS ADD <type> <id> [key=value ...]")) return true;
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
                if (!setProperty(*b, k, v, e2)) {
                    out << e2 << "\n";
                    failed_ = true;
                }
            }
            out << b->id << ": " << b->type() << " added\n";
            return true;
        }
        if (sub == "REMOVE") {
            if (!need(3, "BOARDS REMOVE <id>")) return true;
            // Through board(), like every other command that names a card -- so
            // `BOARDS REMOVE ACR` reaches the same acr0 that `SHOW ACR` does, and an
            // ambiguous name is refused BEFORE anything is pulled out of the
            // backplane. Then remove by the id the CARD has, not the one that was
            // typed: it is the card we found, not the string.
            Board* b = board(a[2], out);
            if (!b) return true;
            std::string id = b->id;
            std::string err;
            if (!m_.remove(id, err)) {
                out << err << "\n";
                failed_ = true;
            } else {
                out << id << ": removed\n";
            }
            return true;
        }
        out << "BOARDS [LIST]|ADD <type> <id> [k=v...]|REMOVE <id>\n";
        failed_ = true;
        return true;
    }

    // ---------------- REGION ----------------
    // Populating a card interactively. Note this goes through the SAME generic
    // loadSubUnit() door the TOML loader uses -- so the monitor learns nothing about
    // what a region is, and `REGION ADD mem0 typ=ram` is refused here in the same words,
    // off the same declaration, as it would be in a machine file. A board that grows a
    // different sub-unit table next year needs no change here.
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
        if (!b->loadSubUnit("region", kv, err)) {
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
        if (!need(2, "SHOW <id> | SHOW BOARDS | SHOW BOARD <type> | SHOW MACHINES"
                     " | SHOW MACHINE [<name>] | SHOW BUS [MAP|IO|IRQ|CONTENTION] | SHOW ROMS"
                     " | SHOW MOUNTS | SHOW PATHS | SHOW DEBUG | SHOW VERSION"))
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
        // MOUNT and MOUNTS both, because the operator is coming here from the MOUNT verb
        // and making them find the S is a toll booth (the same reason BOARDS needs no LIST).
        if (sub == "MOUNTS" || sub == "MOUNT") {
            showMounts(out);
            return true;
        }
        if (sub == "PATHS" || sub == "PATH" || sub == "PWD") {
            showPaths(out);
            return true;
        }
        if (sub == "CONSOLE") {
            showConsole(out);
            return true;
        }
        if (sub == "DEBUG") {
            showDebug(out);
            return true;
        }
        // BUILD as well as VERSION: half the time the question being asked is "which
        // build is this", and the operator should not have to guess our noun.
        if (sub == "VERSION" || sub == "BUILD") {
            showVersion(out);
            return true;
        }
        if (sub == "DISPLAY" || sub == "VIDEO" || sub == "WINDOW") {
            showDisplay(out);
            return true;
        }
        if (sub == "SYMBOLS" || sub == "SYMBOL" || sub == "SYM") {
            showSymbols(a, out);
            return true;
        }
        if (sub == "BOARDS" || sub == "BOARD") {
            // The board catalog -- what you can ADD. Plural BOARDS is the whole list, one
            // aligned row per type with the description WRAPPED inside its column; singular
            // BOARD <type> drills into one: its description, then its properties. Either
            // spelling works with or without a name -- the presence of the name decides.
            // This lives beside SHOW MACHINES: both answer "what can I build?".
            const auto types = boardTypes();
            const int width = 78;  // the monitor's working screen; wider text is a nuisance

            if (a.size() >= 3) {
                const std::string want = lowerAscii(a[2]);
                const BoardType* t = nullptr;
                for (const auto& e : types)
                    if (lowerAscii(e.name) == want) { t = &e; break; }
                if (!t) {
                    out << "no board type '" << a[2] << "'. SHOW BOARDS lists them.\n";
                    return true;
                }

                // An optional trailing UNITS (u) narrows the view to just the unit and
                // sub-unit tables -- the answer to "what can I SET on acr0:tape" without
                // the board's own properties or its description in the way.
                bool unitsOnly = false;
                if (a.size() >= 4) {
                    // Any prefix of UNITS matches -- u, un, uni, unit, units -- the way
                    // every other command word abbreviates (first match wins).
                    const std::string opt = lowerAscii(a[3]);
                    if (std::string("units").starts_with(opt))
                        unitsOnly = true;
                    else {
                        out << "SHOW BOARD <type> [units]\n";
                        return true;
                    }
                }

                if (!unitsOnly) {
                    const size_t descCol = 2 + t->name.size() + 2;
                    auto desc = wrapText(t->description, width - descCol);
                    std::snprintf(buf, sizeof buf, "  %s  %s", t->name.c_str(), desc[0].c_str());
                    out << buf << "\n";
                    for (size_t i = 1; i < desc.size(); ++i)
                        out << std::string(descCol, ' ') << desc[i] << "\n";
                }

                auto b = makeBoard(t->name);

                // One PROPERTY / HELP / values table, self-sizing its name column. The
                // board's own properties and each unit's print through the same renderer,
                // so the catalog view of a not-yet-added board matches SHOW <id> of a live
                // one -- help text and all.
                // `header` prints the PROPERTY/HELP heading and its rule; the UNITS view
                // suppresses it after the first unit so the tables read as one list. A
                // non-zero `fixedWProp` forces the name column width, so a single header can
                // align over units whose own longest name would size the column differently.
                auto renderProps = [&](const std::vector<Property>& props, bool header,
                                       size_t fixedWProp) {
                    size_t wProp = fixedWProp;
                    if (wProp == 0) {
                        wProp = 8;  // "PROPERTY"
                        for (const auto& p : props) wProp = std::max(wProp, p.name.size());
                    }
                    const size_t helpCol = 2 + wProp + 2;

                    out << "\n";
                    if (header) {
                        std::snprintf(buf, sizeof buf, "  %-*s  %s", (int)wProp, "PROPERTY",
                                      "HELP");
                        out << buf << "\n";
                        out << "  " << std::string(wProp, '-') << "  "
                            << std::string(width - helpCol, '-') << "\n";
                    }
                    for (const auto& p : props) {
                        auto help = wrapText(p.help, width - helpCol);
                        std::snprintf(buf, sizeof buf, "  %-*s  %s", (int)wProp, p.name.c_str(),
                                      help[0].c_str());
                        out << buf << "\n";
                        for (size_t i = 1; i < help.size(); ++i)
                            out << std::string(helpCol, ' ') << help[i] << "\n";
                        // The legal values, aligned under the help, for properties that list
                        // them (enum choices, on|off, a bounded range). Wrapped like the help
                        // so a long choice set does not overrun the terminal.
                        auto legal = legalValues(p);
                        if (!legal.empty())
                            for (const auto& line : wrapText("values: " + legal, width - helpCol))
                                out << std::string(helpCol, ' ') << line << "\n";
                    }
                };

                const auto props = b->properties();

                // Collect the units (and sub-unit table schemas) that carry properties --
                // a unit's properties are the unit's, not the board's (DESIGN.md 7.2). Both
                // views speak about them: the full view names them in a footer, the UNITS
                // view prints each one's table. Each entry keeps its heading and its props.
                struct UnitEntry {
                    std::string           heading;  // the "unit '...'  (kind)" line
                    std::string           label;    // the bare name, for the footer list
                    std::vector<Property> props;
                };
                std::vector<UnitEntry> unitEntries;
                for (const auto& u : b->units()) {
                    auto up = b->unitProperties(u.name);
                    if (up.empty()) continue;
                    // Name the unit's kind and, after it, the verb that fills it --
                    // "(serial, CONNECT)" -- so the reader knows a serial unit takes
                    // CONNECT, not MOUNT. A Cpu core takes neither, so it shows only
                    // its kind.
                    const std::string verb = unitKindVerb(u.kind);
                    std::string       heading = "  Unit '" + u.name + "'  ("
                                        + unitKindName(u.kind);
                    if (!verb.empty()) heading += ", " + verb;
                    heading += ")";
                    unitEntries.push_back({heading, u.name, std::move(up)});
                }
                // ...and the sub-unit table schemas (a drive you may declare in a machine
                // file): the same hidden schema, shown the same way SHOW <id> shows it.
                for (const auto& tbl : b->subUnitTables()) {
                    auto sp = b->subUnitProperties(tbl);
                    if (sp.empty()) continue;
                    unitEntries.push_back({"  [[board." + tbl + "]]  (in a machine file)",
                                           tbl, std::move(sp)});
                }

                if (unitsOnly) {
                    if (unitEntries.empty()) {
                        out << "  board '" << t->name << "' has no unit properties.\n";
                        return true;
                    }
                    // Two blank lines between units set each "Unit 'tape2'  (tape)" break
                    // off from the table above, so it does not get lost. The first heading
                    // needs only the single blank the others already carry -- no extra at
                    // the top. The PROPERTY/HELP header prints once, over the first unit;
                    // one column width, sized across every unit, keeps the rest aligned.
                    size_t wProp = 8;  // "PROPERTY"
                    for (const auto& e : unitEntries)
                        for (const auto& p : e.props) wProp = std::max(wProp, p.name.size());
                    bool first = true;
                    for (const auto& e : unitEntries) {
                        out << (first ? "\n" : "\n\n") << e.heading << "\n";
                        renderProps(e.props, first, wProp);
                        first = false;
                    }
                    return true;
                }

                // The full view: the board's own properties, then -- if it has units -- a
                // footer naming them and pointing at the UNITS view for their settings,
                // rather than stacking every unit table under the board's.
                if (!props.empty()) renderProps(props, true, 0);
                if (unitEntries.empty()) {
                    if (props.empty()) out << "\n  (no properties)\n";
                    return true;
                }
                out << "\n  This board has units: ";
                for (size_t i = 0; i < unitEntries.size(); ++i)
                    out << (i ? ", " : "") << unitEntries[i].label;
                out << "\n  SHOW BOARD " << t->name << " UNITS for their properties.\n";
                return true;
            }

            // The catalog. Name column sized to the data, description wrapped beneath it.
            size_t wName = 4;  // "TYPE"
            for (const auto& t : types) wName = std::max(wName, t.name.size());
            const size_t descCol = 2 + wName + 2;

            std::snprintf(buf, sizeof buf, "  %-*s  %s", (int)wName, "TYPE", "DESCRIPTION");
            out << buf << "\n";
            out << "  " << std::string(wName, '-') << "  "
                << std::string(width - descCol, '-') << "\n";
            for (const auto& t : types) {
                auto desc = wrapText(t.description, width - descCol);
                std::snprintf(buf, sizeof buf, "  %-*s  %s", (int)wName, t.name.c_str(),
                              desc[0].c_str());
                out << buf << "\n";
                for (size_t i = 1; i < desc.size(); ++i)
                    out << std::string(descCol, ' ') << desc[i] << "\n";
            }
            out << "\n  SHOW BOARD <type> for a board's properties"
                   " (add UNITS for just the units)\n";
            return true;
        }
        if (sub == "MACHINES") {
            // The catalog of built-in machines -- the same list `--list` prints from the
            // shell, now reachable from the prompt: name + one-line blurb, wrapped.
            const auto machines = builtinMachines();
            const int width = 78;
            size_t wName = 4;  // "NAME"
            for (const auto& b : machines) wName = std::max(wName, std::string(b.name).size());
            const size_t blurbCol = 2 + wName + 2;

            std::snprintf(buf, sizeof buf, "  %-*s  %s", (int)wName, "NAME", "DESCRIPTION");
            out << buf << "\n";
            out << "  " << std::string(wName, '-') << "  "
                << std::string(width - blurbCol, '-') << "\n";
            for (const auto& b : machines) {
                auto blurb = wrapText(b.blurb, width - blurbCol);
                std::snprintf(buf, sizeof buf, "  %-*s  %s", (int)wName, b.name,
                              blurb[0].c_str());
                out << buf << "\n";
                for (size_t i = 1; i < blurb.size(); ++i)
                    out << std::string(blurbCol, ' ') << blurb[i] << "\n";
            }
            out << "\n  SHOW MACHINE <name> for what is in one\n";
            return true;
        }
        if (sub == "MACHINE") {
            if (a.size() >= 3) {
                // A built-in, loaded into a SCRATCH machine so the live one is untouched --
                // exactly what `altairsim -x 'SHOW MACHINE' <name>` does, minus the swap.
                const std::string want = lowerAscii(a[2]);
                const BuiltinMachine* bm = nullptr;
                for (const auto& b : builtinMachines())
                    if (lowerAscii(b.name) == want) { bm = &b; break; }
                if (!bm) {
                    out << "no built-in machine '" << a[2] << "'. SHOW MACHINES lists them.\n";
                    return true;
                }
                Machine tmp;
                std::string err;
                if (!loadMachine(*bm, tmp, err)) {
                    out << err << "\n";
                    return true;
                }
                out << "name      " << bm->name << "\n";
                for (const auto& line : wrapText(bm->blurb, 78 - 10))
                    out << "          " << line << "\n";
                out << "startup   " << (tmp.startup.empty() ? "(none)" : "") << "\n";
                for (const auto& s : tmp.startup) out << "            " << s << "\n";
                out << "\n";
                showBoards(out, tmp);
                return true;
            }
            // The live machine, in exactly the shape `SHOW MACHINE <name>` prints a
            // built-in -- minus the blurb, which a running machine has no equivalent of.
            // The board table carries the CPU and its ISA in the UNITS column, and the
            // clock and sense switches remain the CPU and front-panel boards' properties
            // (DESIGN.md 3, 8): `SHOW cpu0` and `SHOW fp0` are where those live.
            out << "name      " << m_.name << "\n";
            out << "startup   " << (m_.startup.empty() ? "(none)" : "") << "\n";
            for (const auto& s : m_.startup) out << "            " << s << "\n";
            out << "\n";
            showBoards(out, m_);
            return true;
        }
        Board* b = board(a[1], out);
        if (b) showBoard(b, out);
        return true;
    }

    if (cmd == "SET") {
        if (!need(3, "SET <id>[:<unit>]|CONSOLE|DISPLAY|REG|BUS <key>=<value>")) return true;
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
            if (upper(a[2]).rfind("UNCLAIMED", 0) == 0) {
                // The floating-bus diagnostic (DESIGN.md 4.6.1). HALT stops the guest
                // at the offending cycle; WARN just logs it; SILENT is the default.
                Unclaimed p = v == "SILENT" ? Unclaimed::Silent
                              : v == "HALT" ? Unclaimed::Halt
                                            : Unclaimed::Warn;
                m_.bus.setUnclaimedPolicy(p);
                out << "bus: unclaimed="
                    << (p == Unclaimed::Silent ? "SILENT" : p == Unclaimed::Halt ? "HALT" : "WARN")
                    << "\n";
                return true;
            }
            out << "SET BUS CONTENTION=WARN|ERROR|SILENT | UNCLAIMED=WARN|HALT|SILENT\n";
            failed_ = true;
            return true;
        }

        // SET REG A=3F -- and the flags are registers too, so SET REG CY=1 works
        // and nothing here had to be told what a flag is.
        //
        // A register value IS on the wire, so it is HEX (DESIGN.md 10.0.1). `SET
        // REG A=10` is sixteen, exactly as `EX 10` is address sixteen.
        if (is(a[1], "REG")) {
            CpuCore* c = needCpu(out);
            if (!c) return true;
            std::string k, v;
            size_t eq = a[2].find('=');
            if (eq != std::string::npos) {
                k = a[2].substr(0, eq);
                v = a[2].substr(eq + 1);
            } else if (a.size() >= 4) {
                k = a[2];
                v = a[3];
            } else {
                out << "usage: SET REG <r>=<v>\n";
                failed_ = true;
                return true;
            }
            for (const RegDef& r : c->registers()) {
                if (upper(r.name) != upper(k)) continue;
                uint32_t val;
                if (!addr(v, val, out)) return true;
                uint32_t max = r.bits >= 32 ? 0xFFFFFFFFu : (1u << r.bits) - 1;
                if (val > max) {
                    char e[96];
                    std::snprintf(e, sizeof e,
                                  octalMode() ? "%s is %d bits -- %o does not fit."
                                              : "%s is %d bits -- %X does not fit.",
                                  r.name.c_str(), r.bits, val);
                    out << e << "\n";
                    failed_ = true;
                    return true;
                }
                r.set(val);
                showRegs(out);
                return true;
            }
            // Name them HERE. The status line shows a DDT layout, not a catalogue --
            // the halves and the packed flag byte are settable but not on it, so
            // "REGS lists them" would have been a lie the moment we compacted it.
            std::string names;
            for (const RegDef& r : c->registers()) {
                if (!names.empty()) names += " ";
                names += r.name;
            }
            out << upper(k) << ": no such register. This CPU has: " << names << "\n";
            failed_ = true;
            return true;
        }

        // key=value, or `key value` -- worked out before we know WHAT we are
        // setting, because the grammar is the same for a board, a unit and the
        // console, and there is no reason for three copies of it.
        std::string k, v;
        {
            size_t eq = a[2].find('=');
            if (eq != std::string::npos) {
                k = a[2].substr(0, eq);
                v = a[2].substr(eq + 1);
            } else if (a.size() >= 4) {
                k = a[2];
                v = a[3];
            } else {
                out << "usage: SET <id>[:<unit>] <key>=<value>  |  SET CONSOLE <key>=<value>"
                       "  |  SET DISPLAY <key>=<value>\n";
                failed_ = true;
                return true;
            }
        }

        if (is(a[1], "CONSOLE")) {
            // DEBUG on the console is the one global diagnostic SINK, not a console
            // property -- it lives on the dbg facility (per-channel flags are set on
            // the channel: SET <board> DEBUG=..., handled below).
            if (is(k, "DEBUG")) {
                std::string err;
                if (!applyDebugSink(v, err)) {
                    out << err << "\n";
                    failed_ = true;
                } else {
                    out << "debug: sink=" << dbg::sinkName() << "\n";
                }
                return true;
            }
            std::string err;
            if (!setPropertyIn(Console::instance().properties(), "console", k, v,
                               err)) {
                out << err << "\n";
                failed_ = true;
            } else {
                out << "console: " << k << "=" << v << "\n";
            }
            return true;
        }

        // The window the picture lands in, settable the same way and for the same
        // reason: it is the host's, not a board's (host/display.h).
        if (is(a[1], "DISPLAY")) {
            std::string err;
            if (!setPropertyIn(Display::properties(), "display", k, v, err)) {
                out << err << "\n";
                failed_ = true;
            } else {
                out << "display: " << k << "=" << v << "\n";
            }
            return true;
        }

        // SET <channel> DEBUG=/NODEBUG=<flags> -- a diagnostic channel is NOT a board
        // property: a channel can be a library (`6850`, `socket`) that is no board at
        // all, and even a board's channel is the facility's, not the board's schema.
        // So it is handled here, ahead of the board path, for any registered channel.
        // (`SET CONSOLE DEBUG=` is the sink and was handled above; a unit target has no
        // channel, hence the no-colon guard.)
        if ((is(k, "DEBUG") || is(k, "NODEBUG")) && a[1].find(':') == std::string::npos) {
            if (dbg::Channel* c = dbg::find(a[1])) {
                const bool on = is(k, "DEBUG");
                std::string err;
                if (!(on ? c->enable(v, err) : c->disable(v, err))) {
                    out << err << "\n";
                    failed_ = true;
                } else {
                    out << c->name() << ": " << (on ? "debug" : "nodebug") << "=" << v << "\n";
                }
                return true;
            }
            // Not a channel -- fall through so the board path reports "no such board".
        }

        // SET <id>:<unit> <k>=<v> -- a unit is a real thing with real settings.
        if (a[1].find(':') != std::string::npos) {
            Board* b;
            UnitDef u;
            if (!subunit(a[1], b, u, UnitUse::Any, out)) return true;
            std::string err;
            if (!setUnitProperty(*b, u.name, k, v, err)) {
                out << err << "\n";
                failed_ = true;
            } else {
                out << b->id << ":" << u.name << ": " << k << "=" << v << "\n";
            }
            return true;
        }

        Board* b = board(a[1], out);
        if (!b) return true;
        std::string err;
        if (!setProperty(*b, k, v, err)) {
            out << err << "\n";
            failed_ = true;
        } else {
            out << b->id << ": " << k << "=" << v << "\n";
        }
        return true;
    }

    // ---------------- CONNECT / DISCONNECT ----------------
    //
    // GENERIC, not per-board (DESIGN.md 7.7). The monitor resolves the endpoint
    // string; the board is handed a ByteStream and never learns what a socket is.
    // A serial card written next year gets both of these for free.
    if (cmd == "CONNECT") {
        std::string usage = "CONNECT <id>:<unit> <endpoint>   -- " + endpointHelp();
        if (!need(3, usage.c_str())) return true;
        Board* b;
        UnitDef u;
        if (!subunit(a[1], b, u, UnitUse::Connect, out)) return true;
        if (u.kind != UnitKind::Serial) {
            out << b->id << ":" << u.name << " is a " << unitKindName(u.kind)
                << " unit -- there is nothing to connect to it. Use MOUNT.\n";
            failed_ = true;
            return true;
        }

        // EXACTLY ONE UNIT MAY HOLD THE CONSOLE (DESIGN.md 7.2, 9). Two boards
        // reading one keyboard would each get half the characters -- which is not
        // hypothetical, it is what happens the first time a machine has two 2SIOs
        // and you forget. So taking it says who you took it from.
        //
        // `is()` UPPERCASES ITS TOKEN AND COMPARES -- so the literal must be
        // uppercase or it can never match. This read `is(a[2], "console")` and was
        // therefore dead code, silently: two units held the console and neither
        // said so. Every other call site passes an uppercase keyword; this one
        // looked like an endpoint name, which is lowercase by convention, and that
        // is exactly how it slipped through.
        if (is(a[2], "CONSOLE")) {
            for (const auto& other : m_.boards()) {
                for (const auto& ou : other->units()) {
                    if (ou.kind != UnitKind::Serial || ou.state != "console") continue;
                    if (other.get() == b && ou.name == u.name) continue;
                    std::string err;
                    other->disconnect(ou.name, err);
                    out << "console taken from " << other->id << ":" << ou.name << "\n";
                }
            }
        }

        std::string err;
        if (!b->connect(u.name, a[2], err)) {
            out << err << "\n";
            failed_ = true;
        } else {
            out << b->id << ":" << u.name << ": connected to " << a[2] << "\n";
        }
        return true;
    }

    if (cmd == "DISCONNECT") {
        if (!need(2, "DISCONNECT <id>:<unit>")) return true;
        Board* b;
        UnitDef u;
        if (!subunit(a[1], b, u, UnitUse::Connect, out)) return true;
        std::string err;
        if (!b->disconnect(u.name, err)) {
            out << err << "\n";
            failed_ = true;
        } else {
            // Not an error state, and it must not look like one: an unconnected
            // 6850 sits there with TDRE set forever and software that writes to it
            // works fine and talks to nobody.
            out << b->id << ":" << u.name << ": disconnected (the line now goes nowhere)\n";
        }
        return true;
    }

    // ---------------- CONSOLE ----------------
    //
    // CONSOLE CONFIGURES THE CONSOLE. IT DOES NOT RUN THE MACHINE (Patrick,
    // 2026-07-12). It used to do both, and that was wrong twice over: a command
    // that starts the CPU because you asked to look at a setting is a trap, and
    // "start the machine" already has a name -- the switch on the panel says RUN.
    if (cmd == "CONSOLE") {
        if (a.size() < 2) {
            showConsole(out);
            return true;
        }
        for (size_t i = 1; i < a.size(); ++i) {
            size_t eq = a[i].find('=');
            if (eq == std::string::npos) {
                // The one mistake worth catching BY NAME: `CONSOLE F800` is what this
                // command did until today, and somebody's startup script still says
                // it. Silently rejecting it as a bad key would send them hunting.
                bool looksLikeAddress =
                    !a[i].empty() && a[i].find_first_not_of("0123456789abcdefABCDEF") ==
                                         std::string::npos;
                if (looksLikeAddress)
                    out << "CONSOLE configures the console; it does not start the machine.\n"
                           "   RUN "
                        << a[i] << "\n";
                else
                    out << "usage: CONSOLE [<key>=<value>...]   (CONSOLE alone shows it)\n";
                failed_ = true;
                return true;
            }
            std::string k = a[i].substr(0, eq), v = a[i].substr(eq + 1);
            std::string err;
            if (is(k, "DEBUG")) {  // the global sink, as in SET CONSOLE DEBUG= above
                if (!applyDebugSink(v, err)) {
                    out << err << "\n";
                    failed_ = true;
                    return true;
                }
                out << "debug: sink=" << dbg::sinkName() << "\n";
                continue;
            }
            if (!setPropertyIn(Console::instance().properties(), "console", k, v, err)) {
                out << err << "\n";
                failed_ = true;
                return true;
            }
            out << "console: " << k << "=" << v << "\n";
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
            // A port has two sides: IN and OUT decode independently, and a board may
            // claim one without the other (a status port that only reads, a strobe
            // that only writes). Probe both so WHO tells the whole truth.
            for (Cycle t : {Cycle::IoRead, Cycle::IoWrite}) {
                c = BusCycle{};
                c.type = t;
                c.addr = (uint16_t)(p & 0xFF);
                auto who = m_.bus.respondersTo(c);
                std::snprintf(buf, sizeof buf, "port %s %s", fmtByte((uint8_t)(p & 0xFF)).c_str(),
                              t == Cycle::IoRead ? "IN: " : "OUT:");
                out << buf;
                if (who.empty())
                    out << (t == Cycle::IoRead ? " nobody (an IN here reads FF)"
                                               : " nobody (an OUT here goes nowhere)");
                for (auto* b : who) out << " " << b->id;
                out << "\n";
            }
            return true;
        }
        uint32_t A;
        if (!addrSym(a[1], A, out)) return true;

        for (Cycle t : {Cycle::MemRead, Cycle::MemWrite}) {
            c = BusCycle{};
            c.type = t;
            c.addr = (uint16_t)A;
            bool ph = false;
            for (const auto& b : m_.boards())
                if (b->enabled() && b->assertsPhantom(c)) ph = true;
            auto who = m_.bus.respondersTo(c);

            std::snprintf(buf, sizeof buf, "%s %-5s ", fmtWord((uint16_t)A).c_str(),
                          t == Cycle::MemRead ? "read" : "write");
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
        if (!need(3,
                  "MOUNT <id>:<unit> <file> [WP] [CREATE] [extract[=<base>]] [key=value ...]"))
            return true;
        Board* b;
        UnitDef u;
        if (!subunit(a[1], b, u, UnitUse::Mount, out)) return true;

        // WP IS THE WRITE-PROTECT, and until now it was documented in HELP and
        // silently thrown away here (`readOnly = false`, hardcoded). That is fine
        // for a ROM, which cannot be written anyway -- and it is a disk you are
        // about to let CP/M loose on.
        //
        // BOTH SPELLINGS, and neither is a legacy alias to be regretted: WP is a property
        // of the MEDIUM, which is what this does to a floppy or a tape; RO is what the
        // file becomes, which is what it does to a ROM socket. They are one flag because
        // the card can only do one thing about them, and an operator who reaches for the
        // other word is not making a mistake worth an error message.
        //
        // Anything else in the slot IS a typo, and a typo that we accepted would mount
        // the disk READ/WRITE while the operator believed they had protected it. Refuse.
        bool                                             readOnly    = false;
        bool                                             extract     = false;
        bool                                             create      = false;
        std::string                                      extractBase;  // "" -> beside the WAV
        std::vector<std::pair<std::string, std::string>> opts;  // key=value, applied post-mount
        for (size_t i = 3; i < a.size(); ++i) {
            if (is(a[i], "WP") || is(a[i], "RO")) {
                readOnly = true;
                continue;
            }
            // CREATE makes the file first if it is not there, then mounts it -- so a fresh
            // hard-sector disk (mount empty, then FORMAT) or a blank cassette starts entirely
            // inside the simulator. It only ensures the file EXISTS; the board mounts it as
            // normal. Without CREATE a missing file is still a "no such file" (a typo is a typo).
            if (is(a[i], "CREATE")) {
                create = true;
                continue;
            }
            // EXTRACT is not a property -- it writes files after the mount (below), so it is
            // handled here rather than routed to setUnitProperty. `extract` uses the default
            // name; `extract=<base>` names the files.
            if (is(a[i], "EXTRACT")) {
                extract = true;
                continue;
            }
            // ANY key=value is a unit property set, applied once the tape is in (below), so
            // `counter=off`/`stop=2:05` need no per-key code here and a new unit property
            // works at MOUNT the day it exists. WP/RO stay bare flags -- the read-only fence
            // is the board's own argument to mount(), not a property.
            size_t eq = a[i].find('=');
            if (eq != std::string::npos && eq > 0) {
                std::string key = a[i].substr(0, eq), val = a[i].substr(eq + 1);
                if (upper(key) == "EXTRACT") {
                    extract     = true;
                    extractBase = val;
                    continue;
                }
                opts.emplace_back(std::move(key), std::move(val));
                continue;
            }
            out << "MOUNT: '" << a[i] << "': options are WP (RO), CREATE, extract[=<base>], or "
                << "key=value (e.g. counter=off, stop=2:05). usage: MOUNT <id>:<unit> <file> "
                << "[WP] [CREATE] [extract[=<base>]] [key=value ...]\n";
            failed_ = true;
            return true;
        }

        // WHAT WE MOUNT. Normally the file as typed; for an ImageDisk (`.IMD`) it is the raw
        // sibling `.DSK` the block below writes, so everything downstream -- mount(), the
        // narration, CONFIG SAVE -- sees a plain raw image and never the container.
        std::string mountPath = unquote(a[2]);

        // ---- IMD -> raw sibling .DSK -----------------------------------------------------
        //
        // An ImageDisk is a CONTAINER (a per-track sector map, compression, per-sector types)
        // that DiskImage deliberately does not read (host/disk.h). So it is converted to raw
        // HERE, above the image: slurp the `.IMD`, convert, write `foo.dsk` beside it, and
        // mount THAT. The target board `b` is already resolved (subunit, above), so the
        // converter asks IT which head order a double-sided disk needs (host/imd.h) -- the
        // controller is the authority, not a guess.
        {
            std::string up = upper(mountPath);
            if (up.size() >= 4 && up.compare(up.size() - 4, 4, ".IMD") == 0) {
                std::string sibling = mountPath.substr(0, mountPath.size() - 4) + ".dsk";
                std::string imdPath = b->resolvePath(mountPath);
                std::string dskPath = b->resolvePath(sibling);

                // Never clobber a `.dsk` already there -- the same ethic as CREATE. If they
                // want it rebuilt they remove it first; if they want the existing one they
                // mount it directly.
                std::error_code ec;
                if (std::filesystem::exists(dskPath, ec)) {
                    out << b->id << ": " << resolveFrom(startupDir_, sibling)
                        << " already exists; remove it or MOUNT it directly\n";
                    failed_ = true;
                    return true;
                }

                std::string ierr;
                auto media = openMedia(imdPath, /*readOnly=*/true, ierr);
                if (!media) {
                    out << b->id << ": " << ierr << b->pathNote(mountPath) << "\n";
                    failed_ = true;
                    return true;
                }
                std::vector<uint8_t> imd((size_t)media->size());
                if (!imd.empty() && !media->readAt(0, imd.data(), imd.size())) {
                    out << b->id << ": " << resolveFrom(startupDir_, mountPath) << ": read error\n";
                    failed_ = true;
                    return true;
                }

                ImdInfo              info;
                std::vector<uint8_t> rawimg;
                if (!convertImdToRaw(imd, rawimg, info, ierr,
                                     [&](uint64_t bytes) { return b->disksInterleaved(bytes); })) {
                    out << b->id << ": " << resolveFrom(startupDir_, mountPath) << ": " << ierr
                        << "\n";
                    failed_ = true;
                    return true;
                }
                if (!writeHostFile(dskPath, rawimg, ierr)) {
                    out << b->id << ": " << ierr << "\n";
                    failed_ = true;
                    return true;
                }

                // Narrate how the raw file was built, before the mount line: what it came from,
                // the IMD's own description, the emitted geometry, and the total. This is the
                // "how the .dsk was created" report -- the operator can sanity-check it against
                // the disk they expect.
                out << b->id << ": converted " << resolveFrom(startupDir_, mountPath) << " -> "
                    << resolveFrom(startupDir_, sibling) << "\n";
                if (!info.description.empty())
                    out << b->id << ":   IMD: " << info.description << "\n";
                for (const auto& tl : info.tracks)
                    out << b->id << ":   " << tl << "\n";
                out << b->id << ":   " << info.rawBytes << " bytes";
                if (info.heads > 1)
                    out << (info.interleaved ? ", heads interleaved" : ", heads sequential");
                out << "\n";

                mountPath = sibling;  // mount and record the raw .dsk from here on
            }
        }

        // CREATE, before the mount: make a zero-length file at the SAME place the board will
        // open it (resolvePath -- identity for a typed path, config-relative inside a machine
        // file), so mount() then finds it. Never clobber a file that is already there.
        bool created = false;
        if (create) {
            std::string rp = b->resolvePath(mountPath);
            std::error_code ec;
            if (!std::filesystem::exists(rp, ec)) {
                std::string cerr;
                if (!writeHostFile(rp, {}, cerr)) {
                    out << b->id << ": " << cerr << "\n";
                    failed_ = true;
                    return true;
                }
                created = true;
            }
        }

        std::string err;
        if (!b->mount(u.name, mountPath, readOnly, err)) {
            // A CREATE whose mount is then refused must NOT leave its zero-byte file behind:
            // the operator asked to mount a disk, not to litter one. Unlink exactly the file
            // WE made this call (`created`), never one that was already on disk. It also keeps
            // a retry honest -- without it the second attempt measures the empty file the first
            // left and fails identically, so the refusal describes a turd of our own making
            // rather than the real problem. (Same shape as the SHOW PATHS empty-vs-absent bug:
            // a file we made and a file the operator already had are different answers.)
            if (created) {
                std::error_code rmec;
                std::filesystem::remove(b->resolvePath(mountPath), rmec);
            }
            out << b->id << ": " << err << "\n";
            // A MISSING file is the one mount failure the operator can fix from here: add
            // CREATE and we make a blank one and mount it (a hard-sector disk then FORMATs,
            // a cassette records onto it). Only say so when the file is actually absent and
            // they did not already ask -- a geometry or permission error is a different
            // problem and CREATE would not touch it.
            if (!create) {
                std::error_code ec;
                if (!std::filesystem::exists(b->resolvePath(mountPath), ec))
                    out << b->id << ": to make a blank one, add CREATE: MOUNT " << a[1] << " "
                        << a[2] << " CREATE\n";
            }
            failed_ = true;
        } else {
            if (created)
                out << b->id << ":" << u.name << ": created "
                    << resolveFrom(startupDir_, mountPath) << " (empty)\n";
            // The trailing key=value options are applied as unit properties now the tape is
            // in -- so `counter=off`/`stop=2:05` at MOUNT and SET later are the one mechanism.
            // A unit with no such property (a disk, a ROM) says so rather than ignoring it.
            for (const auto& kv : opts) {
                std::string perr;
                if (!setUnitProperty(*b, u.name, kv.first, kv.second, perr)) {
                    out << b->id << ":" << u.name << ": " << perr << "\n";
                    failed_ = true;
                }
            }
            // SAY WHERE THE DISK ACTUALLY IS, not what the file called it. The board
            // stores the name as written -- SHOW and CONFIG SAVE need that -- but the
            // narration is a report of what just HAPPENED, and what happened is that we
            // opened a particular file. A `startup` line that says PS2-MON.TAP printing
            // `mounted PS2-MON.TAP` beside a LOAD printing the full path would have the
            // reader wondering which of the two directories they were actually in.
            //
            // The rule, everywhere: NARRATION SAYS WHERE. CONFIGURATION SAYS WHAT YOU WROTE.
            out << b->id << ":" << u.name << ": mounted "
                << resolveFrom(startupDir_, mountPath)
                << (readOnly ? std::string(" (") + protectedWord(u.kind) + ")" : "") << "\n";

            // ...and, if asked, split the just-mounted WAV into per-program .TAP files. This
            // runs the board's own EXTRACT verb, so the mount option and the verb are one
            // code path; the board narrates each file it wrote to `out`.
            if (extract) {
                std::vector<std::string> ea{"EXTRACT", b->id + ":" + u.name};
                if (!extractBase.empty()) ea.push_back(extractBase);
                std::string eerr;
                if (!b->runCommand("EXTRACT", ea, out, eerr)) {
                    out << b->id << ":" << u.name << ": " << eerr << "\n";
                    failed_ = true;
                }
            }
        }
        // ...AND SAY WHAT THE BOARD SAID, HERE, WHERE IT HAPPENED.
        //
        // Without this the board's own warnings -- "mounted READ-ONLY, the host will not
        // let us write it" (mits-hardsector.cpp), and pathNote()'s "this re-based against
        // the machine file" -- sat in the log until some LATER command flushed, and then
        // printed under that one. The forced-RO warning is the exact thing media.h swears
        // is never silent, and it was arriving three commands late, attached to an EXAMINE.
        flush(out);
        return true;
    }
    if (cmd == "UNMOUNT") {
        if (!need(2, "UNMOUNT <id>:<unit>")) return true;
        Board* b;
        UnitDef u;
        if (!subunit(a[1], b, u, UnitUse::Mount, out)) return true;
        std::string err;
        if (!b->unmount(u.name, err)) {
            out << b->id << ": " << err << "\n";
            failed_ = true;
        } else {
            out << b->id << ":" << u.name
                << ": unmounted (the socket is now EMPTY -- those pages float to FF)\n";
        }
        flush(out);  // ...and a sync-on-eject that complained must say so HERE. See MOUNT.
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
            if (!addrSym(a[1], lo, out)) return true;
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
                    // outside the range: hold the column (byte + its space), show nothing
                    hexs += std::string(byteWidth() + 1, ' ');
                    asc += ' ';
                } else {
                    uint8_t v = rd(E);
                    hexs += fmtByte(v);
                    hexs += ' ';
                    asc += (v >= 0x20 && v < 0x7F) ? (char)v : '.';
                }
                // The traditional mid-line gutter. It is OUTSIDE the branch above:
                // a blank column still has to push it, or a padded line's gutter
                // lands in the wrong place and the alignment we just bought is lost.
                if (w == 16 && k == 7) hexs += ' ';
            }
            std::snprintf(buf, sizeof buf, "%s  %s %s", fmtWord((uint16_t)A).c_str(), hexs.c_str(),
                          asc.c_str());
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
    //
    // EXAMINE also LOADS THE PC. On the panel that is not a side effect, it is what
    // the switch is for: EXAMINE jams the address switches into the program counter
    // and then reads the byte the CPU is now pointing at. `EX F800` followed by RUN
    // is how you start a ROM, and it is why CONSOLE <addr> above is EXAMINE + RUN.
    // So `EX FF00` then STEP executes at FF00, and EXAMINE NEXT drags the PC along
    // with it -- the panel's counter is the only cursor it has.
    if (cmd == "EXAMINE") {
        // EXAMINE *IS* THE CPU (Patrick, 2026-07-12). The panel has no address
        // latch of its own: it stops the processor, jams the switches into the
        // PROGRAM COUNTER, and the CPU drives the address lines and MEMR. So
        //
        //   - EXAMINE with no CPU card is not a thing that can happen. Nothing is
        //     driving the bus. It is an error, not a degraded mode. (Look at a
        //     CPU-less machine's memory with DUMP, which runs no cycle and needs
        //     no processor to drive one.)
        //   - THE PC IS THE CURSOR. Not a copy of it -- the thing itself. Two
        //     counters, one writing to the other, is a split brain: EXAMINE NEXT
        //     would step a private latch while the PC sat somewhere else, and then
        //     quietly drag the PC BACKWARDS to it.
        //
        // There is no longer an exception. `EXAMINE RAW <id>` used to reach behind the
        // bus with no CPU and its own cursor; reading behind the bus is gone (a ROM
        // answers reads like anything else -- §10.2), and with it the second cursor.
        CpuCore* pcOwner = needCpu(out);
        if (!pcOwner) return true;

        uint32_t A;
        if (a.size() < 2) {
            // EXAMINE NEXT: the panel steps the counter and shows what is there.
            A = (uint32_t)((pcOwner->pc() + 1) & 0xFFFF);
        } else if (!addrSym(a[1], A, out)) {
            return true;
        }
        if (A > 0xFFFF) {
            out << "address is 16 bits: 0000-FFFF\n";
            failed_ = true;
            return true;
        }
        pcOwner->setPc((uint16_t)A);

        uint8_t v = rd(A);
        std::snprintf(buf, sizeof buf, "%s  %s  %c  %c%c%c%c%c%c%c%c", fmtWord((uint16_t)A).c_str(),
                      fmtByte(v).c_str(),
                      (v >= 0x20 && v < 0x7F) ? (char)v : '.', (v & 0x80) ? '1' : '0',
                      (v & 0x40) ? '1' : '0', (v & 0x20) ? '1' : '0', (v & 0x10) ? '1' : '0',
                      (v & 0x08) ? '1' : '0', (v & 0x04) ? '1' : '0', (v & 0x02) ? '1' : '0',
                      (v & 0x01) ? '1' : '0');
        out << buf;
        // Looking at ONE byte is exactly when you need to know whether it is a byte
        // at all. FF from a chip and FF from an empty slot read the same.
        if (m_.bus.lastUnclaimed()) out << "   (nobody drives this -- the bus floated it)";
        out << "\n";
        flush(out);
        return true;
    }

    if (cmd == "DEPOSIT") {
        if (!need(3, "DEPOSIT <addr> <byte...>")) return true;
        uint32_t A;
        if (!addrSym(a[1], A, out)) return true;
        for (size_t i = 2; i < a.size(); ++i) {
            uint32_t v;
            if (!addr(a[i], v, out)) return true;
            if (romOverride) {
                std::string why;
                if (!burn(A, (uint8_t)v, why)) {
                    std::snprintf(buf, sizeof buf, "%s: %s", fmtWord((uint16_t)A).c_str(), why.c_str());
                    out << buf << "\n";
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
                    std::snprintf(buf, sizeof buf, "%s: no board decodes writes here", fmtWord((uint16_t)A).c_str());
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

    if (cmd == "EDIT") {
        // INTERACTIVE DEPOSIT (DESIGN.md 10). Show the address and the byte there, take
        // a new one, drop to the next. It writes through the REAL bus exactly like
        // DEPOSIT -- and burns a ROM the same way -- so it needs no CPU and, unlike
        // EXAMINE, does NOT move the PC: it is not the front panel. Its cursor is a
        // local that is gone the moment you type '.'.
        if (!need(2, "EDIT <addr> [ROM]")) return true;
        uint32_t A;
        if (!addrSym(a[1], A, out)) return true;
        if (A > 0xFFFF) {
            out << "address is 16 bits: 0000-FFFF\n";
            failed_ = true;
            return true;
        }
        // No keyboard to take the new bytes from -- an MCP `command`, a `startup` list,
        // any exec() that is not inside a REPL. Loop on nothing and it would spin, so say
        // so: DEPOSIT is the non-interactive way to write memory.
        if (!in_ || !ed_) {
            out << "EDIT needs an interactive or piped session -- use DEPOSIT here.\n";
            return true;
        }
        // If this machine has a CPU we can assemble for, the operator may type a
        // mnemonic where a byte would go and have the encoding land in place. Null
        // when there is no CPU (m_.isa() == "") or the ISA has no assembler yet
        // (Z80): EDIT stays byte-only, exactly as before.
        const Assembler* asm_ = assemblerFor(m_.isa());
        // Write one byte through the SAME path DEPOSIT uses -- burn a ROM when
        // romOverride, else the bus, reporting a discarded byte the same way.
        // Returns false only on a FATAL ROM-burn failure so the caller stops; a
        // discarded RAM byte is reported but not fatal. Assembling an instruction
        // reuses this per byte, so a multi-byte encoding lands like N deposits.
        auto deposit1 = [&](uint32_t at, uint8_t byte) -> bool {
            if (romOverride) {
                std::string why;
                if (!burn(at, byte, why)) {
                    std::snprintf(buf, sizeof buf, "%s: %s", fmtWord((uint16_t)at).c_str(), why.c_str());
                    out << buf << "\n";
                    failed_ = true;
                    return false;  // a ROM you cannot burn will not burn on the next byte either
                }
            } else {
                m_.bus.memWrite((uint16_t)at, byte);
                // Same silence-is-a-bug rule DEPOSIT keeps: if nobody latched the
                // byte, SAY SO rather than let it vanish into a gap in the map.
                if (m_.bus.lastUnclaimed()) {
                    BusCycle bc;
                    bc.type = Cycle::MemRead;
                    bc.addr = (uint16_t)at;
                    auto rdr = m_.bus.respondersTo(bc);
                    std::snprintf(buf, sizeof buf, "%s: no board decodes writes here", fmtWord((uint16_t)at).c_str());
                    out << buf;
                    if (!rdr.empty())
                        out << " (" << rdr.front()->id << " answers reads -- it is ROM)";
                    out << ". byte discarded.\n";
                }
            }
            return true;
        };
        for (;;) {
            uint8_t v = rd(A);
            std::snprintf(buf, sizeof buf, "%s %s ", fmtWord((uint16_t)A).c_str(), fmtByte(v).c_str());
            std::string resp;
            if (!ed_->read(buf, resp, *in_)) break;  // EOF / Ctrl-D -- done
            size_t b0 = resp.find_first_not_of(" \t");
            std::string tok;
            if (b0 != std::string::npos) tok = resp.substr(b0, resp.find_last_not_of(" \t") - b0 + 1);
            if (tok == ".") break;                                   // done
            if (tok.empty()) { A = (A + 1) & 0xFFFF; continue; }     // leave it, next byte

            // BYTE FIRST: try the whole line as a plain byte exactly as before, so
            // every input that used to deposit a byte still does -- a bare CC is the
            // byte 0xCC, not CALL-carry (which always carries an address, so it has a
            // space and fails this and reaches the assembler below).
            uint32_t nv;
            std::string e;
            if (parseNum(tok, nv, octalMode() ? 8 : 16, e) && nv <= 0xFF) {
                if (!deposit1(A, (uint8_t)nv)) break;
                A = (A + 1) & 0xFFFF;
                continue;
            }

            // Not a byte. With no assembler for this ISA, that is the old bad-byte
            // error, unchanged. With one, hand it the line and let the encoding fall
            // out -- `IN 10` writes DB 10 and the prompt drops two bytes.
            if (!asm_) {
                out << (octalMode() ? "?  a byte is 000-377, or '.' to stop\n"
                                    : "?  a byte is 00-FF, or '.' to stop\n");  // stay put, re-prompt
                continue;
            }
            AsmResult r = asm_->assemble((uint16_t)A, tok, octalMode() ? 8 : 16);
            if (!r.error.empty()) {
                out << "?  " << r.error << ", or '.' to stop\n";  // stay put, re-prompt
                continue;
            }
            bool fatal = false;
            for (uint8_t byte : r.bytes) {
                if (!deposit1(A, byte)) { fatal = true; break; }
                A = (A + 1) & 0xFFFF;
            }
            if (fatal) break;
        }
        out << ".\n";
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
        std::snprintf(buf, sizeof buf, "port %s -> %s", fmtByte((uint8_t)p).c_str(), fmtByte(v).c_str());
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
        std::snprintf(buf, sizeof buf, "port %s <- %s", fmtByte((uint8_t)p).c_str(),
                      fmtByte((uint8_t)(v & 0xFF)).c_str());
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
            if (romOverride) {
                std::string why;
                if (!burn(A, (uint8_t)v, why)) {
                    std::snprintf(buf, sizeof buf, "%s: %s", fmtWord((uint16_t)A).c_str(), why.c_str());
                    out << buf << "\n";
                    failed_ = true;
                    return true;
                }
            } else {
                m_.bus.memWrite((uint16_t)A, (uint8_t)v);
            }
        }
        std::snprintf(buf, sizeof buf, "filled %s-%s with %s", fmtWord((uint16_t)lo).c_str(),
                      fmtWord((uint16_t)hi).c_str(), fmtByte(v).c_str());
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
                std::snprintf(buf, sizeof buf, "%s", fmtWord((uint16_t)A).c_str());
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
        if (!range(a[1], lo, hi, out) || !addrSym(a[2], dst, out)) return true;
        int diff = 0;
        for (uint32_t A = lo; A <= hi; ++A) {
            uint8_t x = rd(A), y = rd(dst + (A - lo));
            if (x != y) {
                std::snprintf(buf, sizeof buf, "%s %s != %s %s", fmtWord((uint16_t)A).c_str(),
                              fmtByte(x).c_str(), fmtWord((uint16_t)(dst + (A - lo))).c_str(),
                              fmtByte(y).c_str());
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
        if (!range(a[1], lo, hi, out) || !addrSym(a[2], dst, out)) return true;
        std::vector<uint8_t> tmp;
        for (uint32_t A = lo; A <= hi; ++A) tmp.push_back(rd(A));
        for (size_t k = 0; k < tmp.size(); ++k) {
            if (romOverride) {
                std::string why;
                if (!burn(dst + (uint32_t)k, tmp[k], why)) {
                    std::snprintf(buf, sizeof buf, "%s: %s", fmtWord((uint16_t)(dst + k)).c_str(), why.c_str());
                    out << buf << "\n";
                    failed_ = true;
                    return true;
                }
            } else {
                m_.bus.memWrite((uint16_t)(dst + k), tmp[k]);
            }
        }
        out << tmp.size() << " bytes moved\n";
        flush(out);
        return true;
    }

    if (cmd == "LOAD") {
        if (!need(2, "LOAD <file> [AT <addr>] [FORMAT=BIN|HEX] [ROM]")) return true;
        a[1] = unquote(a[1]);  // and every message below now names the file, not the quote

        // LOAD is MOUNT's other half, and it keeps MOUNT's bargain: a `startup` line in
        // a machine file that says LOAD "LDRPS2.HEX" means the bootstrap lying beside
        // that file (core/paths.h). At the prompt startupDir_ is "" and this is the
        // identity function, so what you type is what the shell would have opened.
        a[1] = resolveFrom(startupDir_, a[1]);
        uint32_t at = 0;
        bool haveAt = false;
        for (size_t i = 2; i + 1 < a.size(); ++i)
            if (is(a[i], "AT")) {
                if (!addrSym(a[i + 1], at, out)) return true;
                haveAt = true;
            }

        // FORMAT=BIN|HEX. The file's CONTENTS decide by default and that is nearly
        // always right -- Intel HEX announces itself with a colon and hex digits, which
        // is a thing a flat binary essentially never opens with. FORMAT= is the override
        // for when it IS wrong, and it always wins.
        //
        // This was advertised in the help for a long time and parsed NOWHERE: you could
        // type FORMAT=HEX and it was dropped on the floor without a word. The command
        // reference promised it, so the reference was a lie -- fixed by making the code
        // tell the truth rather than by quietly deleting the promise.
        int forced = -1;  // -1 autodetect, 0 BIN, 1 HEX
        for (size_t i = 2; i < a.size(); ++i) {
            if (upper(a[i]).rfind("FORMAT=", 0) != 0) continue;
            std::string want = upper(a[i]).substr(7);
            if (want == "HEX") forced = 1;
            else if (want == "BIN") forced = 0;
            else {
                out << "FORMAT=" << want << "? It is BIN or HEX.\n";
                failed_ = true;
                return true;
            }
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
        if (forced == 1 || (forced < 0 && looksLikeHex(data))) {
            if (!loadHex(data, img, err)) {
                out << a[1] << ": " << err << "\n";  // names the record. loudly.
                failed_ = true;
                return true;
            }
            // AT on a file that carries its own addresses moves the image so its FIRST
            // DATA RECORD lands there -- so AT means what it means for a flat binary
            // too: PUT IT HERE (Patrick, 2026-07-17). It used to ADD `at` to every
            // embedded address, which made one word mean "shift by" for HEX and "put at"
            // for BIN, and the difference only showed on a file that did not start at
            // zero. The arithmetic (anchor, and the modulo-64K wrap) is hex.h's.
            if (haveAt) relocateTo(img, at);
        } else {
            if (!haveAt) {
                out << a[1] << " is a flat binary and carries no addresses -- it needs AT <addr>\n";
                failed_ = true;
                return true;
            }
            loadBin(data, at, img);
        }

        size_t gone = 0;
        std::string why;
        for (const auto& [A, v] : img.bytes) {
            if (romOverride) {
                // THE PROM BURNER. Behind the bus, into whichever chip answers here.
                if (!burn(A, v, why)) {
                    std::snprintf(buf, sizeof buf, "%s: %s", fmtWord((uint16_t)A).c_str(), why.c_str());
                    out << buf << "\n";
                    failed_ = true;
                    return true;
                }
            } else {
                m_.bus.memWrite((uint16_t)A, v);
                if (m_.bus.lastUnclaimed()) ++gone;
            }
        }
        std::snprintf(buf, sizeof buf, "loaded %zu bytes from %s (%s-%s)%s", img.size(),
                      a[1].c_str(), fmtWord(img.lo()).c_str(), fmtWord(img.hi()).c_str(),
                      romOverride ? " (ROM override)" : "");
        out << buf << "\n";
        if (gone) {
            // Loading through the bus is a bus write, so ROM does not take it -- exactly
            // as a real machine would not. Say so; do not quietly half-load.
            std::snprintf(buf, sizeof buf,
                          "  WARNING: %zu byte(s) landed nowhere (ROM, or unmapped). "
                          "To program a ROM: LOAD %s ROM",
                          gone, a[1].c_str());
            out << buf << "\n";
        }
        if (img.hasStart) {
            std::snprintf(buf, sizeof buf, "  start address %s (no CPU yet)", fmtWord(img.start).c_str());
            out << buf << "\n";
        }
        flush(out);
        return true;
    }

    if (cmd == "SAVE") {
        if (!need(3, "SAVE <file> <range> [FORMAT=BIN|HEX|OCTAL]")) return true;
        a[1] = unquote(a[1]);
        a[1] = resolveFrom(startupDir_, a[1]);  // ...the same rule as LOAD, in reverse
        uint32_t lo, hi;
        if (!range(a[2], lo, hi, out)) return true;
        Image img;
        for (uint32_t A = lo; A <= hi; ++A) img.bytes[A] = rd(A);

        // THE NAME DECIDES, AND FORMAT= OVERRIDES IT -- SAVE goes by the filename unless
        // told otherwise (Patrick, 2026-07-17).
        //
        // Which is the other half of LOAD's rule, and deliberately not the same
        // mechanism: LOAD can read the file and see what it IS, and SAVE cannot -- the
        // file does not exist yet. So SAVE has only the name to go on, and a name is a
        // guess. FORMAT= is how you say it outright when the guess would be wrong.
        //
        // OCTAL is a THIRD, WRITE-ONLY format: an octal listing for reading, eyeballing
        // against a MITS manual, or pasting somewhere -- LOAD does not read it back
        // (there is no octal load path, deliberately). BIN and HEX round-trip; OCTAL
        // does not, and the confirmation says so by naming the format it wrote.
        //
        // PRN is a FOURTH, likewise write-only: the DISASM listing -- address, object
        // bytes, mnemonic and any labels you have SYMBOLS-loaded -- written to a file
        // instead of the screen, for reading and marking up when reverse-engineering
        // code (issue #176). It is a listing, not source: it does not re-assemble, and
        // like OCTAL there is no load path back.
        enum class Fmt { Bin, Hex, Oct, Prn } fmt = Fmt::Bin;
        std::string uname = upper(a[1]);
        auto endsWith = [&](std::string ext) {
            return uname.size() > ext.size() && uname.rfind(ext) == uname.size() - ext.size();
        };
        if (endsWith(".HEX")) fmt = Fmt::Hex;
        else if (endsWith(".OCT")) fmt = Fmt::Oct;
        else if (endsWith(".PRN") || endsWith(".LST")) fmt = Fmt::Prn;
        for (size_t i = 3; i < a.size(); ++i) {
            if (upper(a[i]).rfind("FORMAT=", 0) != 0) continue;
            std::string want = upper(a[i]).substr(7);
            if (want == "HEX") fmt = Fmt::Hex;
            else if (want == "BIN") fmt = Fmt::Bin;
            else if (want == "OCTAL" || want == "OCT") fmt = Fmt::Oct;
            else if (want == "PRN" || want == "LST" || want == "LISTING") fmt = Fmt::Prn;
            else {
                out << "FORMAT=" << want << "? It is BIN, HEX, OCTAL or PRN.\n";
                failed_ = true;
                return true;
            }
        }

        // PRN needs to decode, so it needs an instruction set -- resolved the same way
        // DISASM resolves it (the active core's ISA). No CPU, no decoder, no listing:
        // say so before we truncate a file we cannot fill.
        const Disassembler* prnDis = nullptr;
        if (fmt == Fmt::Prn) {
            std::string want = m_.isa();
            if (!want.empty()) prnDis = disassemblerFor(want);
            if (!prnDis) {
                out << "no CPU in this machine, so I cannot disassemble a .PRN listing.\n"
                       "Use HEX or OCTAL for a data view, or add a CPU.\n";
                failed_ = true;
                return true;
            }
        }

        std::ofstream f(a[1], std::ios::binary);
        if (!f) {
            out << "cannot write '" << a[1] << "'\n";
            failed_ = true;
            return true;
        }
        const char* fmtName = "bin";
        if (fmt == Fmt::Hex) {
            f << saveHex(img);
            fmtName = "hex";
        } else if (fmt == Fmt::Oct) {
            // Always octal, whatever base the console prints in: a .OCT file is an
            // octal artifact, not a view of one session. Split-octal addresses (hi
            // byte, lo byte -- the front-panel convention), octal bytes 000-377, eight
            // to a line so a line that starts on an /8 boundary opens on a round octal
            // address. The header range is fixed hex so the file is reproducible.
            char ob[48];
            std::snprintf(ob, sizeof ob, "; altairsim octal image  %04X-%04X\n",
                          (unsigned)lo, (unsigned)hi);
            f << ob;
            for (uint32_t A = lo; A <= hi;) {
                std::snprintf(ob, sizeof ob, "%03o %03o ", (unsigned)((A >> 8) & 0xFF),
                              (unsigned)(A & 0xFF));
                f << ob;
                for (int k = 0; k < 8 && A <= hi; ++k, ++A) {
                    std::snprintf(ob, sizeof ob, " %03o", (unsigned)rd(A));
                    f << ob;
                }
                f << "\n";
            }
            fmtName = "octal";
        } else if (fmt == Fmt::Prn) {
            // The DISASM listing, to a file instead of the screen. disasmLine writes
            // exactly what DISASM prints -- address, object bytes, mnemonic, and any
            // SYMBOLS labels heading their own line -- and follows the console base,
            // so a session in octal writes an octal listing. We disassemble forward
            // from lo, one instruction at a time, until we reach hi; the last
            // instruction may read a byte or two past hi (its own operand), exactly
            // as DISASM does on screen.
            char hb[48];
            std::snprintf(hb, sizeof hb, "; altairsim disassembly  %04X-%04X\n",
                          (unsigned)lo, (unsigned)hi);
            f << hb;
            for (uint32_t at = lo; at <= hi;) {
                at += disasmLine(at, *prnDis, f);
                if (at > 0xFFFF) break;  // ran off the top of memory; do not wrap silently
            }
            fmtName = "listing";
        } else {
            for (auto v : img.flat()) f.put((char)v);
        }
        std::snprintf(buf, sizeof buf, "saved %s-%s to %s (%s)", fmtWord((uint16_t)lo).c_str(),
                      fmtWord((uint16_t)hi).c_str(), a[1].c_str(), fmtName);
        out << buf << "\n";
        return true;
    }

    // ---------------- THE CPU ----------------

    if (cmd == "REGS") {
        if (!needCpu(out)) return true;
        showRegs(out);
        return true;
    }

    // DISASM. It needs an instruction set, NOT a CPU -- which is why it worked in
    // milestone 1a against the DBL PROM with an empty backplane, and why the 8080
    // decode tables were exercised long before anything executed them.
    //
    // You do not normally name the CPU, and must not have to: the active core says
    // which instruction set it speaks and DISASM asks it (DESIGN.md 3.0.2).
    if (cmd == "DISASM") {
        std::string want;
        for (size_t i = 1; i < a.size(); ++i) {
            if (upper(a[i]).rfind("CPU=", 0) == 0) {
                want = a[i].substr(4);
                a.erase(a.begin() + (long)i);
                break;
            }
        }
        if (want.empty()) want = m_.isa();
        if (want.empty()) {
            out << "no CPU in this machine, so I do not know how to decode these bytes.\n"
                   "Say which: DISASM " << (a.size() > 1 ? a[1] : "<addr>") << " CPU=8080\n";
            failed_ = true;
            return true;
        }
        const Disassembler* d = disassemblerFor(want);
        if (!d) {
            out << "no instruction set '" << want << "'. Known:";
            for (const auto& s : instructionSets()) out << " " << s;
            out << "\n";
            failed_ = true;
            return true;
        }

        uint32_t lo = disasmNext_, hi = 0;
        uint32_t n = 16;  // a screenful, and a count -- so it is DECIMAL
        bool haveRange = false;
        if (a.size() >= 2) {
            if (a[1].find('-') != std::string::npos || a[1].find('/') != std::string::npos) {
                if (!range(a[1], lo, hi, out)) return true;
                haveRange = true;
            } else if (!addrSym(a[1], lo, out)) {
                return true;
            }
        }
        if (a.size() >= 3 && !haveRange && !count(a[2], n, out)) return true;

        uint32_t at = lo;
        for (uint32_t i = 0; haveRange ? at <= hi : i < n; ++i) {
            at += disasmLine(at, *d, out);
            if (at > 0xFFFF) break;  // ran off the top of memory; do not wrap silently
        }
        disasmNext_ = at & 0xFFFF;
        return true;
    }

    // STEP -- one instruction, with real bus cycles, through the real decode.
    if (cmd == "STEP") {
        CpuCore* c = needCpu(out);
        if (!c) return true;

        uint32_t n = 1;
        if (a.size() >= 2 && !count(a[1], n, out)) return true;  // a count: DECIMAL

        SigintGuard guard;

        // Printing every instruction is what STEP is FOR -- watching them go by is
        // the point. Past a screenful or two that stops being a trace and starts
        // being a flood, so we run quietly and report. The cutoff is arbitrary; the
        // behaviour is not, and it is stated rather than discovered.
        const uint32_t kEcho = 32;
        bool echo = n <= kEcho;

        RunResult total;
        for (uint32_t i = 0; i < n; ++i) {
            // One line per instruction, printed AFTER it runs: the machine as it now
            // stands, with the instruction the PC has reached next. This is the same
            // register-and-mnemonic line HISTORY records. STEP used to print a line
            // BEFORE each instruction and one more after the last, so a single `S`
            // showed two lines -- and every step repeated the previous PC, reading as
            // if two instructions had run. `S 3` now shows three lines, not four.
            RunResult r = m_.debug.run(1);
            total.steps += r.steps;
            total.tStates += r.tStates;
            total.pc = r.pc;
            bool stopped = r.why != StopReason::Steps;
            if (stopped) reportStop(r, m_.debug, out);
            if (echo) showRegs(out);
            if (stopped) break;
        }
        flush(out);
        disasmNext_ = c->pc();
        if (!echo) {
            char b[96];
            std::snprintf(b, sizeof b, "%llu instructions, %llu T-states.",
                          (unsigned long long)total.steps, (unsigned long long)total.tStates);
            out << b << "\n";
            showRegs(out);
        }
        return true;
    }

    // NEXT -- STEP that does not descend. At a CALL or RST it runs the callee to
    // completion and stops at the return address, so a subroutine reads as one step;
    // anything else is a plain single STEP. The mechanism is exactly what the
    // operator would do by hand: a temporary breakpoint at the return address and a
    // RUN. So it goes through runMachine() -- the callee is LIVE (it can read the
    // console) and interruptible (ATTN, ^C), and a real breakpoint inside it still
    // stops there. The temp target lives in the Debugger, off the user's list.
    if (cmd == "NEXT") {
        CpuCore* c = needCpu(out);
        if (!c) return true;

        uint8_t op = m_.bus.peek(c->pc());
        SigintGuard guard;
        if (isCall(op) || isRst(op)) {
            uint8_t len = 1;
            if (const Disassembler* d = disassemblerFor(c->isa())) len = insnAt(c->pc(), *d).len;
            m_.debug.setStepTarget((c->pc() + len) & 0xFFFF);
            runMachine(out, /*stepOver=*/true);
            m_.debug.setStepTarget(-1);  // ALWAYS clear -- a real bp/HLT/ATTN may have stopped us first
        } else {
            RunResult r = m_.debug.run(1);
            if (r.why != StopReason::Steps) reportStop(r, m_.debug, out);
        }
        flush(out);
        disasmNext_ = c->pc();
        showRegs(out);
        return true;
    }

    // RUN -- the switch on the panel. `RUN <addr>` is EXAMINE + RUN: it loads the
    // PC exactly as EXAMINE does, because on the panel that is literally the pair of
    // switches you throw. Everything else is in runMachine().
    if (cmd == "TYPE") {
        // Inject keystrokes at the guest console -- type-ahead, exactly what a key from
        // the VDM window or the terminal does (host/console.h). A `startup` line does this
        // BEFORE the RUN that boots the guest, which is how a machine file launches a
        // program the monitor cannot reach (e.g. SOLOS `XE`). See the command's HELP.
        if (!need(2, "TYPE \"text\"")) return true;
        const std::string s = unquote(a[1]);
        std::string keys;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                switch (s[++i]) {
                    case 'r': keys += '\r'; break;
                    case 'n': keys += '\n'; break;
                    case 't': keys += '\t'; break;
                    case '\\': keys += '\\'; break;
                    case '"': keys += '"'; break;
                    default: keys += '\\'; keys += s[i]; break;  // leave an unknown escape as written
                }
            } else {
                keys += s[i];
            }
        }
        Console::instance().inject(keys);
        return true;
    }

    if (cmd == "RUN") {
        CpuCore* c = needCpu(out);
        if (!c) return true;

        if (a.size() >= 2) {
            uint32_t at;
            if (!addrSym(a[1], at, out)) return true;
            if (at > 0xFFFF) {
                out << "address is 16 bits: 0000-FFFF\n";
                failed_ = true;
                return true;
            }
            c->setPc((uint16_t)at);
        }

        // Under MCP the server is single-threaded and there is no keyboard to press ATTN,
        // so entering the unbounded run loop would wedge the connection forever (this is
        // also the RUN inside a CONFIG LOAD startup). Park the PC and return; the client
        // advances with the non-blocking `run` tool (mcp/server.cpp).
        if (mcpMode_) {
            char b[64];
            std::snprintf(b, sizeof b, "%04X", (unsigned)c->pc());
            out << "PC set to " << b
                << "; not entering the run loop under MCP -- advance with the run tool.\n";
            disasmNext_ = c->pc();
            showRegs(out);
            return true;
        }

        runMachine(out);

        flush(out);
        disasmNext_ = c->pc();
        showRegs(out);
        return true;
    }

    // BREAK. Three kinds, and only ONE of them is about the CPU:
    //
    //   BREAK <addr>          PC lands here -- one comparison against a register
    //   BREAK MEM R|W <addr>  a bus CYCLE touched this address
    //   BREAK IO R|W <port>   a bus CYCLE touched this port
    //
    // The last two are bus observers (DESIGN.md 3.0.3), so they catch a DMA
    // transfer as readily as a processor, and they will work unchanged on a Z80.
    if (cmd == "BREAK") {
        if (a.size() < 2) {
            const auto& bps = m_.debug.breakpoints();
            if (bps.empty()) {
                out << "no breakpoints.\n";
                return true;
            }
            out << " id  what          hits\n";
            for (const Breakpoint& b : bps) {
                std::snprintf(buf, sizeof buf, "%3d  %-12s %5llu", b.id, b.describe().c_str(),
                              (unsigned long long)b.hits);
                out << buf << "\n";
            }
            return true;
        }

        // A trailing TRACE ON|OFF makes this a TRACEPOINT: it acts and the machine
        // keeps running. Stripped FIRST, because IF takes the whole rest of the line
        // as its expression and would otherwise eat it -- and `BREAK 200 IF HL==8000
        // TRACE ON` is exactly the combination worth having. `end` is the end of the
        // breakpoint proper from here on; nothing below may look at a.size() again.
        BreakAction action = BreakAction::Stop;
        size_t end = a.size();
        if (end >= 2 && is(a[end - 2], "TRACE") &&
            (is(a[end - 1], "ON") || is(a[end - 1], "OFF"))) {
            action = is(a[end - 1], "ON") ? BreakAction::TraceOn : BreakAction::TraceOff;
            end -= 2;
        }

        // BREAK <kind> <action> -- a DEVICE-EVENT breakpoint (BREAK TAPE STOP, and its
        // future siblings). It fires when a board reaches a named hardware state, not on a
        // bus cycle or a PC value, so it is checked here BEFORE the address path -- a
        // <kind> word that matched must resolve to a member of that kind or error, never
        // fall through to be read as a symbol. One table (core/debug.h kDeviceEvents)
        // drives this parser, describe() and the run-loop poll together, so a new member
        // is a single row and the three cannot drift. A trailing TRACE ON|OFF was already
        // stripped, so BREAK TAPE STOP TRACE ON arms a tracepoint for free.
        for (const DeviceEvent& de : kDeviceEvents) {
            if (!is(a[1], de.kind)) continue;
            if (end == 3 && is(a[2], de.action)) {
                // A tracepoint may be the first mention of tracing this session; point the
                // sink here but leave it off, exactly as the address path does below.
                if (action == BreakAction::TraceOn && !m_.debug.traceConfigured()) {
                    m_.debug.traceTo(&out, 0);
                    m_.debug.traceOff();
                }
                int id = m_.debug.add(de.bk, 0, 0, nullptr, action);
                for (const Breakpoint& b : m_.debug.breakpoints())
                    if (b.id == id) out << "breakpoint " << id << ": " << b.describe() << "\n";
                return true;
            }
            // The <kind> matched but the rest did not -- list what this kind accepts,
            // the same "which?" idiom BREAK MEM|IO uses for a bad R|W.
            out << "which? BREAK " << upper(a[1]);
            for (const DeviceEvent& e2 : kDeviceEvents)
                if (is(a[1], e2.kind)) out << " " << e2.action;
            out << "\n";
            failed_ = true;
            return true;
        }

        BreakKind kind = BreakKind::Pc;
        size_t argi = 1;
        bool io = is(a[1], "IO"), mem = is(a[1], "MEM");
        if (io || mem) {
            if (!need(3, "BREAK MEM|IO R|W <addr>")) return true;
            bool w = is(a[2], "W") || is(a[2], "WRITE");
            bool rd_ = is(a[2], "R") || is(a[2], "READ");
            if (!w && !rd_) {
                out << "which? BREAK " << upper(a[1]) << " R <addr>   or   ... W <addr>\n";
                failed_ = true;
                return true;
            }
            kind = io ? (w ? BreakKind::IoWrite : BreakKind::IoRead)
                      : (w ? BreakKind::MemWrite : BreakKind::MemRead);
            argi = 3;
        }
        if (end <= argi) {
            out << "usage: BREAK <addr> | BREAK MEM R|W <addr> | BREAK IO R|W <port>\n";
            failed_ = true;
            return true;
        }

        uint32_t lo, hi;
        if (a[argi].find('-') != std::string::npos || a[argi].find('/') != std::string::npos) {
            if (!range(a[argi], lo, hi, out)) return true;
        } else {
            // A symbol names a memory or PC break target; an I/O break wants a PORT, where a
            // symbol (a memory address) has no meaning -- so that one stays on addr().
            if (!(io ? addr(a[argi], lo, out) : addrSym(a[argi], lo, out))) return true;
            hi = lo;
        }

        // BREAK <addr> IF <expr>: a condition over the registers. PC breakpoints
        // only -- a MEM/IO breakpoint fires INSIDE an instruction, where "what is A?"
        // has no boundary-consistent answer. The rest of the line is the expression;
        // it is re-tokenized by the parser, so spacing does not matter.
        std::shared_ptr<const Expr> cond;
        if (end > argi + 1 && is(a[argi + 1], "IF")) {
            if (kind != BreakKind::Pc) {
                out << "IF applies to a plain BREAK <addr>, not to a MEM or IO breakpoint.\n";
                failed_ = true;
                return true;
            }
            CpuCore* c = needCpu(out);
            if (!c) return true;

            std::string src;
            for (size_t i = argi + 2; i < end; ++i) {
                if (!src.empty()) src += " ";
                src += a[i];
            }
            if (src.empty()) {
                out << "usage: BREAK <addr> IF <expr>   e.g. BREAK 100 IF A==0\n";
                failed_ = true;
                return true;
            }

            // A bare word is a register if the CPU reflects one by that name -- that
            // is what tells `A` the accumulator from `0A` the number.
            std::vector<RegDef> regs = c->registers();
            auto known = [&regs](const std::string& name) {
                for (const RegDef& rd : regs)
                    if (upper(rd.name) == upper(name)) return true;
                return false;
            };
            // ...and a bare word that is NOT a register may be a loaded symbol, folded to
            // its value so `BREAK 200 IF HL==STACK` reads the label. Registers win.
            auto symbol = [this](const std::string& name, uint32_t& v) {
                return m_.syms.lookup(name, v);
            };
            std::string perr;
            cond = Expr::parse(src, known, perr, symbol);
            if (!cond) {
                out << "bad condition: " << perr << "\n";
                failed_ = true;
                return true;
            }
        }

        // A tracepoint may be the FIRST mention of tracing in a session, and the
        // debugger cannot default a sink for itself -- it is core, and the console is
        // the monitor's. So point it here, but leave it OFF: the tracepoint turns it
        // on when it fires, which is the whole point of arming one.
        if (action == BreakAction::TraceOn && !m_.debug.traceConfigured()) {
            m_.debug.traceTo(&out, 0);
            m_.debug.traceOff();
        }

        int id = m_.debug.add(kind, lo, hi, cond, action);
        for (const Breakpoint& b : m_.debug.breakpoints())
            if (b.id == id) out << "breakpoint " << id << ": " << b.describe() << "\n";
        return true;
    }

    if (cmd == "NOBREAK") {
        if (a.size() < 2) {
            size_t n = m_.debug.breakpoints().size();
            m_.debug.clear();
            out << n << " breakpoint(s) cleared.\n";
            return true;
        }
        uint32_t id;
        if (!count(a[1], id, out)) return true;  // an id is not on the wire: DECIMAL
        std::string err;
        if (!m_.debug.remove((int)id, err)) {
            out << err << "\n";
            failed_ = true;
            return true;
        }
        out << "breakpoint " << id << " cleared.\n";
        return true;
    }

    // TRACE and HISTORY watch the SAME cycle stream every board sees, from outside
    // the backplane (DESIGN.md 3.0.3). Not CPU features -- they catch a DMA transfer
    // as readily as the processor and work unchanged on any core.
    if (cmd == "TRACE") {
        if (a.size() < 2 || (!is(a[1], "ON") && !is(a[1], "OFF"))) {
            out << "usage: TRACE ON|OFF [file] [MASK=IN,OUT,IRQ,DMA,CONTENTION]\n";
            failed_ = true;
            return true;
        }
        if (is(a[1], "OFF")) {
            m_.debug.traceOff();
            // The sink STAYS -- TRACE OFF stops the tracing, it does not forget where
            // it was going, so `TRACE ON <file> MASK=...` then `TRACE OFF` is how you
            // aim a tracepoint at a file. But FLUSH it: the file has to be complete on
            // disk for someone reading it now, even though we will write to it again.
            if (traceFile_.is_open()) traceFile_.flush();
            out << "trace off.";
            if (m_.debug.traceConfigured())
                out << "  (where it goes is remembered: TRACE ON, or a tracepoint, resumes it.)";
            out << "\n";
            return true;
        }

        // TRACE ON [file] [MASK=...]. Order does not matter: a MASK= token is the
        // mask, anything else is the file (at most one).
        unsigned mask = 0;
        std::string file;
        for (size_t i = 2; i < a.size(); ++i) {
            if (upper(a[i]).rfind("MASK=", 0) == 0) {
                std::string list = a[i].substr(5);
                size_t start = 0;
                while (start <= list.size()) {
                    size_t comma = list.find(',', start);
                    std::string tok = list.substr(start, comma - start);
                    if (!tok.empty()) {
                        if (is(tok, "IN"))              mask |= Debugger::InCycle;
                        else if (is(tok, "OUT"))        mask |= Debugger::OutCycle;
                        else if (is(tok, "IRQ"))        mask |= Debugger::Irq;
                        else if (is(tok, "DMA"))        mask |= Debugger::Dma;
                        else if (is(tok, "CONTENTION")) mask |= Debugger::Contended;
                        else {
                            out << "TRACE: unknown mask '" << tok
                                << "' -- pick from IN,OUT,IRQ,DMA,CONTENTION\n";
                            failed_ = true;
                            return true;
                        }
                    }
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
            } else if (file.empty()) {
                file = a[i];
            } else {
                out << "TRACE: unexpected '" << a[i] << "'\n";
                failed_ = true;
                return true;
            }
        }

        std::ostream* sink = &out;
        if (!file.empty()) {
            if (traceFile_.is_open()) traceFile_.close();
            traceFile_.open(file, std::ios::out | std::ios::trunc);
            if (!traceFile_) {
                out << "TRACE: cannot open " << file << "\n";
                failed_ = true;
                return true;
            }
            sink = &traceFile_;
        } else if (traceFile_.is_open()) {
            // Back to the console: the old file is no longer the sink, so close it
            // rather than leave a half-written trace open on a stream nobody writes.
            traceFile_.close();
        }
        m_.debug.traceTo(sink, mask);
        out << "trace on" << (file.empty() ? "" : (" -> " + file));
        if (mask) out << "  (masked)";
        out << ".\n";
        return true;
    }

    if (cmd == "HISTORY") {
        // The default is the CPU: one DDT-style line per instruction, the flight recorder
        // that parallels STEP. HISTORY BUS is the bus/cycle recorder (what HISTORY used to
        // be); HISTORY CPU names the default out loud. Then an optional count -- a depth is
        // not on the wire, so DECIMAL, and 16 when omitted.
        bool bus = false;
        size_t argi = 1;
        if (a.size() >= 2) {
            std::string sub = upper(a[1]);
            if (sub == "BUS") {
                bus = true;
                argi = 2;
            } else if (sub == "CPU") {
                argi = 2;
            }
        }
        size_t n = 16;
        if (a.size() > argi) {
            uint32_t cnt;
            if (!count(a[argi], cnt, out)) return true;
            n = cnt;
        }

        if (bus) {
            auto recs = m_.debug.history(n);
            if (recs.empty()) {
                out << "no bus history yet -- it records while the machine RUNs.\n";
                return true;
            }
            // Aligned to formatCycle()'s columns: T-state (10, right-justified), type (4),
            // addr/port (col 17), data (after " = ", col 24), then who drove -> who
            // answered. See Debugger::formatCycle. The handles resolve against the
            // debugger's board-name table.
            out << "   T-STATE  TYPE ADDR DATA   DROVE    -> ANSWERED\n";
            const auto& handles = m_.debug.boardHandles();
            for (const auto& rec : recs) out << Debugger::formatCycle(rec, handles) << "\n";
            return true;
        }

        // CPU: the STEP view, recorded. Each line is the machine as it stood WITH the
        // instruction it was about to run -- registers, flags and the decoded mnemonic.
        if (!needCpu(out)) return true;
        auto recs = m_.debug.insnHistory(n);
        if (recs.empty()) {
            out << "no CPU history yet -- it records while the machine RUNs.\n";
            return true;
        }
        for (const auto& rec : recs) out << renderInsn(rec) << "\n";
        return true;
    }

    // ---------------- EXECUTION ----------------
    if (cmd == "RESET") {
        // RESET CPU is a DEBUGGING CONVENIENCE AND NOT A REAL SIGNAL (DESIGN.md 6).
        // There is no wire on the backplane that resets the processor and nothing
        // else, and saying so is the difference between a tool and a lie.
        if (a.size() >= 2 && is(a[1], "CPU")) {
            CpuCore* c = needCpu(out);
            if (!c) return true;
            c->reset(Reset::Bus);
            out << "CPU reset: PC=0000, interrupts off. The other boards were NOT told.\n";
            return true;
        }
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
    if (cmd == "SNAPSHOT") {
        if (!need(2, "SNAPSHOT <file>")) return true;
        std::string shown = unquote(a[1]);
        std::string file  = resolveFrom(startupDir_, shown);
        std::string err;
        if (!m_.snapshot(file, err)) {
            out << "SNAPSHOT: " << err << "\n";
            failed_ = true;
            return true;
        }
        out << "snapshot written to " << shown << "\n";
        return true;
    }
    if (cmd == "RESTORE") {
        if (!need(2, "RESTORE <file>")) return true;
        std::string shown = unquote(a[1]);
        std::string file  = resolveFrom(startupDir_, shown);
        std::string err;
        if (!m_.restore(file, err)) {
            out << "RESTORE: " << err << "\n";
            failed_ = true;
            return true;
        }
        out << "restored from " << shown << "\n";
        flush(out);
        return true;
    }

    // ---------------- CONFIG ----------------
    if (cmd == "SYMBOLS") {
        if (!need(2, "SYMBOLS LOAD <file> [REPLACE] | SYMBOLS CLEAR")) return true;

        if (is(a[1], "CLEAR")) {
            m_.syms.clear();
            out << "symbols cleared\n";
            return true;
        }

        if (is(a[1], "LOAD")) {
            if (a.size() < 3) {
                out << "SYMBOLS LOAD <file> [REPLACE]\n";
                failed_ = true;
                return true;
            }
            std::string shown = unquote(a[2]);                    // the name SHOW SYMBOLS prints
            std::string file  = resolveFrom(startupDir_, shown);  // ...and where it actually is
            bool replace = a.size() > 3 && is(a[3], "REPLACE");

            std::ifstream f(file, std::ios::binary);
            if (!f) {
                out << "cannot open '" << file << "'\n";
                failed_ = true;
                return true;
            }
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                                      std::istreambuf_iterator<char>());

            // .SYM vs .PRN: the extension decides, and content settles a tie -- a symbol
            // table (HHHH NAME) is not an assembler listing (core/symbols.h).
            std::string ext;
            if (size_t dot = shown.rfind('.'); dot != std::string::npos) ext = upper(shown.substr(dot + 1));
            bool asSym = ext == "SYM" ? true
                       : (ext == "PRN" || ext == "LST") ? false
                       : looksLikeSym(data);

            SymbolTable::LoadStats st;
            std::string err;
            bool ok = asSym ? loadSym(data, shown, m_.syms, replace, st, err)
                            : loadPrn(data, shown, m_.syms, replace, st, err);
            if (!ok) {  // the only hard error is a relocatable .PRN, and it names the line
                out << err << "\n";
                failed_ = true;
                return true;
            }

            out << st.added << " symbol(s) from " << shown;
            if (st.redefined) {
                out << ", " << st.redefined << " redefined (";
                for (size_t i = 0; i < st.redefinedNames.size(); ++i)
                    out << (i ? " " : "") << st.redefinedNames[i];
                if ((int)st.redefinedNames.size() < st.redefined) out << " ...";
                out << ")";
            }
            out << "\n";
            // A load that parsed NOTHING is CALLED OUT: an unrecognised listing format
            // loads nothing, and a silent success there is the exact trap SHOW SYMBOLS
            // would then hide. A re-load where every name already existed (redefined > 0)
            // did parse -- it is not that case.
            if (st.added == 0 && st.redefined == 0)
                out << "  (found no symbols -- expected a CP/M .SYM or an ASM/M80/MAC .PRN)\n";
            return true;
        }

        out << "SYMBOLS LOAD <file> [REPLACE] | SYMBOLS CLEAR\n";
        failed_ = true;
        return true;
    }

    if (cmd == "CONFIG") {
        if (!need(3, "CONFIG LOAD|SAVE <file.toml>")) return true;
        std::string err;
        if (is(a[1], "LOAD")) {
            // THE MACHINE YOU HAD IS GONE, and only if the file was good. loadToml()
            // builds the new machine in a scratch backplane and swaps it in whole
            // (machine.h, replaceWith), so a file that does not parse leaves you
            // exactly where you were and this branch is never reached.
            std::vector<std::string> notes;
            if (!loadToml(a[2], m_, err, &notes)) {
                out << err << "\n";
                failed_ = true;
                return true;
            }
            // ...AND THEN POWER IT, because a backplane full of cards that have never
            // seen POC* is not a machine: its RAM was never filled, its ROM images were
            // never read, and no card has been told to reset. This is the same line
            // main.cpp runs after loading a machine named on the command line, and it
            // is here so that the two roads arrive at the same place -- which is the
            // whole claim `CONFIG LOAD mine.toml` makes.
            m_.power();
            out << "loaded " << a[2] << ": " << m_.boards().size() << " board(s)\n";
            // The author's `#>` notes, before the startup commands run -- an operator reads
            // "type DIR at the A> prompt" and then watches the prompt appear, in that order.
            for (const std::string& note : notes) out << note << "\n";
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
    //
    // ...WHICH IS EXACTLY WHY THE PATHS IN ONE NEED SAYING SOMETHING ABOUT. These
    // commands look like the ones a human types because they ARE the ones a human
    // types -- but they were WRITTEN IN A FILE, and a path written in a machine file
    // is relative to that file (core/paths.h). So for the length of this list, and
    // not one command longer, the machine's directory is where relative paths start.
    //
    // That is what makes `tapes/MitsPS2/ps2int.toml` a thing a user can be handed:
    //
    //     startup = ["MOUNT acr0:tape \"PS2-MON.TAP\"", "LOAD \"LDRPS2.HEX\"", "RUN 0"]
    //
    // names the two files lying beside it, and goes on naming them whether you `cd`
    // into that directory or point at it from somewhere else.
    startupDir_ = m_.dir;
    for (const auto& b : m_.boards()) b->setConfigDir(m_.dir);

    for (const auto& s : m_.startup) {
        out << "startup> " << s << "\n";
        if (!exec(s, out)) break;
    }

    // ...and the file stops talking. Whatever the operator types next is theirs.
    startupDir_.clear();
    for (const auto& b : m_.boards()) b->setConfigDir("");
}

int Monitor::repl(std::istream& in, std::ostream& out, bool interactive) {
    std::string line;
    LineEditor ed;

    // KEEP THE VIDEO WINDOW ALIVE WHILE THE MACHINE IS STOPPED (DESIGN.md 7.4). The run
    // loop pumps the SDL host once a slice, but a stopped machine sits here in the line
    // editor's blocking read and pumps nothing -- so the compositor pings the window for
    // liveness, gets no answer, and the OS offers to Force-Quit us (TODO). The editor runs
    // this on each idle tick: pollEvents() drains SDL's whole event queue (window liveness,
    // AND it tosses any gamepad events sharing that one queue -- so servicing the display
    // covers the joystick without waking the gamepad subsystem on a machine that has none),
    // and a close box clicked HERE -- unlike one clicked mid-RUN -- means the operator is
    // done with the window, so it closes for real. Liveness only: no emulated time advances
    // and nothing on the backplane is touched. A null display (headless, a test) makes this
    // a no-op.
    ed.setIdleHook([] {
        if (!g_display) return;
        g_display->pollEvents();
        if (g_display->takeQuitRequest()) g_display->closeWindow();
    });

    // Tab at the prompt completes commands, board ids, property names and their values --
    // all off the same reflection SET reads (complete(), above).
    ed.setCompleter([this](const std::string& s) { return complete(s); });

    // Lend this input to any interactive command (EDIT) for the life of the loop, and
    // take it back on the way out -- through every exit, including a `break`. Cleared to
    // null is the honest state everywhere else: a command that reads follow-up lines
    // must find nothing here when nobody is typing.
    in_ = &in;
    ed_ = &ed;
    struct Lend {
        Monitor& m;
        ~Lend() { m.in_ = nullptr; m.ed_ = nullptr; }
    } lend{*this};

    // PER-DIRECTORY COMMAND HISTORY. altairsim is run across many projects, so history
    // is per-cwd -- a hidden .altairsim_history in the directory you launched from, not
    // a per-user file (and there is no ~ on Windows to key one off anyway). Only a real
    // interactive terminal reads or writes it: -x/-s pass interactive==false, --mcp
    // returns before ever reaching repl, and a pipe (altairsim < script) is interactive
    // here but not a tty, so LineEditor::interactive() -- stdinIsTty() && stdoutIsTty()
    // -- is false. The process never chdir's, so current_path() is the launch cwd.
    // `SET CONSOLE history=0` turns the file off; a missing file just starts empty.
    std::filesystem::path histPath;
    if (interactive && LineEditor::interactive()) {
        std::error_code ec;
        std::filesystem::path cwd = std::filesystem::current_path(ec);  // no-throw overload
        if (!ec) {
            histPath = cwd / ".altairsim_history";
            int depth = Console::instance().historyDepth();
            if (depth > 0) {
                std::ifstream f(histPath);  // missing/unreadable -> falsy -> start empty
                if (f) ed.loadHistory(f, (size_t)depth);
            }
        }
    }

    // Write history back on EVERY exit -- a break, Ctrl-D/EOF, QUIT, or an exception --
    // so a session that ends any way still leaves its trail. The depth is re-read here
    // to honor a SET CONSOLE history=... made mid-session; 0 (off) or an unwritable
    // directory is a silent no-op, never a crash. `histPath` empty is the single "off"
    // signal -- it is set only on the interactive-tty path above.
    struct SaveHistory {
        const LineEditor&            ed;
        const std::filesystem::path& path;
        ~SaveHistory() {
            if (path.empty()) return;
            int depth = Console::instance().historyDepth();
            if (depth <= 0) return;
            std::ofstream f(path, std::ios::trunc);  // cannot open -> falsy -> no-op
            if (f) ed.saveHistory(f, (size_t)depth);
        }
    } saveHistory{ed, histPath};

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
