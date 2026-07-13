#pragma once
//
// CommandDef -- what a command IS, separate from which commands there are.
//
// IT LIVES IN core/ AND NOT cli/ FOR ONE REASON: a BOARD can declare a verb
// (Board::commands(), DESIGN.md 5.4), and a board must never include the CLI.
// The dependency runs cli -> core and only that way; the day core/board.h had to
// reach up into cli/commands.h to name this struct would be the day the layering
// inverted. So the STRUCT is here, and the TABLE of built-in commands stays up in
// cli/commands.h where it belongs, because that table is the monitor's business
// and nothing in core has any opinion about it.
//
// The fields are `const char*` on purpose. A command table -- built-in or a
// board's -- is a STATIC table of literals, and a board returning one should not
// have to allocate a string to say its own name.

namespace altair {

struct CommandDef {
    const char* name;
    bool built;          // false: resolves, and says which milestone it waits for
    const char* waiting; // "the CPU", "the debugger", ...
    const char* usage;   // one line, shown by `HELP <cmd>` and on a usage error
    const char* detail;  // the long form and the examples. May be null.
};

} // namespace altair
