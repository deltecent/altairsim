#pragma once
//
// Command names and prefix resolution (docs/cli-commands.md).
//
// THE TABLE IS THE RANKING. It is in priority order, and the FIRST command whose
// name starts with what you typed wins. That is the entire algorithm.
//
// There is no "minimum abbreviation" column and there is no priority number,
// because both would be a second thing to keep true. `D` DUMPs because DUMP is
// listed above DEPOSIT, DISASM, DISMOUNT and DISCONNECT -- so DEPOSIT needs `DE`
// and DISASM needs `DISA`, and nobody worked that out or wrote it down. Reorder
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

#include <string>
#include <vector>

namespace altair {

struct CommandDef {
    const char* name;
    bool built;          // false: resolves, and says which milestone it waits for
    const char* waiting; // "the CPU", "the debugger", ...
    const char* help;
};

// In priority order. First prefix match wins.
const std::vector<CommandDef>& commands();

// Case-insensitive prefix match. Null if nothing in the table starts with `word`.
const CommandDef* resolveCommand(const std::string& word);

} // namespace altair
