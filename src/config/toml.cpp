#include "config/toml.h"

// NOTE: no board header is included here, and none should ever be again. The config
// layer knows Board, and nothing about what any particular one of them is.
#include "boards/registry.h"
#include "core/machines.h"  // `base = "default"` -- a built-in is a config file too
#include "core/paths.h"     // ...and a file's relative paths are relative to IT
#include "core/version.h"   // CONFIG SAVE names the release a default belongs to
#include "host/console.h"
#include "host/display.h"
#include "host/terminal/stream.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
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
    // WHERE IT WAS WRITTEN, for the error message: the header's line, and one line per
    // entry of `kv`. A parallel vector and not a third member of the pair, because
    // KeyValues is what every board's loadSubUnit() takes and a board has no use for it.
    int line = 0;
    std::vector<int> kvLine;
};

// Drop a trailing `# comment`, honoring quotes so a path with a '#' survives -- and
// honoring the BACKSLASH inside them, so an escaped quote does not flip us back out of
// the string and expose the rest of the line to the '#' test. The backslash itself is
// KEPT: the value parser below is the one that resolves the escape, not us.
//
// ONE comment is louder than the rest: a `#>` comment is a NOTE the file's author wrote
// for the operator to READ when the machine loads (docs/manual/configuring.md). It is
// still a comment -- it configures nothing, it round-trips through nothing -- but instead
// of being dropped on the floor its text is captured into `notes`, in file order, one
// entry per `#>` line. `#>` alone is a blank line; a single leading space after the `>`
// is eaten so `#> text` reads as `text`. An ordinary `#` is discarded exactly as before.
//
// A SINGLE-QUOTED (literal) string is honored too, and it has no escapes at all: inside
// '...' a '"', a '\' or a '#' is just text. That is how CONFIG SAVE writes a value that
// holds a '"' (issue #538) -- the double-quoted form resolves no escapes in a single value,
// so a raw '"' in it would end the string here and let a later '#' cut the line.
std::string stripComment(const std::string& line, std::vector<std::string>* notes) {
    std::string s;
    bool q = false, lit = false, esc = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
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
        if (c == '"' && !lit) q = !q;
        if (c == '\'' && !q) lit = !lit;
        if (c == '#' && !q && !lit) {
            if (notes && i + 1 < line.size() && line[i + 1] == '>') {
                std::string note = line.substr(i + 2);
                if (!note.empty() && note.back() == '\r') note.pop_back();  // a CRLF file
                if (!note.empty() && note.front() == ' ') note.erase(0, 1);
                notes->push_back(note);
            }
            break;
        }
        s += c;
    }
    return s;
}

bool parse(const std::string& text, std::vector<Table>& out, std::string& err,
           std::vector<std::string>& notes) {
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    Table* cur = nullptr;
    out.push_back({"", false, {}, {}, false, 0, {}});  // the root table
    cur = &out.back();

    while (std::getline(in, line)) {
        ++lineNo;
        std::string s = trim(stripComment(line, &notes));
        if (s.empty()) continue;

        if (s.front() == '[') {
            bool arr = s.compare(0, 2, "[[") == 0;
            size_t close = s.find(arr ? "]]" : "]");
            if (close == std::string::npos) {
                err = "line " + std::to_string(lineNo) + ": unterminated table header";
                return false;
            }
            std::string nm = trim(s.substr(arr ? 2 : 1, close - (arr ? 2 : 1)));
            if (!trim(s.substr(close + (arr ? 2 : 1))).empty()) {
                err = "line " + std::to_string(lineNo) + ": unexpected text after the table header";
                return false;
            }
            out.push_back({nm, arr, {}, {}, false, lineNo, {}});
            cur = &out.back();
            continue;
        }

        size_t eq = s.find('=');
        std::string k = eq == std::string::npos ? std::string() : trim(s.substr(0, eq));
        if (k.empty()) {
            err = "line " + std::to_string(lineNo) + ": expected key = value";
            return false;
        }
        std::string v = trim(s.substr(eq + 1));

        // A KEY WRITTEN TWICE IN ONE TABLE is a block that was pasted and not cleaned up.
        // The second one used to win silently, which is the one thing a machine file must
        // never do: look as if a setting took when a later line undid it.
        for (size_t i = 0; i < cur->kv.size(); ++i)
            if (cur->kv[i].first == k) {
                err = "line " + std::to_string(lineNo) + ": '" + k + "' is already set at line " +
                      std::to_string(cur->kvLine[i]);
                return false;
            }
        const int keyLine = lineNo;

        if (!v.empty() && v.front() == '[') {
            // An inline array. We only use it for `startup`, and multi-line
            // arrays are common there, so keep reading until the bracket closes.
            std::string acc = v;
            while (acc.find(']') == std::string::npos && std::getline(in, line)) {
                ++lineNo;
                acc += " " + trim(stripComment(line, &notes));
            }
            size_t lb = acc.find('['), rb = acc.rfind(']');
            if (rb == std::string::npos) {
                err = "line " + std::to_string(keyLine) + ": unterminated array";
                return false;
            }
            std::string body = acc.substr(lb + 1, rb - lb - 1);

            // ---- A STARTUP ENTRY IS A COMMAND LINE, AND A COMMAND LINE QUOTES ITS
            // FILENAMES. So `\"` has to survive to the monitor, and until it did, this
            // could not be written at all:
            //
            //     startup = ["MOUNT acr0:tape \"examples/basic4k/4K BASIC Ver 3-1.tap\""]
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
            cur->kvLine.push_back(keyLine);
            continue;
        }

        cur->kv.push_back({k, unquote(v)});
        cur->kvLine.push_back(keyLine);
    }
    return true;
}

// ---------------------------------------------------------------------------
// `base = "default"` -- START FROM A MACHINE AND SAY WHAT IS DIFFERENT.
//
// A config file with no `base` is a COMPLETE MACHINE, exactly as it always was, and
// that is why the key is explicit rather than assumed. If every file silently
// inherited the default, then `original` -- a machine defined by what it does NOT have --
// would have to REMOVE a floppy controller, a 2SIO and 52K of RAM to describe a bare
// 1975 Altair, and silence would stop meaning "nothing". One line at the top of a file
// tells you what its backplane starts as; without that line, the file IS the backplane.
//
// The depth guard is not paranoia: `base` can name a FILE, and two files can name each
// other. That is a hang, and a hang at startup is the worst kind.
// ---------------------------------------------------------------------------
constexpr int kMaxBaseDepth = 8;

// The depth guard's message, alone. Every other error in a chain of bases gains one
// `file: line N:` per level, which is the path to the bad file. A loop has no bad file,
// so this one goes up unchanged and only the file the user loaded puts its name on it.
const std::string kBaseTooDeep = "more than " + std::to_string(kMaxBaseDepth) +
                                 " levels deep -- do two files name each other?";

bool loadInto(const std::string& text, const std::string& source, Machine& m,
              std::string& err, int depth, std::vector<std::string>* notesOut = nullptr);

// `dir` is the directory of the file that WROTE this `base` line -- because a base
// named as a file is a path like any other, and a path in a machine file is relative
// to that machine file (core/paths.h). `base = "../mini/cpm22-mini.toml"` means the
// one next door, from wherever the pair of them are copied to.
bool loadBase(const std::string& name, const std::string& dir, Machine& m, std::string& err,
              int depth) {
    if (depth >= kMaxBaseDepth) {
        err = kBaseTooDeep;
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
              std::string& err, int depth, std::vector<std::string>* notesOut) {
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
    if (depth == 0) {
        m.dir = dir;
        // Decided by SPELLING, like `dir` itself: a built-in arrives as "builtin:name",
        // which is not a file. This is the ONLY reliable built-in-vs-file answer -- `dir`
        // is empty for a cwd-local file too, so SHOW PATHS must read this, not dir.empty().
        m.fromFile = looksLikeFile(source);
    }

    std::vector<Table>       tabs;
    std::vector<std::string> localNotes;
    if (!parse(text, tabs, err, localNotes)) {
        err = path + ": " + err;
        return false;
    }
    // `#>` notes belong to the OUTERMOST file only -- the one the operator loaded. A `base`
    // recurses through loadInto() at depth+1 with no notes pointer, so its own notes stay
    // where they were written and do not surface on top of the file that inherited it.
    if (depth == 0 && notesOut) *notesOut = std::move(localNotes);

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

    // EVERY ERROR BELOW SAYS WHICH LINE, in the form parse() already used. A machine file
    // is mostly comment, and "unknown key" with no line sends the reader hunting.
    auto at = [&path](int line) { return path + ": line " + std::to_string(line) + ": "; };

    Board* current = nullptr;

    // A TABLE THAT IS WRITTEN ONE TIME: [machine], [console], [display], [terminal], and
    // each [board.unit.<name>] under one [[board]]. A second one is two files pasted
    // together, and it used to be reported as whatever the second copy tripped over --
    // a second `base` said "must come before the first [[board]]" in a file with no board
    // above it. THIS FILE only: a `base` is its own frame, so a file may restate its base.
    std::map<std::string, int> onceTables, unitTables;
    auto once = [&](std::map<std::string, int>& seen, const std::string& key,
                    const std::string& shown, int line) {
        auto [it, fresh] = seen.emplace(key, line);
        if (fresh) return true;
        err = at(line) + "[" + shown + "] is already at line " + std::to_string(it->second) +
              " -- a table is written one time";
        return false;
    };

    // WHICH CARDS CAME FROM THE BASE, and which this file created itself. The whole
    // delta grammar turns on that difference -- see the [[board]] branch below.
    std::set<std::string> fromBase, declared;

    for (auto& t : tabs) {
        // A KEY ABOVE THE FIRST TABLE belongs to nothing. It used to be read as a
        // [machine] key when it was one and DROPPED when it was not, so a misspelled
        // setting up there loaded clean and did nothing.
        if (t.name.empty()) {
            if (!t.kv.empty()) {
                err = at(t.kvLine[0]) + "'" + t.kv[0].first + "' is not in a table";
                return false;
            }
            continue;
        }

        // THE BRACKETS ARE PART OF THE NAME. [[board]] is "one more board" and [machine]
        // is "the machine", and the header used to be read by its name only -- so a file
        // with [board] written twice loaded as two boards, and [[console]] loaded as
        // [console]. A LIST of things takes double brackets: a board, and everything a
        // board owns a list of (region, drive, socket). A table written one time takes
        // single: the four host tables, and a unit's properties. A name we do not know is
        // left for "unknown table" below.
        {
            bool once1 = t.name == "machine" || t.name == "console" || t.name == "display" ||
                         t.name == "terminal" || t.name == "board.unit" ||
                         t.name.rfind("board.unit.", 0) == 0;
            bool list = !once1 && (t.name == "board" || t.name.rfind("board.", 0) == 0);
            if (list && !t.array) {
                err = at(t.line) + "[" + t.name + "] must be [[" + t.name +
                      "]] -- double brackets, one for each";
                return false;
            }
            if (once1 && t.array) {
                err = at(t.line) + "[[" + t.name + "]] must be [" + t.name +
                      "] -- a table that is written one time";
                return false;
            }
        }

        if (t.name == "machine" || t.name == "console" || t.name == "display" ||
            t.name == "terminal") {
            if (!once(onceTables, t.name, t.name, t.line)) return false;
        }

        if (t.name == "machine") {
            // BASE FIRST, whatever order the keys are written in. It builds the machine
            // that every other line in this file is a change TO, so it cannot run after
            // the `name` it would otherwise overwrite.
            for (size_t i = 0; i < t.kv.size(); ++i) {
                const auto& [k, v] = t.kv[i];
                if (k != "base") continue;
                if (!m.boards().empty()) {
                    err = at(t.kvLine[i]) + "`base` must come before the first [[board]] -- "
                                            "it is what the boards are a change TO";
                    return false;
                }
                if (!loadBase(v, dir, m, err, depth)) {
                    if (err != kBaseTooDeep)
                        err = at(t.kvLine[i]) + err;
                    else if (depth == 0)
                        err = at(t.kvLine[i]) + "base = \"" + v + "\": " + err;
                    return false;
                }
                for (const auto& b : m.boards()) fromBase.insert(b->id);
            }

            for (size_t i = 0; i < t.kv.size(); ++i) {
                const auto& [k, v] = t.kv[i];
                const int   ln = t.kvLine[i];
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
                    err = at(ln) + "clock_hz belongs to the CPU BOARD, not to [machine] --\n"
                          "  the crystal is on the board. Put it in the CPU's [[board]]:\n"
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
                    err = at(ln) + "sense belongs to the FRONT PANEL, not to [machine] --\n"
                          "  the switches are on the Display/Control board. Add the board:\n"
                          "      [[board]]\n"
                          "      type  = \"fp\"\n"
                          "      id    = \"fp0\"\n"
                          "      sense = " + v;
                    return false;
                } else if (k == "startup") {
                    m.startup = t.list;
                } else {
                    err = at(ln) + "unknown [machine] key '" + k + "'";
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
            unitTables.clear();  // a new board: its unit tables start over
            if (id.empty()) {
                err = at(t.line) + "every [[board]] needs an `id`";
                return false;
            }

            if (wantRemove) {
                if (!type.empty()) {
                    err = at(t.line) + "[[board]] " + id +
                          ": `remove` and `type` contradict each other -- one takes the "
                          "board out, the other fits a new one";
                    return false;
                }
                for (size_t i = 0; i < t.kv.size(); ++i) {
                    const std::string& k = t.kv[i].first;
                    if (k != "id" && k != "remove") {
                        err = at(t.kvLine[i]) + "[[board]] " + id + ": `" + k +
                              "` on a board that is being removed -- it would set a "
                              "property on a board that is about to leave the machine";
                        return false;
                    }
                }
                if (!m.remove(id, err)) {
                    err = at(t.line) + "[[board]] " + id + ": " + err;
                    return false;
                }
                fromBase.erase(id);
                current = nullptr;  // ...so a stray [[board.region]] after this is caught
                continue;
            }

            if (type.empty()) {
                current = m.find(id);
                if (!current) {
                    err = at(t.line) + "[[board]] " + id + ": no board with that id" +
                          (fromBase.empty()
                               ? " -- this file has no `base`, so there is nothing to "
                                 "modify. Give it a `type` to fit the board."
                               : " in the base. Give it a `type` to fit a new board, or "
                                 "check the spelling.");
                    return false;
                }
            } else {
                if (fromBase.count(id) && !declared.count(id)) {
                    if (!m.remove(id, err)) {  // REPLACE: out with the base's, in with ours
                        err = at(t.line) + "[[board]] " + id + ": " + err;
                        return false;
                    }
                    fromBase.erase(id);
                }
                current = m.add(type, id, err);  // a dup WITHIN this file still lands here
                if (!current) {
                    err = at(t.line) + err;
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
            for (size_t i = 0; i < t.kv.size(); ++i) {
                const auto& [k, v] = t.kv[i];
                if (k == "type" || k == "id" || k == "remove") continue;
                if (!setProperty(*current, k, v, err)) {
                    err = at(t.kvLine[i]) + "[[board]] " + id + ": " + err;
                    return false;
                }
            }
            continue;
        }

        // A sub-unit table: [[board.region]], [[board.drive]], [board.unit.a].
        if (t.name.rfind("board.", 0) == 0) {
            if (!current) {
                err = at(t.line) + (t.array ? "[[" : "[") + t.name + (t.array ? "]]" : "]") +
                      " before any [[board]]";
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
                    err = at(t.line) + "[board.unit] needs a unit name -- [board.unit.a]";
                    return false;
                }
                std::string unit = sub.substr(dot + 1);
                UnitDef     ud;
                if (!current->findUnit(unit, ud)) {
                    err = at(t.line) + "board '" + current->id + "' (" + current->type() +
                          ") has no unit '" + unit + "'";
                    return false;
                }
                if (!once(unitTables, ud.name, "board.unit." + ud.name, t.line)) return false;
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
                for (size_t i = 0; i < t.kv.size(); ++i)
                    if (!setUnitProperty(*current, ud.name, t.kv[i].first, t.kv[i].second, err)) {
                        err = at(t.kvLine[i]) + "[board.unit." + unit + "] on " + current->id +
                              ": " + err;
                        return false;
                    }
                continue;
            }

            // ONE DOOR (Board::loadSubUnit). It checks the table is this card's, checks
            // every key against the keys the card DECLARES it takes, and validates every
            // value against the kind, the choices and the range that declaration carries --
            // before the card is asked to build anything. The loader learns nothing about
            // what a region or a drive is, which is what it never knew and never should.
            KeyValues kv = t.kv;
            if (dot != std::string::npos) kv.push_back({"unit", sub.substr(dot + 1)});
            // The board's message already names the TABLE and the CARD TYPE ("dcdd:
            // [[board.drive]] has no key `readnoly` -- its keys are unit, mount, readonly, media"),
            // so all this needs to add is the file and the id. Prefixing the table again
            // here read `[[board.drive]] on dsk0: dcdd: [[board.drive]] has no ...`.
            //
            // THE LINE IS THE KEY'S, NOT THE TABLE'S. TOML puts a bare key in the last
            // table opened, so a stray key can be far below the header, and the header's
            // line looks correct. The `unit` appended above has no line of its own; it
            // and a refusal about the whole table fall back to the header.
            size_t bad = kv.size();
            if (!current->loadSubUnit(table, kv, err, &bad)) {
                bool oneKey = bad < t.kvLine.size();
                err = at(oneKey ? t.kvLine[bad] : t.line) + current->id + ": " + err;
                // ...AND SAY WHICH TABLE THE KEY IS IN. The key's line alone reads as
                // nonsense when the reader did not mean the key for this table at all:
                // `name = "x"` with no [machine] above it "has no `name`" on a drive.
                // The header's line is what shows them where the key landed.
                if (oneKey)
                    err += "; this key is in the [[board." + table + "]] table that starts at line " +
                           std::to_string(t.line);
                return false;
            }
            continue;
        }

        // [console] -- the HOST's keyboard and screen, not a board. Its properties
        // go through the same one path as everything else (DESIGN.md 7.2), which
        // is why a config file cannot set something the monitor would refuse.
        if (t.name == "console") {
            for (size_t i = 0; i < t.kv.size(); ++i) {
                if (!setPropertyIn(Console::instance().properties(), "console", t.kv[i].first, t.kv[i].second, err)) {
                    err = at(t.kvLine[i]) + "[console]: " + err;
                    return false;
                }
            }
            continue;
        }

        // [display] -- the HOST's video window, the same kind of thing as [console]
        // and not a board either. A machine whose console IS its screen says so here,
        // which is the whole point of the setting being answerable before any window
        // exists (host/display.h).
        //
        // Accepted in a build with no video: the setting is about what this machine
        // WANTS, and a headless host simply has nothing to apply it to. A machine file
        // that failed to load on a no-SDL build would make the file unportable for a
        // preference that changes no emulated behavior at all.
        if (t.name == "display") {
            for (size_t i = 0; i < t.kv.size(); ++i) {
                if (!setPropertyIn(Display::properties(), "display", t.kv[i].first, t.kv[i].second, err)) {
                    err = at(t.kvLine[i]) + "[display]: " + err;
                    return false;
                }
            }
            continue;
        }

        // [terminal] -- the built-in terminal's transform chain (issue #244). Like
        // [console], but it lives on the `connect = "terminal"` endpoint, so a real
        // serial line stays 8-bit clean (host/terminal/stream.h). Accepted headless for
        // the same reason as [display]: the setting is about what the machine WANTS, and
        // a build with no window simply has nothing to apply it to.
        if (t.name == "terminal") {
            for (size_t i = 0; i < t.kv.size(); ++i) {
                if (!setPropertyIn(TerminalStream::properties(), "terminal", t.kv[i].first, t.kv[i].second, err)) {
                    err = at(t.kvLine[i]) + "[terminal]: " + err;
                    return false;
                }
            }
            continue;
        }

        err = at(t.line) + "unknown table [" + t.name + "]";
        return false;
    }

    // ---- EXACTLY ONE UNIT MAY HOLD THE CONSOLE, AND A CONFIG FILE IS NOT EXEMPT
    // (Patrick, 2026-07-12, asking how the console is determined when a machine has
    // several serial boards with multiple ports each.)
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
// LOADING A MACHINE FILE IS ALL OR NOTHING, and that is what the scratch machine below
// is for. `built` is where the file is assembled; `m` does not change until it has been
// assembled successfully, and if the file is bad `m` never changes at all.
//
// This is also what makes a machine file mean the SAME THING wherever it is read. It is
// a MACHINE -- the whole backplane, the thing CONFIG SAVE wrote down -- and not a list
// of amendments to whatever you happen to have running. `altairsim -f mine.toml` and
// `CONFIG LOAD mine.toml` are now one road with one meaning, and the round trip the
// manual promises (configuring.md) actually holds: save what you have, load it back,
// get it back.
//
// It cost two bugs to learn. Loading straight into `m` MERGED, so CONFIG LOAD of a file
// CONFIG SAVE had just written died on `a board with id 'fp0' already exists` -- the
// documented worked example, refused by the program. And because the merge ran until it
// hit that, a file whose new cards came first left them behind: the load FAILED and
// changed your machine anyway.
//
// `base` still merges, and must -- it is the one case where a file IS an amendment to
// another, and it recurses through loadInto() below, not through here. Note that it can
// only work at all now: `base` refuses to run once a machine has boards in it, so in the
// old world CONFIG LOAD of any file with a `base` was dead on arrival.
bool loadTomlText(const std::string& text, const std::string& source, Machine& m,
                  std::string& err, std::vector<std::string>* notes) {
    Machine built;
    if (!loadInto(text, source, built, err, /*depth=*/0, notes)) return false;
    m.replaceWith(built);
    return true;
}

bool loadToml(const std::string& path, Machine& m, std::string& err,
              std::vector<std::string>* notes) {
    std::ifstream f(path);
    if (!f) {
        err = "cannot open '" + path + "'";
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return loadTomlText(ss.str(), path, m, err, notes);
}

// A text value, quoted so the reader gets back exactly these bytes (issue #538).
//
// A double-quoted single value resolves NO escapes -- existing machine files write Windows
// paths as "C:\disks\x.dsk" with single backslashes, and they must keep loading -- so a
// '"' cannot be escaped into one. A value holding a '"' is written as a TOML literal string,
// '...', which has no escapes to need. A value holding BOTH quote characters has no form
// this reader takes back, so it is refused (`what` names it) instead of written wrong.
// Everything else is double-quoted exactly as before, so an ordinary save is unchanged.
static std::string tomlString(const std::string& text, const std::string& what,
                              std::string* err) {
    if (text.find('"') == std::string::npos) return "\"" + text + "\"";
    if (text.find('\'') == std::string::npos) return "'" + text + "'";
    if (err && err->empty())
        *err = what + ": a value with both ' and \" in it cannot be saved";
    return "\"\"";
}

// What a NEW board of a type starts with, as property name -> value text, for CONFIG SAVE to
// mark the keys that still hold it. `unit` empty = the board's own properties, else that
// unit's. A board that cannot be built, or has no such unit, gives an empty map: nothing is
// marked. This only reads -- no setter is called, on the fresh board or any other.
static std::map<std::string, std::string> defaultsOf(Board* fresh, const std::string& unit = "") {
    std::map<std::string, std::string> out;
    if (!fresh) return out;
    for (const auto& p : unit.empty() ? fresh->properties() : fresh->unitProperties(unit))
        if (p.get) out[p.name] = p.get().text(p.radix);
    return out;
}

// The comment CONFIG SAVE puts after a key that holds its default, or nothing. It names the
// release, because a default can change between releases and the file outlives the binary.
static std::string defaultMark(const std::map<std::string, std::string>& defaults,
                               const std::string& name, const std::string& text) {
    auto it = defaults.find(name);
    if (it == defaults.end() || it->second != text) return "";
    return std::string("  # default ") + versionNumber();
}

bool saveToml(const std::string& path, Machine& m, std::string& err) {
    // Build the text FIRST: a refused save must not have truncated the file already there.
    std::string why;
    std::string text = saveTomlText(m, &why);
    if (!why.empty()) {
        err = why;
        return false;
    }
    std::ofstream f(path);
    if (!f) {
        err = "cannot write '" + path + "'";
        return false;
    }
    f << text;
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
std::string saveTomlText(Machine& m) { return saveTomlText(m, nullptr); }

std::string saveTomlText(Machine& m, std::string* err) {
    std::ostringstream f;
    f << "[machine]\n";
    f << "name     = " << tomlString(m.name, "machine name", err) << "\n";
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
    // The startup list, plus any symbol files loaded interactively. A symbol table has no
    // board and no property, so CONFIG SAVE cannot round-trip it through the properties()
    // walk the way it does a ROM mount -- instead it re-emits the FILENAME as a SYMBOLS LOAD
    // startup command (DESIGN.md 10.3.2: round-trip the name, not the parsed table). A file
    // already named by a SYMBOLS line in `startup` is left to that line, so it is not doubled.
    std::vector<std::string> lines = m.startup;
    for (const std::string& file : m.syms.loadOrder) {
        bool already = false;
        for (const std::string& s : m.startup) {
            bool isSym = s.size() >= 7 &&
                         (s[0] == 'S' || s[0] == 's') && (s[1] == 'Y' || s[1] == 'y') &&
                         (s[2] == 'M' || s[2] == 'm');
            if (isSym && s.find(file) != std::string::npos) { already = true; break; }
        }
        if (!already) lines.push_back("SYMBOLS LOAD \"" + file + "\"");
    }
    if (!lines.empty()) {
        f << "startup  = [\n";
        for (const auto& s : lines) {
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
        f << "type = " << tomlString(b->type(), b->id + " type", err) << "\n";
        f << "id   = " << tomlString(b->id, b->id + " id", err) << "\n";
        // Straight out of properties() -- the same list SHOW prints and SET
        // writes. Round-trip is therefore structural, not something we maintain.
        //
        // A SAVE IS A READ. It calls get(), and it does not call set() -- not once, not
        // even to ask a question. This walk used to PROBE: it called each setter with the
        // value it had just got, and skipped the properties that refused. Two things were
        // wrong with that, and only the first was obvious.
        //
        // It was REDUNDANT: since 1419215 read-only IS `!p.set`, the one signal SET, SHOW,
        // MCP and the generated reference all honour, and this was the last consumer
        // deciding read-only-ness its own way.
        //
        // And IT WROTE TO THE MACHINE IT WAS DESCRIBING. A setter is not a question, and
        // some of them do work: `card` on the bankmem board rebuilds every segment (and
        // clears the live bank), so CONFIG SAVE on a banked card would have silently
        // remapped the guest's address space -- a save that MOVED MEMORY. The note that
        // used to sit below excused this walk on the grounds that a board property is "a
        // port jumper". `card` is not a port jumper, and the next board's property will
        // not be one either.
        //
        // A value that equals what a NEW board of this type starts with is marked
        // `# default X.Y.Z`, so a reader can tell a default from a setting somebody chose.
        // The comparison is against a fresh board from the registry, which is a read of a
        // different object -- the live board is still never written. The loader drops the
        // comment, so the file loads back as the same machine.
        std::unique_ptr<Board> fresh = makeBoard(b->type());
        const std::map<std::string, std::string> freshBoard = defaultsOf(fresh.get());
        for (const auto& p : b->properties()) {
            if (!p.set) continue;
            Value v = p.get();
            const std::string text = v.text(p.radix);
            if (p.kind == Kind::Str || p.kind == Kind::Enum)
                f << p.name << " = " << tomlString(text, b->id + " " + p.name, err);
            else
                f << p.name << " = " << text;
            f << defaultMark(freshBoard, p.name, text) << "\n";
        }
        // ---- Unit properties: `[board.unit.a]` ----
        //
        // Generic, over units() and unitProperties(). A card added next year that
        // declares units with settings round-trips through CONFIG SAVE with no
        // change here -- which is the whole bet the reflection layer is making.
        //
        // The body is now IDENTICAL to the board walk above, which is the point: there
        // was never a reason for the two to differ. (This one never probed -- `connect`
        // has a setter and setting it RE-RESOLVES THE ENDPOINT, tearing a terminal down
        // or rebinding a live socket. That was always the right instinct; it just was not
        // applied one loop earlier.)
        for (const auto& u : b->units()) {
            auto up = b->unitProperties(u.name);
            if (up.empty()) continue;
            f << "\n  [board.unit." << u.name << "]\n";
            const std::map<std::string, std::string> freshUnit = defaultsOf(fresh.get(), u.name);
            for (const auto& p : up) {
                if (!p.set) continue;
                Value v = p.get();
                const std::string text = v.text(p.radix);
                if (p.kind == Kind::Str || p.kind == Kind::Enum)
                    f << "  " << p.name << " = "
                      << tomlString(text, b->id + ":" + u.name + " " + p.name, err);
                else
                    f << "  " << p.name << " = " << text;
                f << defaultMark(freshUnit, p.name, text) << "\n";
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
                  << (fl.quoted ? tomlString(fl.text, b->id + " " + su.table + " " + fl.key, err)
                                : fl.text)
                  << "\n";
        }
    }
    return f.str();
}

} // namespace altair
