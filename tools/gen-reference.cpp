// gen-reference -- the manual's reference chapters, emitted from the binary.
//
// WHY THIS IS A PROGRAM AND NOT A HAND-WRITTEN CHAPTER.
//
// A board's properties() ARE its TOML keys (DESIGN.md 5). One reflection layer already
// backs SET, SHOW, the TOML loader, CONFIG SAVE, the MCP tool schemas and tab completion,
// and the project's whole claim is that there is NO SECOND SCHEMA ANYWHERE. A parameter
// table typed into a Markdown file would be exactly that second schema -- and it would be
// wrong immediately. It was: while planning this manual, a careful reader with
// s100-memory.cpp open wrote a defaults table for the memory card that was wrong in three
// of eight rows (honors_phantom, phantom and fill are all/all/random, not none/none/zero).
//
// So the reference is PRINTED, not retyped. This is the fourth consumer of the reflection
// layer, and it is modelled on the third -- src/mcp/server.cpp already builds MCP schemas
// out of properties() the same way.
//
// WHY IT IS C++ AND NOT A SHELL SCRIPT OVER THE CLI. No single command emits what a table
// needs: `BOARDS TYPES` gives a property's name and help but no default and no range, and
// `SHOW <id>` gives its value and range but no help. A script would have to rejoin two
// lossy TEXT projections of structs we already hold in memory -- the second-schema sin
// again, one level up, in the least testable place available.
//
// The output is committed under docs/manual/ref/, and a ctest re-runs this and diffs. Edit
// a properties() and forget the docs, and the suite goes red.
//
//   gen-reference <outdir>

#include "boards/registry.h"
#include "cli/commands.h"
#include "core/board.h"
#include "core/machines.h"
#include "core/value.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace altair;

namespace {

// The banner that goes at the top of every generated chapter.
//
// NOTE WHAT IT DOES NOT SAY. It names no source file, no build target and no test -- because
// this file SHIPS, inside the manual, to a reader who has none of those things. How to
// regenerate it is a fact about the repository, and it lives in the Developer Guide where the
// repository is. (The self-containment test, tests/acceptance/docs-manual.cmake, catches this
// if it creeps back -- it caught exactly this banner once already.)
const char* kDoNotEdit =
    "<!-- GENERATED FROM THE PROGRAM ITSELF. Do not edit by hand.\n"
    "     Every default, range and description below is printed from the same tables the\n"
    "     monitor resolves against, so it cannot disagree with the program you are running. -->\n";

// A cell that is about to go into a GFM table. `|` would end the column and a newline
// would end the row, so both have to go -- and a help string is allowed to contain either.
std::string cell(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '|')
            o += "\\|";
        else if (c == '\n')
            o += ' ';
        else
            o += c;
    }
    return o;
}

// What a property will ACCEPT -- rendered in the property's own radix, so the legal column
// and the default column read in the same base. A port whose default prints as 0x10 must
// not advertise its range in decimal.
std::string legal(const Property& p) {
    if (p.kind == Kind::Bool) return "`on` \\| `off`";
    if (p.kind == Kind::Enum) {
        std::string o;
        for (size_t i = 0; i < p.choices.size(); i++) {
            if (i) o += " \\| ";
            o += "`" + p.choices[i] + "`";
        }
        return o;
    }
    if (p.kind == Kind::Int) {
        if (p.min == p.max) return "any";  // min==max means unbounded (value.h)
        return "`" + Value::ofInt(p.min).text(p.radix) + "` .. `" +
               Value::ofInt(p.max).text(p.radix) + "`";
    }
    return "text";
}

std::string kindName(Kind k) {
    switch (k) {
    case Kind::Int: return "int";
    case Kind::Bool: return "bool";
    case Kind::Str: return "string";
    case Kind::Enum: return "enum";
    }
    return "?";
}

// A property with no setter is LIVE STATE, not a jumper: `lines` on a 6850, `current_level`
// on an 88-VI. CONFIG SAVE already skips them, and a manual that listed them as things you
// may put in a TOML file would be lying in the same way.
bool readOnly(const Property& p) { return !p.set; }

void propTable(std::ostream& o, std::vector<Property>& props) {
    if (props.empty()) {
        o << "*No settable properties.*\n";
        return;
    }
    o << "| Key | Kind | Default | Legal | Meaning |\n";
    o << "|---|---|---|---|---|\n";
    for (auto& p : props) {
        std::string def = p.get ? p.get().text(p.radix) : "";
        if (!def.empty()) def = "`" + def + "`";
        std::string meaning = cell(p.help);
        if (readOnly(p)) meaning += " **(read-only — not a key you may set)**";
        if (p.irqJumper) meaning += " *(interrupt strap)*";
        o << "| `" << p.name << "` | " << kindName(p.kind) << " | " << def << " | "
          << (readOnly(p) ? "—" : legal(p)) << " | " << meaning << " |\n";
    }
}

// The keys of a SUB-UNIT table -- [[board.drive]], [[board.region]]. These are the
// DESCRIPTION half of a Property (kind, choices, range, radix, help) with no accessor at
// all, because the drive or region they describe does not exist until the table is read
// and it is built (Board::subUnitProperties). So there is no Default column: there is
// nothing to read a default off. And crucially there is NO read-only mark -- propTable
// stamps that on `!p.set`, and one of these has no setter EITHER, which would print every
// one of them "(read-only -- not a key you may set)" when in a machine file they are
// nothing BUT settable. The monitor drew exactly this line, showSchema() beside showProps()
// (src/cli/monitor.cpp); this is the same split, one consumer over.
void schemaTable(std::ostream& o, std::vector<Property>& props) {
    o << "| Key | Kind | Legal | Meaning |\n";
    o << "|---|---|---|---|\n";
    for (auto& p : props)
        o << "| `" << p.name << "` | " << kindName(p.kind) << " | " << legal(p) << " | "
          << cell(p.help) << " |\n";
    o << "\n";  // a heading may follow immediately, and pandoc wants the blank line
}

// ---------------------------------------------------------------------------
// ref/boards.md
// ---------------------------------------------------------------------------
void boards(const std::string& dir) {
    std::ofstream o(dir + "/boards.md");
    o << kDoNotEdit
      << "\n# Boards and their parameters\n\n"
         "Every key below is a key you may write in a machine file, and the *same* key you\n"
         "may `SET` at the monitor prompt. That is not a coincidence and it is not a\n"
         "convention: a board's properties **are** its TOML schema, so there is nothing here\n"
         "that could disagree with the program.\n\n"
         "Numbers follow the one rule: **on the wire → hex, never on the wire → decimal.**\n"
         "A port is hex; a baud rate and a drive count are decimal. The defaults below are\n"
         "printed in each property's own base.\n\n";

    o << "| Type | What it is |\n|---|---|\n";
    for (const auto& t : boardTypes())
        o << "| [`" << t.name << "`](#" << t.name << ") | " << cell(t.description) << " |\n";
    o << "\n";

    for (const auto& t : boardTypes()) {
        auto b = makeBoard(t.name);
        if (!b) continue;
        o << "\n## `" << t.name << "`\n\n" << t.description << "\n\n";

        auto units = b->units();
        if (!units.empty()) {
            o << "**Units:** ";
            for (size_t i = 0; i < units.size(); i++) {
                if (i) o << ", ";
                o << "`" << units[i].name << "` (" << unitKindName(units[i].kind) << ")";
            }
            o << "\n\n";
        }

        // A card that owns a LIST -- drives on a controller, regions on a memory card --
        // documents that list's keys, not merely that it takes one. `readonly` on a drive
        // was real and worked and appeared in NO reference; that was the whole of bug #9.
        for (const auto& s : b->subUnitTables()) {
            auto sp = b->subUnitProperties(s);
            o << "### `[[board." << s << "]]` — a list you may add\n\n";
            if (sp.empty())
                o << "*This card takes a `[[board." << s << "]]` list.*\n\n";
            else
                schemaTable(o, sp);
        }

        auto props = b->properties();
        o << "### Board properties\n\n";
        propTable(o, props);

        for (const auto& u : units) {
            auto up = b->unitProperties(u.name);
            if (up.empty()) continue;
            o << "\n### Unit `" << u.name << "` — `[board.unit." << u.name << "]`\n\n";
            propTable(o, up);
        }
        o << "\n";
    }
}

// ---------------------------------------------------------------------------
// ref/commands.md
//
// The help text in CommandDef is already written for a reader -- it is what `HELP DUMP`
// prints -- so the job here is layout, not prose. Its examples are indented two spaces;
// a run of those becomes a fenced block, and everything else is a paragraph.
// ---------------------------------------------------------------------------
void detailBlock(std::ostream& o, const char* detail) {
    if (!detail) return;
    std::istringstream in(detail);
    std::string line;
    bool fenced = false;
    while (std::getline(in, line)) {
        bool example = line.size() > 2 && line[0] == ' ' && line[1] == ' ';
        if (example && !fenced) {
            o << "\n```\n";
            fenced = true;
        } else if (!example && fenced) {
            o << "```\n\n";
            fenced = false;
        }
        if (fenced)
            o << line.substr(2) << "\n";
        else
            o << line << "\n";
    }
    if (fenced) o << "```\n";
}

void commandsDoc(const std::string& dir) {
    std::ofstream o(dir + "/commands.md");
    o << kDoNotEdit
      << "\n# Every monitor command\n\n"
         "**Commands resolve by prefix, and the first match wins.** There are no aliases and\n"
         "no fixed abbreviations: the shortest prefix that reaches a command is derived from\n"
         "the table's order, so it is shown here as `D[UMP]` — type the part before the\n"
         "bracket. (`?` is the one true alias, for `HELP`.)\n\n"
         "**Numbers:** on the wire is **hex** (addresses, ports, bytes); never on the wire is\n"
         "**decimal** (counts, widths, sizes). `0x`/`$`/`h` force hex, `#` forces decimal, and\n"
         "a `K`/`M` suffix is always decimal.\n\n";

    // The reserved ones, up front. They RESOLVE but do not run -- which is the honest
    // answer to "what does it not do yet", and it comes straight off the `built` flag
    // rather than out of somebody's memory.
    std::vector<const CommandDef*> unbuilt;
    for (const auto& c : commands())
        if (!c.built) unbuilt.push_back(&c);

    if (!unbuilt.empty()) {
        o << "## Not built yet\n\n"
             "These **resolve** — they take their abbreviation today, so it cannot change\n"
             "under your fingers when they land — and they tell you what they are waiting on.\n\n"
             "| Command | Waiting on |\n|---|---|\n";
        for (const auto* c : unbuilt)
            o << "| `" << abbreviation(*c) << "` | " << cell(c->waiting ? c->waiting : "") << " |\n";
        o << "\n";
    }

    o << "## The commands\n\n";
    for (const auto& c : commands()) {
        if (!c.built) continue;
        o << "\n### " << c.name << " — `" << abbreviation(c) << "`\n\n";
        o << "```\n" << c.usage << "\n```\n";
        detailBlock(o, c.detail);
        o << "\n";
    }
}

// ---------------------------------------------------------------------------
// ref/machines.md -- the built-in machines. A built-in IS a TOML file, compiled in.
// ---------------------------------------------------------------------------
void machinesDoc(const std::string& dir) {
    std::ofstream o(dir + "/machines.md");
    o << kDoNotEdit
      << "\n# The built-in machines\n\n"
         "A built-in is a machine file that lives **inside the binary** — the same format you\n"
         "would write yourself, with nothing special about it. Name one and you get it in any\n"
         "directory on earth:\n\n"
         "```\n$ altairsim basic4k\n```\n\n"
         "`altairsim --list` prints this table, and `altairsim -x 'SHOW MACHINE' <name>` shows\n"
         "what is actually in one.\n\n"
         "| Machine | What it is |\n|---|---|\n";
    for (const auto& m : builtinMachines())
        o << "| `" << m.name << "` | " << cell(m.blurb) << " |\n";
    o << "\n";
}

// ---------------------------------------------------------------------------
// ref/cheatsheet.md -- the printable page.
//
// It is a PROJECTION of the same data, not a hand-written summary of it. A summary you
// type is a second schema wearing a smaller hat: it would drift from the chapters it
// summarises, and it would drift silently, because nothing would ever check it.
// ---------------------------------------------------------------------------
void cheatsheet(const std::string& dir) {
    std::ofstream o(dir + "/cheatsheet.md");
    o << kDoNotEdit << "\n# Quick reference\n\n";

    o << "## Getting out, and back in\n\n"
         "| Key | Does |\n|---|---|\n"
         "| `^E` | **ATTN** — stop the machine and take the keyboard back. Nothing is lost. |\n"
         "| `RUN` | Resume, at the exact instruction it stopped on. |\n"
         "| `QUIT` | Leave. (There is no `EXIT`.) |\n\n";

    o << "## Command line\n\n"
         "```\n"
         "altairsim [options] [machine]\n"
         "\n"
         "  machine            a built-in name, or a config file (has a '/' or ends .toml).\n"
         "                     Omitted: ./altairsim.toml if there is one, else `default`.\n"
         "  -m, --machine <n>  ALWAYS a built-in name -- never a file.\n"
         "  -f, --file <path>  ALWAYS a file -- never a built-in name.\n"
         "  -n, --none         empty backplane: no boards, no memory, nothing.\n"
         "  -l, --list         list the built-in machines and exit.\n"
         "  -s, --script <f>   run a command script, then exit with its status.\n"
         "  -x, --exec <cmd>   run one monitor command (repeatable), then exit.\n"
         "  -i, --interactive  after --script/--exec, stay in the monitor.\n"
         "      --mcp          MCP server on stdio.\n"
         "  -v, --version      -h, --help\n"
         "```\n\n";

    o << "## Monitor commands\n\n"
         "Type the part before the bracket. `*` = resolves, but not built yet.\n\n"
         "| Command | Usage |\n|---|---|\n";
    for (const auto& c : commands())
        o << "| `" << abbreviation(c) << "`" << (c.built ? "" : " \\*") << " | `" << cell(c.usage)
          << "` |\n";
    o << "\n";

    o << "## Boards\n\n| Type | What it is |\n|---|---|\n";
    for (const auto& t : boardTypes())
        o << "| `" << t.name << "` | " << cell(t.description) << " |\n";
    o << "\n";

    o << "## Machines\n\n| Machine | What it is |\n|---|---|\n";
    for (const auto& m : builtinMachines())
        o << "| `" << m.name << "` | " << cell(m.blurb) << " |\n";
    o << "\n";

    o << "## A machine file, in one look\n\n"
         "```toml\n"
         "[machine]\n"
         "name    = \"mine\"\n"
         "base    = \"default\"        # start from a machine, and say what is DIFFERENT\n"
         "startup = [\"RUN FF00\"]     # the operator's own keystrokes. There is no BOOT verb.\n"
         "\n"
         "[[board]]                  # type + a NEW id      -> ADD the card\n"
         "type = \"2sio\"              # type + an id from the base -> REPLACE it outright\n"
         "id   = \"sio0\"              # NO type + an id      -> MODIFY the one already there\n"
         "port = 10                  # remove = true        -> PULL THE CARD OUT\n"
         "\n"
         "  [board.unit.a]           # a unit's own settings\n"
         "  connect = \"console\"\n"
         "\n"
         "  [[board.region]]         # a list the card owns (memory)\n"
         "  type = \"ram\"\n"
         "  at   = 0000              # hex: it is an address\n"
         "  size = \"56K\"             # decimal: it is a size\n"
         "\n"
         "  [[board.drive]]          # a list the card owns (disk controllers)\n"
         "  unit  = 0\n"
         "  mount = \"cpm.dsk\"        # relative to THIS FILE\n"
         "\n"
         "[console]                  # the HOST's terminal -- not a board\n"
         "strip7out = true\n"
         "```\n\n"
         "**Paths:** a path *inside* a machine file is relative to **that file**. A path you\n"
         "*type* is relative to **your shell**.\n\n";

    o << "## Endpoints — `CONNECT <id>:<unit> <endpoint>`\n\n"
         "| Endpoint | Is |\n|---|---|\n"
         "| `console` | the host terminal. Exactly one unit may hold it. |\n"
         "| `null` | nowhere. Writes vanish, reads never come. |\n"
         "| `loopback` | itself — what you write comes back. |\n"
         "| `socket:PORT` | **listens** — this is telnet-in. |\n"
         "| `socket:HOST:PORT` | **calls out**. |\n"
         "| `serial:DEVICE` | a real serial port on this host. |\n\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: gen-reference <outdir>\n";
        return 2;
    }
    const std::string dir = argv[1];

    boards(dir);
    commandsDoc(dir);
    machinesDoc(dir);
    cheatsheet(dir);

    // A silent failure here would commit an empty chapter, and the diff test would then
    // happily hold it stable forever.
    for (const char* f : {"boards.md", "commands.md", "machines.md", "cheatsheet.md"}) {
        std::ifstream in(dir + "/" + f, std::ios::ate);
        if (!in || in.tellg() < 200) {
            std::cerr << "gen-reference: " << dir << "/" << f << " did not get written\n";
            return 1;
        }
    }
    return 0;
}
