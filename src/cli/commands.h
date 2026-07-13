#pragma once
//
// Command names and prefix resolution (docs/cli-commands.md).
//
// THE TABLE IS THE RANKING. It is in priority order, and the FIRST command whose
// name starts with what you typed wins. That is the entire algorithm.
//
// There is no "minimum abbreviation" column and there is no priority number,
// because both would be a second thing to keep true. `D` DUMPs because DUMP is
// listed above DEPOSIT, DISASM and DISCONNECT -- so DEPOSIT needs `DE` and DISASM
// needs `DI`, and nobody worked that out or wrote it down. Reorder
// the table and every abbreviation in the monitor re-derives itself. Nothing
// singles out single-character commands; one letter is just a short prefix.
//
// THE ONE INVARIANT: no command name may be a strict prefix of another. If one
// were, its full correctly-spelled name would resolve to whichever is listed
// first, and there would be no way left to type the other. Renaming REGS to REG
// would break exactly this, and tests/test_cli.cpp fails if anyone does.
//
// EVERY command is in this table, INCLUDING the ones that do not exist yet. That
// is the point: if only the built commands were listed, `S` would mean SHOW today
// and STEP the day the CPU lands, and a user's fingers would silently start doing
// something else. Reserved commands resolve, then say what they are waiting for.
// Abbreviations are a contract, and the contract is fixed now.

#include "core/command.h"  // CommandDef itself -- a BOARD can declare one, so it lives in core

#include <string>
#include <vector>

namespace altair {

// In priority order. First prefix match wins.
const std::vector<CommandDef>& commands();

// Case-insensitive prefix match. Null if nothing in the table starts with `word`.
const CommandDef* resolveCommand(const std::string& word);

// The shortest prefix that resolves back to this command -- DERIVED, never stored,
// so it is right by construction and stays right when the table is reordered.
// Returns the whole name marked up: "D[UMP]", "DE[POSIT]", "GO".
std::string abbreviation(const CommandDef& c);

// The same, for a verb a BOARD brought with it (Board::commands()) -- and it is the
// MIRROR IMAGE of the rule above, which is why it cannot be the same function.
//
// A built-in is reached by MATCHING the table. A board verb is reached by the table
// FAILING to match, because the monitor asks the cards only after the static menu has
// said no. So a board verb's abbreviation is the shortest prefix that resolves to
// NOTHING: `REW[IND]`, because RESET already answers to R, RE and RES.
//
// If every prefix is shadowed -- including the full name -- the verb is UNREACHABLE
// and this returns the bare name with no brackets. That is a bug in the BOARD, not in
// a user's typing, so it is caught by tests/test_cli.cpp as a merge gate rather than
// at runtime: a user cannot create one, only a board author can.
std::string boardAbbreviation(const CommandDef& c);

} // namespace altair
