#include "config/toml.h"

// NOTE: no board header is included here, and none should ever be again. The config
// layer knows Board, and nothing about what any particular one of them is.
#include "boards/registry.h"
#include "core/machines.h"  // `base = "default"` -- a built-in is a config file too
#include "core/paths.h"     // ...and a file's relative paths are relative to IT
#include "host/console.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>
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

// Drop a trailing `# comment`, honoring quotes so a path with a '#' survives -- and
// honoring the BACKSLASH inside them, so an escaped quote does not flip us back out of
// the string and expose the rest of the line to the '#' test. The backslash itself is
// KEPT: the value parser below is the one that resolves the escape, not us.
std::string stripComment(const std::string& line) {
    std::string s;
    bool q = false, esc = false;
    for (char c : line) {
        if (esc) {
            s += c;
            esc = false;
            continue;
        }
        if (q && c == '\\') {
            s += c;
            esc = true;
            continue;
        }
        if (c == '"') q = !q;
        if (c == '#' && !q) break;
        s += c;
    }
    return s;
}

bool parse(const std::string& text, std::vector<Table>& out, std::string& err) {
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    Table* cur = nullptr;
    out.push_back({"", false, {}, {}, false});  // the root table
    cur = &out.back();

    while (std::getline(in, line)) {
        ++lineNo;
        std::string s = trim(stripComment(line));
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
                acc += " " + trim(stripComment(line));
            }
            size_t lb = acc.find('['), rb = acc.rfind(']');
            std::string body = acc.substr(lb + 1, rb - lb - 1);

            // ---- A STARTUP ENTRY IS A COMMAND LINE, AND A COMMAND LINE QUOTES ITS
            // FILENAMES. So `\"` has to survive to the monitor, and until it did, this
            // could not be written at all:
            //
            //     startup = ["MOUNT acr0:tape \"tapes/4KBasic31/4K BASIC Ver 3-1.tap\""]
            //
            // Every `"` toggled, escape or not, so the entry was cut at the backslash and
            // the machine came up with an empty recorder. That is not an exotic case: the
            // monitor's tokenizer needs the quotes precisely BECAUSE the period artifacts
            // all have spaces in their names (cli/monitor.cpp), so EVERY tape in the tree
            // was unmountable from a config file -- while docs/config.md promised "anything
            // you can type, a config can do".
            //
            // Two escapes, and no more. `\"` and `\\` are what a command line needs; the
            // rest of TOML's basic-string alphabet (\n, \t, \uXXXX) means nothing to a
            // monitor command, and quietly eating an unknown one would turn a Windows path
            // typed with single backslashes into a shorter, wrong path. Say so instead.
            std::string item;
            bool        inq = false, esc = false;
            for (char c : body) {
                if (esc) {
                    if (c != '"' && c != '\\') {
                        err = "line " + std::to_string(lineNo) + ": unknown escape `\\" +
                              std::string(1, c) + "` in " + k +
                              " (this parser knows \\\" and \\\\)";
                        return false;
                    }
                    item += c;
                    esc = false;
                    continue;
                }
                if (inq && c == '\\') {
                    esc = true;
                    continue;
                }
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

// ---------------------------------------------------------------------------
// `base = "default"` -- START FROM A MACHINE AND SAY WHAT IS DIFFERENT.
//
// A config file with no `base` is a COMPLETE MACHINE, exactly as it always was, and
// that is why the key is explicit rather than assumed. If every file silently
// inherited the default, then `4k` -- a machine defined by what it does NOT have --
// would have to REMOVE a floppy controller, a 2SIO and 52K of RAM to describe a bare
// 1975 Altair, and silence would stop meaning "nothing". One line at the top of a file
// tells you what its backplane starts as; without that line, the file IS the backplane.
//
// The depth guard is not paranoia: `base` can name a FILE, and two files can name each
// other. That is a hang, and a hang at startup is the worst kind.
// ---------------------------------------------------------------------------
constexpr int kMaxBaseDepth = 8;

bool loadInto(const std::string& text, const std::string& source, Machine& m,
              std::string& err, int depth);

// `dir` is the directory of the file that WROTE this `base` line -- because a base
// named as a file is a path like any other, and a path in a machine file is relative
// to that machine file (core/paths.h). `base = "../mini/cpm22-mini.toml"` means the
// one next door, from wherever the pair of them are copied to.
bool loadBase(const std::string& name, const std::string& dir, Machine& m, std::string& err,
              int depth) {
    if (depth >= kMaxBaseDepth) {
        err = "base = \"" + name + "\": more than " + std::to_string(kMaxBaseDepth) +
              " levels deep -- do two files name each other?";
        return false;
    }

    // A FILE OR A BUILT-IN, decided by SPELLING and never by probing the disk -- the
    // same rule the command line uses (looksLikeFile(), core/machines.h), and for the
    // same reason: `base = "default"` must not change meaning the day somebody saves a
    // file called `default` in the working directory.
    if (looksLikeFile(name)) {
        // The base is opened at the RESOLVED path, but it is loaded under its resolved
        // name too -- so that IT, in turn, computes its own directory from where it
        // really is, and ITS relative paths come out right. A chain of bases each
        // sitting in a different directory works, and each link speaks for itself.
        const std::string file = resolveFrom(dir, name);
        std::ifstream     f(file);
        if (!f) {
            err = "base: cannot open '" + file + "'";
            return false;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        return loadInto(ss.str(), file, m, err, depth + 1);
    }

    const BuiltinMachine* b = findMachine(name);
    if (!b) {
        err = "base: no built-in machine called '" + name + "' (try --list)";
        return false;
    }
    return loadInto(std::string(b->toml, b->size), "builtin:" + std::string(b->name), m, err,
                    depth + 1);
}

bool loadInto(const std::string& text, const std::string& source, Machine& m,
              std::string& err, int depth) {
    const std::string& path = source;

    // THE DIRECTORY THIS FILE IS SPEAKING FROM (core/paths.h).
    //
    // Decided by SPELLING, like everything else here: a built-in arrives as
    // "builtin:default", which names no directory, so its dir is "" -- and "" means
    // the shell's working directory, which is the only thing a machine living in
    // .rodata could possibly mean. `altairsim ps2int.toml`, run in the directory the
    // file is in, also gives "" -- the file names no directory either. The common
    // case costs nothing and changes nothing; only a file named through a directory
    // has anything to resolve.
    //
    // It is computed PER FRAME, not per machine. A `base` in another directory gets
    // its own, so its mounts are relative to IT and not to whoever named it.
    const std::string dir = looksLikeFile(source) ? dirOf(source) : std::string();

    // ...and the OUTERMOST file's directory is the machine's, because `startup` is a
    // list of commands that came out of that file and Monitor::runStartup has to know
    // where they were written to make sense of the paths in them.
    if (depth == 0) m.dir = dir;

    std::vector<Table> tabs;
    if (!parse(text, tabs, err)) {
        err = path + ": " + err;
        return false;
    }

    // Every card this frame configures is told where this frame is speaking from, and
    // told again -- with "" -- when the frame is done. A board resolves a path only
    // while a file is talking to it; the moment the operator takes over, "" is back
    // and MOUNT means what the shell says it means.
    struct ClearConfigDir {
        Machine& m;
        ~ClearConfigDir() {
            for (const auto& b : m.boards()) b->setConfigDir("");
        }
    } clearOnExit{m};

    Board* current = nullptr;

    // WHICH CARDS CAME FROM THE BASE, and which this file created itself. The whole
    // delta grammar turns on that difference -- see the [[board]] branch below.
    std::set<std::string> fromBase, declared;

    for (auto& t : tabs) {
        if (t.name == "machine" || t.name.empty()) {
            // BASE FIRST, whatever order the keys are written in. It builds the machine
            // that every other line in this file is a change TO, so it cannot run after
            // the `name` it would otherwise overwrite.
            for (auto& [k, v] : t.kv) {
                if (k != "base") continue;
                if (!m.boards().empty()) {
                    err = path + ": `base` must come before the first [[board]] -- it is "
                                 "what the boards are a change TO";
                    return false;
                }
                if (!loadBase(v, dir, m, err, depth)) {
                    err = path + ": " + err;
                    return false;
                }
                for (const auto& b : m.boards()) fromBase.insert(b->id);
            }

            for (auto& [k, v] : t.kv) {
                if (k == "base") continue;
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
                    // THE SWITCHES ARE ON THE PANEL, and the panel is a CARD -- exactly
                    // the same argument as clock_hz above, and it cost exactly as much
                    // to get wrong. This key USED to parse into a Machine::sense byte
                    // that nothing put on the bus: no board decoded port FF, so the
                    // guest's IN 0FFH read the floating bus (0xFF) no matter what was
                    // written here. A config that LOOKED like it set the switches and
                    // did not is precisely the failure the clock_hz error exists to
                    // prevent, so this key gets the same refusal and the same sentence.
                    err = path + ": sense belongs to the FRONT PANEL, not to [machine] --\n"
                          "  the switches are on the Display/Control board. Add the card:\n"
                          "      [[board]]\n"
                          "      type  = \"fp\"\n"
                          "      id    = \"fp0\"\n"
                          "      sense = " + v;
                    return false;
                } else if (k == "startup") {
                    m.startup = t.list;
                } else if (!t.name.empty()) {
                    err = path + ": unknown [machine] key '" + k + "'";
                    return false;
                }
            }
            continue;
        }

        // ---- [[board]] -- ADD a card, MODIFY one the base brought, REPLACE it, or
        // ---- PULL IT OUT. Which of the four is decided by `type` and `remove`:
        //
        //   type + a new id          ADD. The only form that exists in a file with no
        //                            `base`, and the only one that existed at all before
        //                            `base` did.
        //   type + an id from base   REPLACE, outright: naming a card's TYPE means you
        //                            are specifying the whole card, not amending it. The
        //                            base's settings on it are gone, which is what you
        //                            want when you re-fit a memory board from 56K to 24K
        //                            -- regions are a LIST, and appending a second one
        //                            would overlap the first rather than replace it.
        //   no type + an id          MODIFY IN PLACE. Properties, unit properties, and
        //                            anything added to its lists. This is the common one:
        //                            "the base's floppy controller, with a disk in it".
        //   remove = true            Pull the card out of the slot.
        //
        // AND `type` + AN ID THIS FILE ALREADY DECLARED IS STILL AN ERROR. That check is
        // load-bearing -- it is what catches a copy-pasted [[board]] whose id was never
        // changed -- so REPLACE is deliberately scoped to ids that came from the base.
        // A duplicate within one file is a typo; a duplicate against the base is intent.
        if (t.name == "board") {
            std::string type, id;
            bool        wantRemove = false;
            for (auto& [k, v] : t.kv) {
                if (k == "type") type = v;
                else if (k == "id") id = v;
                else if (k == "remove") wantRemove = (v == "true" || v == "1" || v == "yes");
            }
            if (id.empty()) {
                err = path + ": every [[board]] needs an `id`";
                return false;
            }

            if (wantRemove) {
                if (!type.empty()) {
                    err = path + ": [[board]] " + id +
                          ": `remove` and `type` contradict each other -- one takes the "
                          "card out, the other fits a new one";
                    return false;
                }
                for (auto& [k, v] : t.kv) {
                    (void)v;
                    if (k != "id" && k != "remove") {
                        err = path + ": [[board]] " + id + ": `" + k +
                              "` on a board that is being removed -- it would set a "
                              "property on a card that is about to leave the machine";
                        return false;
                    }
                }
                if (!m.remove(id, err)) {
                    err = path + ": [[board]] " + id + ": " + err;
                    return false;
                }
                fromBase.erase(id);
                current = nullptr;  // ...so a stray [[board.region]] after this is caught
                continue;
            }

            if (type.empty()) {
                current = m.find(id);
                if (!current) {
                    err = path + ": [[board]] " + id + ": no board with that id" +
                          (fromBase.empty()
                               ? " -- this file has no `base`, so there is nothing to "
                                 "modify. Give it a `type` to fit the card."
                               : " in the base. Give it a `type` to fit a new card, or "
                                 "check the spelling.");
                    return false;
                }
            } else {
                if (fromBase.count(id) && !declared.count(id)) {
                    if (!m.remove(id, err)) {  // REPLACE: out with the base's, in with ours
                        err = path + ": [[board]] " + id + ": " + err;
                        return false;
                    }
                    fromBase.erase(id);
                }
                current = m.add(type, id, err);  // a dup WITHIN this file still lands here
                if (!current) {
                    err = path + ": " + err;
                    return false;
                }
                declared.insert(id);
            }

            // THIS FILE IS NOW THE ONE TALKING TO THIS CARD, so any path it hands over
            // is relative to this file (core/board.h, core/paths.h). The loader still
            // does not know WHICH of the card's keys are paths, and must not -- that is
            // the board's business, and the whole point of properties() is that this
            // layer never learns what a `mount` is. It says where it is standing; the
            // card decides what to do about it.
            current->setConfigDir(dir);

            // Everything else is a PROPERTY, resolved against the board's own
            // properties(). The loader knows nothing about phantom straps or
            // baud rates and never will.
            for (auto& [k, v] : t.kv) {
                if (k == "type" || k == "id" || k == "remove") continue;
                if (!setProperty(*current, k, v, err)) {
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

            // ---- `[board.unit.a]` IS UNIT PROPERTIES, AND IT IS GENERIC. ----
            //
            // It is NOT a sub-unit table, and treating it as one is what broke CONFIG
            // SAVE. The WRITER emits [board.unit.<name>] for every board that has a
            // unit with settings -- generically, over units()/unitProperties(). The
            // READER used to demand the board opt in via subUnitTables(), and exactly
            // one board (the 2SIO) ever did. So the writer would faithfully save an
            // 88-ACR's `mode = "play"` and the reader would then refuse the file it
            // had just written: "board 'acr0' (acr) has no [[board.unit]] table". Every
            // machine with a cassette or a disk in it saved to something unloadable.
            //
            // A round trip is only a round trip if BOTH halves are generic. The 2SIO's
            // addSubUnit() was never board-specific anyway -- it looked the unit up by
            // name and called setUnitProperty() for each key, which is precisely this,
            // written once. (`region` and `drive` are different animals: those are
            // LISTS of things the board owns, and they keep addSubUnit().)
            if (table == "unit") {
                if (dot == std::string::npos) {
                    err = path + ": [board.unit] needs a unit name -- [board.unit.a]";
                    return false;
                }
                std::string unit = sub.substr(dot + 1);
                UnitDef     ud;
                if (!current->findUnit(unit, ud)) {
                    err = path + ": board '" + current->id + "' (" + current->type() +
                          ") has no unit '" + unit + "'";
                    return false;
                }
                // The ONE property path -- same parser, same radix rule, same
                // validation as `SET acr0:tape MODE=play` types at the monitor. A
                // config file cannot set something the monitor would refuse.
                //
                // ud.name, NOT `unit`: the name the BOARD has, not the case the file
                // happened to write it in. findUnit() is case-blind and had already
                // said yes to `[board.unit.A]` -- and then the raw "A" went down to
                // Sio2Board::channel(), which is not, and the file was refused with
                // "has no property 'baud'". The board's own name is the canonical one;
                // this is the same thing the monitor does with u.name.
                for (const auto& [k, v] : t.kv)
                    if (!setUnitProperty(*current, ud.name, k, v, err)) {
                        err = path + ": [board.unit." + unit + "] on " + current->id + ": " + err;
                        return false;
                    }
                continue;
            }

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

        // [console] -- the HOST's keyboard and screen, not a board. Its properties
        // go through the same one path as everything else (DESIGN.md 7.2), which
        // is why a config file cannot set something the monitor would refuse.
        if (t.name == "console") {
            for (const auto& [k, v] : t.kv) {
                if (!setPropertyIn(Console::instance().properties(), "console", k, v, err)) {
                    err = path + ": [console]: " + err;
                    return false;
                }
            }
            continue;
        }

        err = path + ": unknown table [" + t.name + "]";
        return false;
    }

    // ---- EXACTLY ONE UNIT MAY HOLD THE CONSOLE, AND A CONFIG FILE IS NOT EXEMPT
    // (Patrick, 2026-07-12: "assuming several serial boards with multiple ports
    // each, how is the console determined?")
    //
    // The monitor's CONNECT has arbitrated this from the start -- connecting a
    // second unit STEALS the console and says who from. This path did not, and a
    // rule enforced on one of two paths is not a rule: a file could cable two ports
    // to one terminal, and each would get half the operator's keystrokes, silently.
    //
    // But it must NOT steal here. Interactively, `CONNECT sio1:a console` is you
    // moving the cable, and the last one you plug in is the one you meant. A FILE
    // that names two consoles is not a decision, it is a typo -- there is no "last"
    // about it -- so it is refused, and both are named.
    {
        std::vector<std::string> holders;
        for (const auto& b : m.boards())
            for (const auto& u : b->units())
                if (u.kind == UnitKind::Serial && u.state == "console")
                    holders.push_back(b->id + ":" + u.name);

        if (holders.size() > 1) {
            err = path + ": " + std::to_string(holders.size()) +
                  " units are cabled to the console (";
            for (size_t i = 0; i < holders.size(); ++i) err += (i ? ", " : "") + holders[i];
            err +=
                ").  There is one keyboard, so they would each get half of what you\n"
                "   type.  Connect ONE to `console`; the others can take null, loopback,\n"
                "   a socket or a serial port.";
            return false;
        }
    }

    // A machine that has just been built has just been switched on -- ONCE, at the
    // outermost file. A `base` is not a machine that ran and was then modified; it is
    // the first half of building this one, and powering it up mid-build would mean the
    // cards the base brought saw POWER before the cards this file adds even existed.
    if (depth == 0) m.power();
    return true;
}

} // namespace

// A BUILT-IN MACHINE IS A TOML FILE THAT HAPPENS TO LIVE IN .rodata.
//
// This is the same bargain as the built-in ROMs, and for the same reason: there
// is ONE machine language, and a built-in cannot drift from a file because it
// travels the identical parser. `source` is only ever used to name the thing in
// an error message -- "builtin:turnkey: ..." reads exactly like a path would.
//
// ...which is also what makes `base = "default"` cost nothing: a built-in and a file are
// the same text through the same parser, so a base can be either one.
bool loadTomlText(const std::string& text, const std::string& source, Machine& m,
                  std::string& err) {
    return loadInto(text, source, m, err, /*depth=*/0);
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
    f << saveTomlText(m);
    return true;
}

// The text CONFIG SAVE writes, without a file in the way.
//
// Split out from saveToml() so that the ROUND TRIP is testable in memory: feed this
// straight into loadTomlText() and the machine that comes back must be the machine that
// went in. That test is not decoration -- CONFIG SAVE spent this whole milestone writing
// [board.unit.<name>] tables that the loader then REFUSED, so every machine with a
// cassette or a disk in it saved to a file that would not load. The two halves are both
// generic now, and this is what keeps them that way.
std::string saveTomlText(Machine& m) {
    std::ostringstream f;
    f << "[machine]\n";
    f << "name     = \"" << m.name << "\"\n";
    // No clock_hz here, and no sense either. Both are BOARD properties -- the crystal
    // is on the CPU card and the switches are on the front panel -- so both are
    // written out by the same generic properties() walk that writes every other
    // board's, which is why CONFIG SAVE round-trips and cannot drift from what SET
    // accepts.
    // ...and the startup list, ESCAPED, because a startup entry is a command line and a
    // command line quotes its filenames. Write `MOUNT acr0:tape "4K BASIC Ver 3-1.tap"`
    // out raw and the quotes around the path close the TOML string early -- CONFIG SAVE
    // produces a file CONFIG LOAD cannot read, which is the same asymmetry the unit
    // tables had. The reader knows exactly these two escapes and no others.
    if (!m.startup.empty()) {
        f << "startup  = [\n";
        for (const auto& s : m.startup) {
            f << "  \"";
            for (char c : s) {
                if (c == '"' || c == '\\') f << '\\';
                f << c;
            }
            f << "\",\n";
        }
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
        // ---- Unit properties: `[board.unit.a]` ----
        //
        // Generic, over units() and unitProperties(). A card added next year that
        // declares units with settings round-trips through CONFIG SAVE with no
        // change here -- which is the whole bet the reflection layer is making.
        //
        // NOTE WHAT THIS DELIBERATELY DOES NOT DO: it does not probe for
        // read-only properties by calling set(get()), the way the board walk above
        // does. That trick is safe for a port jumper and NOT safe here, because
        // `connect` has a side effect -- setting it RE-RESOLVES THE ENDPOINT. On a
        // console that means tearing the terminal down and rebuilding it; on a
        // socket it would mean rebinding a live port. Saving a config file must not
        // perturb the machine it is describing.
        for (const auto& u : b->units()) {
            auto up = b->unitProperties(u.name);
            if (up.empty()) continue;
            f << "\n  [board.unit." << u.name << "]\n";
            for (const auto& p : up) {
                if (!p.set) continue;
                Value v = p.get();
                if (p.kind == Kind::Str || p.kind == Kind::Enum)
                    f << "  " << p.name << " = \"" << v.text(p.radix) << "\"\n";
                else
                    f << "  " << p.name << " = " << v.text(p.radix) << "\n";
            }
        }

        // Sub-units: [[board.region]], [[board.drive]].
        //
        // The TODO that used to live here -- "this dynamic_cast is the last
        // board-specific line in the config layer, and it should not survive" -- is
        // discharged. It is these four lines now, and they know nothing about memory,
        // about disks, or about anything else a board might keep a list of. The board
        // rendered the text; this writes it down.
        for (const auto& su : b->subUnits()) {
            f << "\n  [[board." << su.table << "]]\n";
            for (const auto& fl : su.fields)
                f << "  " << fl.key << " = "
                  << (fl.quoted ? "\"" + fl.text + "\"" : fl.text) << "\n";
        }
    }
    return f.str();
}

} // namespace altair
