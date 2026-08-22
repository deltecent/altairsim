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
#include "host/endpoint.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
    if (!p.values.empty()) {   // a hand-written hint (a free-form Str's grammar); escape the pipes for the table
        std::string o;
        for (char c : p.values) { if (c == '|') o += "\\|"; else o += c; }
        return o;
    }
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

// A KEY WITH A SECOND SPELLING SAYS SO, in the row for the spelling that is real. The Key
// column stays the canonical name alone -- that is what SHOW prints and what CONFIG SAVE
// writes -- so a reader copying out of this table gets the same file we would have written,
// and a reader who typed the other one still finds it here (Property::aliases).
std::string alsoSpelled(const Property& p) {
    if (p.aliases.empty()) return "";
    std::string o = " *(also ";
    for (size_t i = 0; i < p.aliases.size(); i++) {
        if (i) o += ", ";
        o += "`" + p.aliases[i] + "`";
    }
    return o + ")*";
}

void propTable(std::ostream& o, std::vector<Property>& props) {
    if (props.empty()) {
        o << "*No settable properties.*\n";
        return;
    }
    o << "| Key | Kind | Default | Legal | Meaning |\n";
    o << "|---|---|---|---|---|\n";
    for (auto& p : props) {
        // A READ-ONLY PROPERTY HAS NO DEFAULT, and printing one is not merely useless.
        // Its value is whatever the running machine last made it, so what lands here is
        // whatever the machine that BUILT THE MANUAL happened to hold -- `hostdir_root`
        // would print the maintainer's build directory into every reader's copy. `—` for
        // the same reason Legal is `—`: there is nothing here you may write.
        // `—`, not a blank cell: `hostdir`'s default really IS the empty string, and the
        // two must not look alike.
        std::string def = readOnly(p) ? "—" : (p.get ? p.get().text(p.radix) : "");
        if (!def.empty() && def != "—") def = "`" + def + "`";
        std::string meaning = cell(p.help);
        if (readOnly(p)) meaning += " **(read-only — not a key you may set)**";
        if (p.irqJumper) meaning += " *(interrupt strap)*";
        meaning += alsoSpelled(p);
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
          << cell(p.help) << alsoSpelled(p) << " |\n";
    o << "\n";  // a heading may follow immediately, and pandoc wants the blank line
}

// A generator that cannot categorise something must STOP, not paper over it. The two
// groupings below are the one place a board's or a command's section lives, and the
// contract this whole file keeps is that adding one and forgetting its docs turns the
// build red -- so an unlisted name is a hard error here, not a silent "Other".
[[noreturn]] void die(const std::string& what) {
    std::cerr << "gen-reference: " << what << "\n";
    std::exit(1);
}

// ---------------------------------------------------------------------------
// Grouping the catalogue for the reader.
//
// registry.cpp lists boards in the order they were BUILT, and commands.cpp lists commands
// in PREFIX-RESOLUTION priority order -- neither is the order a reader wants to browse, and
// neither may be reordered to suit the manual (the first is nobody's job to alphabetise;
// the second IS the abbreviation algorithm, cli/commands.h). So the manual groups them by
// function and sorts by name WITHIN each group here, as a presentation over the real tables
// -- the abbreviations still come from commands()'s true order via abbreviation().
// ---------------------------------------------------------------------------

// A board's functional group. Every board type must name one (see die()).
const char* boardCategory(const std::string& n) {
    if (n == "8080" || n == "z80" || n == "8085" || n == "6800") return "CPU";
    if (n == "memory" || n == "bankmem" || n == "v2z80rom") return "Memory";
    if (n == "dcdd" || n == "mds" || n == "hdsk" || n == "versafloppy" ||
        n == "tarbell" || n == "tarbelldd" || n == "16fdc" || n == "64fdc" ||
        n == "icom" || n == "dualsd" || n == "dualide") return "Disk";
    if (n == "2sio" || n == "sio" || n == "sbc" || n == "pmmi" ||
        n == "turnkey" || n == "usio" || n == "propio" || n == "680io") return "Serial";
    if (n == "acr" || n == "uio" || n == "680kcacr") return "Tape";
    if (n == "pio" || n == "4pio" || n == "680uio" || n == "d7a" || n == "c700" || n == "lpc")
        return "Parallel and printer";
    if (n == "vdm1" || n == "dazzler" || n == "vdb8024") return "Video";
    if (n == "sol") return "Systems";  // a whole machine's I/O on one card -- more will come
    if (n == "pb1") return "PROM programmer";
    if (n == "fp" || n == "virtc" || n == "hostbridge" || n == "ss1") return "Other";
    return nullptr;
}

// The order the board groups print in.
const std::vector<std::string> kBoardOrder = {
    "CPU", "Memory", "Disk", "Serial", "Tape",
    "Parallel and printer", "Video", "Systems", "PROM programmer", "Other",
};

// A monitor command's functional group. Every built command must name one (see die()).
const char* commandGroup(const std::string& n) {
    if (n == "RUN" || n == "STEP" || n == "NEXT" || n == "RESET" || n == "POWER" ||
        n == "TYPE") return "Running the machine";
    if (n == "DUMP" || n == "EXAMINE" || n == "DEPOSIT" || n == "EDIT" || n == "FILL" ||
        n == "MOVE" || n == "SEARCH" || n == "COMPARE" || n == "LOAD" || n == "SAVE" ||
        n == "IN" || n == "OUT" || n == "REGS") return "Examining and changing memory";
    if (n == "BREAK" || n == "NOBREAK" || n == "TRACE" || n == "HISTORY" ||
        n == "DISASM" || n == "SYMBOLS" || n == "WHO") return "Debugging and tracing";
    if (n == "BOARDS" || n == "REGION" || n == "SET" || n == "SHOW" || n == "CONFIG" ||
        n == "CONSOLE" || n == "MOUNT" || n == "UNMOUNT" || n == "CONNECT" ||
        n == "DISCONNECT" || n == "SNAPSHOT" || n == "RESTORE")
        return "Configuring the machine";
    if (n == "HELP" || n == "QUIT") return "Getting help and leaving";
    return nullptr;
}

// A terse "what it does" for the quick-reference table -- the usage line is syntax, not
// meaning, and the printable sheet wants both. Every built command must have one (see
// die()): a new command with no summary stops the build rather than printing a blank cell.
// This is authored prose, the one place the cheatsheet is not a pure projection of a table;
// the guard below keeps it from silently falling behind the command set.
const char* commandSummary(const std::string& n) {
    if (n == "DUMP") return "Show memory as hex and ASCII.";
    if (n == "STEP") return "Run one instruction (or n), showing the registers after each.";
    if (n == "NEXT") return "Step one instruction, running any CALL/RST to completion.";
    if (n == "RUN") return "Start or resume the machine, optionally at an address.";
    if (n == "HISTORY") return "Replay the recent instruction (or bus-cycle) history.";
    if (n == "MOUNT") return "Put a disk or tape image into a drive; a .imd is converted to a raw .dsk beside it.";
    if (n == "BREAK") return "Set a breakpoint on an address, memory/I/O access, or tape stop.";
    if (n == "EDIT") return "Enter bytes into memory interactively from an address.";
    if (n == "CONFIG") return "Load or save the whole machine as a TOML file.";
    if (n == "SET") return "Change a property of a board, the console, display, a register, or the bus.";
    if (n == "SHOW") return "Display the state of a board, the bus, or the machine.";
    if (n == "DEPOSIT") return "Write bytes into memory at an address.";
    if (n == "EXAMINE") return "Point the front panel at an address (and show that byte).";
    if (n == "IN") return "Read a byte from an I/O port.";
    if (n == "OUT") return "Write a byte to an I/O port.";
    if (n == "LOAD") return "Load a file into memory (binary or Intel hex).";
    if (n == "SAVE") return "Write a range of memory out to a file.";
    if (n == "FILL") return "Fill a range of memory with a byte.";
    if (n == "SEARCH") return "Find bytes or a string in a range of memory.";
    if (n == "COMPARE") return "Compare a range of memory against another address.";
    if (n == "MOVE") return "Copy a range of memory to another address.";
    if (n == "WHO") return "Say which board answers an address or I/O port.";
    if (n == "BOARDS") return "List, add, or remove boards on the backplane.";
    if (n == "REGS") return "Show the CPU registers (SET REG changes one).";
    if (n == "REGION") return "Add a RAM or ROM region to a memory board.";
    if (n == "DISASM") return "Disassemble memory into instructions.";
    if (n == "SYMBOLS") return "Load or clear a symbol table for disassembly.";
    if (n == "UNMOUNT") return "Take a disk or tape out of a drive.";
    if (n == "DISCONNECT") return "Unplug the endpoint from a serial unit.";
    if (n == "CONSOLE") return "Show or set the host console's properties.";
    if (n == "CONNECT") return "Attach a serial unit to an endpoint (console, socket, file, ...).";
    if (n == "RESET") return "Reset the machine, keeping RAM (RESET CPU resets just the processor).";
    if (n == "POWER") return "Power-cycle the machine -- the only thing that clears RAM.";
    if (n == "TRACE") return "Log every bus cycle while the machine runs.";
    if (n == "TYPE") return "Feed text to the guest as if typed at its keyboard.";
    if (n == "SNAPSHOT") return "Save the whole machine state to a file.";
    if (n == "RESTORE") return "Load machine state back from a snapshot.";
    if (n == "NOBREAK") return "Remove a breakpoint, or all of them.";
    if (n == "HELP") return "Show help for a command.";
    if (n == "QUIT") return "Leave the simulator.";
    return nullptr;
}

// The order the command groups print in.
const std::vector<std::string> kCommandOrder = {
    "Running the machine", "Examining and changing memory", "Debugging and tracing",
    "Configuring the machine", "Getting help and leaving",
};

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
         "printed in each property's own base.\n\n"
         "The catalogue is **grouped by function** — CPU, memory, disk, serial, and so on —\n"
         "and within a group the boards are in **alphabetical order**.\n\n";

    // Bucket the registry by group, keeping group order (kBoardOrder) and sorting each
    // group by name. An unlisted board is a hard error -- boardCategory() returns null and
    // this stops, rather than dropping a board out of the manual silently.
    std::vector<BoardType> types = boardTypes();
    auto byName = [](const BoardType& a, const BoardType& b) { return a.name < b.name; };
    auto inGroup = [&](const std::string& g) {
        std::vector<BoardType> v;
        for (const auto& t : types) {
            const char* c = boardCategory(t.name);
            if (!c) die("board '" + t.name + "' has no category -- add it to boardCategory()");
            if (g == c) v.push_back(t);
        }
        std::sort(v.begin(), v.end(), byName);
        return v;
    };

    // The index, one small table per group.
    for (const auto& g : kBoardOrder) {
        auto group = inGroup(g);
        if (group.empty()) continue;
        o << "**" << g << "**\n\n| Type | What it is |\n|---|---|\n";
        for (const auto& t : group)
            o << "| [`" << t.name << "`](#" << t.name << ") | " << cell(t.description) << " |\n";
        o << "\n";
    }

    // The detail, same grouping and order. A group is an `##` section; each board an `###`
    // under it, so its anchor stays `#<name>` (the index links to it) and its own tables
    // nest one level deeper.
    for (const auto& g : kBoardOrder) {
        auto group = inGroup(g);
        if (group.empty()) continue;
        o << "\n## " << g << "\n";

        for (const auto& t : group) {
            auto b = makeBoard(t.name);
            if (!b) continue;
            o << "\n### `" << t.name << "`\n\n" << t.description << "\n\n";

            auto units = b->units();
            if (!units.empty()) {
                o << "**Units:** ";
                for (size_t i = 0; i < units.size(); i++) {
                    if (i) o << ", ";
                    // Name the unit's kind and, after it, the verb that fills it --
                    // `(serial, CONNECT)` -- matching what `SHOW BOARD <type> UNITS`
                    // prints. A cpu core takes neither, so it shows only its kind.
                    o << "`" << units[i].name << "` (" << unitKindName(units[i].kind);
                    if (const char* v = unitKindVerb(units[i].kind); v && *v) o << ", " << v;
                    o << ")";
                }
                o << "\n\n";
            }

            // A card that owns a LIST -- drives on a controller, regions on a memory card --
            // documents that list's keys, not merely that it takes one. `readonly` on a
            // drive was real and worked and appeared in NO reference; that was bug #9.
            for (const auto& s : b->subUnitTables()) {
                auto sp = b->subUnitProperties(s);
                o << "#### `[[board." << s << "]]` — a list you may add\n\n";
                if (sp.empty())
                    o << "*This card takes a `[[board." << s << "]]` list.*\n\n";
                else
                    schemaTable(o, sp);
            }

            auto props = b->properties();
            o << "#### Board properties\n\n";
            propTable(o, props);

            for (const auto& u : units) {
                auto up = b->unitProperties(u.name);
                if (up.empty()) continue;
                o << "\n#### Unit `" << u.name << "` — `[board.unit." << u.name << "]`\n\n";
                propTable(o, up);
            }
            o << "\n";
        }
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

    // Expand the same `{endpoints}` token the live HELP printer does (monitor.cpp), so
    // the manual shows the real grammar instead of the literal token. all=true forces the
    // full, platform-independent list -- see endpointHelp() -- so this file stays byte-
    // identical across builds and the docs-reference check cannot break on the printer flag.
    std::string text = detail;
    const std::string tok = "{endpoints}";
    for (size_t at = text.find(tok); at != std::string::npos; at = text.find(tok, at))
        text.replace(at, tok.size(), endpointHelp(true));

    std::istringstream in(text);
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
         "**`.` repeats your last command.** It runs quietly, with no echo, so a single\n"
         "keystroke walks the continuing verbs forward: `DI` disassembles a screenful, then\n"
         "`.` `.` `.` keeps going; the same holds for `DUMP` and `STEP`.\n\n"
         "**Numbers:** on the wire is **hex** (addresses, ports, bytes); never on the wire is\n"
         "**decimal** (counts, widths, sizes). `0x`/`$`/`h` force hex, `0o`/trailing-`q` force\n"
         "octal, `#` forces decimal, and a `K`/`M` suffix is always decimal. `SET CONSOLE\n"
         "base=octal` makes the wire class read and print in **split octal** (each byte its own\n"
         "`000`–`377` group, an address as two of them) — the MITS front-panel convention;\n"
         "`base=hex` is the default. Either way both spellings stay typeable.\n\n"
         "The commands are **grouped by what they do**, and within a group they are in\n"
         "**alphabetical order**. The abbreviation beside each name is still derived from the\n"
         "master table's priority order, not from this listing, so it is what you type\n"
         "regardless of where the command sits here.\n\n";

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

    // Every built command names a group; an unlisted one is a hard error (see die()), so a
    // new command cannot slip into the manual uncategorised.
    for (const auto& c : commands())
        if (c.built && !commandGroup(c.name))
            die("command '" + std::string(c.name) + "' has no group -- add it to commandGroup()");

    // Grouped, and alphabetical within each group. Each group is an `##` section; each
    // command an `###` under it, so the abbreviation heading nests correctly.
    for (const auto& g : kCommandOrder) {
        std::vector<const CommandDef*> inGroup;
        for (const auto& c : commands())
            if (c.built && g == commandGroup(c.name)) inGroup.push_back(&c);
        std::sort(inGroup.begin(), inGroup.end(),
                  [](const CommandDef* a, const CommandDef* b) {
                      return std::string(a->name) < b->name;
                  });
        if (inGroup.empty()) continue;

        o << "## " << g << "\n";
        for (const auto* c : inGroup) {
            o << "\n### " << c->name << " — `" << abbreviation(*c) << "`\n\n";
            o << "```\n" << c->usage << "\n```\n";
            detailBlock(o, c->detail);
            o << "\n";
        }
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

    o << "## Editing the command line\n\n"
         "`Tab` completes what you are typing — a command, then a board id, then its property "
         "names, then a property's values (`SET mem0 fill=` then `Tab`); a second `Tab` lists "
         "the choices. `Up`/`Down` walk the command history, saved per directory in "
         "`.altairsim_history`.\n\n"
         "| Key | Does |\n|---|---|\n"
         "| `Ctrl-A` / `Ctrl-E` (or `Home` / `End`) | start / end of line |\n"
         "| `Alt-B` / `Alt-F` (or `Ctrl-Left` / `Ctrl-Right`) | back / forward one word |\n"
         "| `Ctrl-W` / `Ctrl-K` / `Ctrl-U` | erase word behind / to end of line / whole line |\n"
         "| `Backspace` / `Delete` | erase before / under the cursor |\n\n";

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
         "  -v, --version      print the version and exit.\n"
         "  -h, --help         print this help and exit.\n"
         "```\n\n";

    bool anyUnbuilt = false;
    for (const auto& c : commands())
        if (!c.built) anyUnbuilt = true;

    o << "## Monitor commands\n\n"
         "Type the part before the bracket.";
    // The `*` legend only earns its line when a command is actually starred -- off the
    // same `built` flag the table reads, so the two can never disagree.
    if (anyUnbuilt) o << " `*` = resolves, but not built yet.";
    // Alphabetical, for browsing -- a copy sorted by name. The abbreviation still comes from
    // abbreviation(), which reads commands()'s TRUE priority order, so the shortcut shown is
    // unaffected by this presentation sort (the same split the boards table makes).
    std::vector<CommandDef> cmds(commands().begin(), commands().end());
    std::sort(cmds.begin(), cmds.end(),
              [](const CommandDef& a, const CommandDef& b) {
                  return std::string(a.name) < std::string(b.name);
              });
    o << "\n\n| Command | Does | Usage |\n|---|---|---|\n";
    for (const auto& c : cmds) {
        const char* s = commandSummary(c.name);
        if (!s) die(std::string("command '") + c.name + "' has no summary -- add it to commandSummary()");
        o << "| `" << abbreviation(c) << "`" << (c.built ? "" : " \\*") << " | " << cell(s)
          << " | `" << cell(c.usage) << "` |\n";
    }
    o << "\n";

    // Grouped by function and alphabetised within each group -- the same presentation as
    // the boards chapter, over the same tables. registry order is build order, which is not
    // a reader's order, so bucket by boardCategory() (a null category is a hard error there).
    o << "## Boards\n\n";
    {
        std::vector<BoardType> types = boardTypes();
        for (const auto& g : kBoardOrder) {
            std::vector<BoardType> group;
            for (const auto& t : types) {
                const char* c = boardCategory(t.name);
                if (!c) die("board '" + t.name + "' has no category -- add it to boardCategory()");
                if (g == c) group.push_back(t);
            }
            if (group.empty()) continue;
            std::sort(group.begin(), group.end(),
                      [](const BoardType& a, const BoardType& b) { return a.name < b.name; });
            o << "**" << g << "**\n\n| Type | What it is |\n|---|---|\n";
            for (const auto& t : group)
                o << "| `" << t.name << "` | " << cell(t.description) << " |\n";
            o << "\n";
        }
    }

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
         "base      = octal          # read/print the wire class in split octal (MITS style)\n"
         "```\n\n"
         "**Paths:** a path *inside* a machine file is relative to **that file**. A path you\n"
         "*type* is relative to **your shell**.\n\n";

    o << "## Endpoints — `CONNECT <id>:<unit> <endpoint>`\n\n"
         "| Endpoint | Is |\n|---|---|\n"
         "| `console` | the host terminal. Exactly one unit may hold it. |\n"
         "| `null` | nowhere. Writes vanish, reads never come. |\n"
         "| `loopback` | itself — what you write comes back. |\n"
         "| `scripted` | a caller in place of a human — what MCP and the tests type into. |\n"
         "| `socket:PORT` | **listens** — this is telnet-in. |\n"
         "| `socket:HOST:PORT` | **calls out**. |\n"
         "| `serial:DEVICE` | a real serial port on this host. |\n"
         "| `in:PATH` | a host file as a reader (paper tape). `?cps=N` paces it. |\n"
         "| `out:PATH` | a host file as a punch — 8-bit clean, never truncating. |\n"
         "| `terminal` | a window the simulator draws itself (SDL builds). "
         "`?emulation=vt100\\|adm3a\\|vt52\\|h19`, `?size=COLSxROWS`. |\n"
         "| `printer:QUEUE` | a real print queue on this host. |\n"
         "| `<endpoint>\\|FILE` | a tap: append `\\|FILE` to any endpoint to also log the line. |\n\n";
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
