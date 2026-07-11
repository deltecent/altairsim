#include "cli/commands.h"

#include <cctype>

namespace altair {

// ---------------------------------------------------------------------------
// THE RANKING. Read it top to bottom: that IS the priority.
//
// The nine that own their prefix are Patrick's, 2026-07-11:
//   DUMP, STEP, RESET, HISTORY, MOUNT, BREAK, EDIT, CONFIG, GO
//
// `D` is DUMP, which is what a ROM monitor's `D` has always been. SIMH made `D`
// DEPOSIT and `E` EXAMINE; this breaks with it deliberately. It also puts the
// shortest key on the keyboard on the command that cannot destroy anything, and
// makes you type two letters to change memory. DEPOSIT keeps the front panel's
// word -- it just costs `DE`.
//
// `built = false` means the command RESOLVES but does not run yet, and says so.
// That is on purpose: `S` must mean STEP from the first day, so that it does not
// mean SHOW until the CPU lands and then quietly change under someone's fingers.
// ---------------------------------------------------------------------------
static const std::vector<CommandDef> kCommands = {
    // ---- the nine that win their prefix ----
    {"DUMP",       true,  nullptr, "DUMP [<addr>|<range>] [WIDTH=16]  -- an addr dumps 256 bytes"},
    {"STEP",       false, "the CPU", "STEP [n]"},
    {"RESET",      true,  nullptr, "RESET [CPU]"},
    {"HISTORY",    false, "the debugger", "HISTORY [n]"},
    {"MOUNT",      true,  nullptr, "MOUNT <id>:<u> <file> [RO]"},
    {"BREAK",      false, "the debugger", "BREAK <addr> [IF <expr>] | BREAK IO <port>"},
    {"EDIT",       false, "the line editor", "EDIT <addr> -- interactive; Enter advances"},
    {"CONFIG",     true,  nullptr, "CONFIG LOAD <f.toml> | CONFIG SAVE <f.toml>"},
    {"GO",         false, "the CPU", "GO [addr]"},

    // ---- everything else, ranked by how often you type it ----
    {"SET",        true,  nullptr, "SET <id> <k>=<v>"},              // SE  (beats SEARCH)
    {"SHOW",       true,  nullptr, "SHOW <id>|BUS [MAP|IO|CONTENTION]|ROMS|MACHINE"},
    {"DEPOSIT",    true,  nullptr, "DEPOSIT <addr> <bytes...>"},     // DE
    // EXAMINE and DEPOSIT are the two switches on the front panel, and they belong
    // together -- DE and EX. EXAMINE is the quick look at ONE byte; bare EXAMINE
    // steps to the next, which is the panel's EXAMINE NEXT.
    {"EXAMINE",    true,  nullptr, "EXAMINE [<addr>]   -- one byte; bare = EXAMINE NEXT"},  // EX
    {"IN",         true,  nullptr, "IN <port>          -- run a real IN cycle"},        // I
    {"OUT",        true,  nullptr, "OUT <port> <byte>  -- run a real OUT cycle"},       // O
    {"LOAD",       true,  nullptr, "LOAD <file> [AT <addr>] [FORMAT=BIN|HEX] [RAW <id>]"},
    {"SAVE",       true,  nullptr, "SAVE <file> <range> [FORMAT=BIN|HEX] [RAW <id>]"},
    {"FILL",       true,  nullptr, "FILL <range> <byte>"},
    {"SEARCH",     true,  nullptr, "SEARCH <range> <bytes...>|\"str\""},  // SEA
    {"COMPARE",    true,  nullptr, "COMPARE <range> <addr>|<file>"},  // COM
    {"MOVE",       true,  nullptr, "MOVE <range> <dest>"},            // MOV
    {"WHO",        true,  nullptr, "WHO <addr> | WHO IO <port>"},
    {"BOARD",      true,  nullptr, "BOARD LIST|TYPES|ADD <type> <id> [k=v...]|REMOVE <id>"},  // BO
    {"REGS",       false, "the CPU", "REGS | SET REG <r>=<v>"},       // REG (beats REGION)
    {"REGION",     true,  nullptr, "REGION ADD <id> type=ram|rom at=<addr> [size=|mount=]"},  // REGI
    {"DISASM",     false, "the CPU", "DISASM <range>|<addr> [n]"},    // DIS
    {"DISMOUNT",   true,  nullptr, "DISMOUNT <id>:<u>"},              // DISM
    {"DISCONNECT", false, "the serial boards", "DISCONNECT <id>:<u>"},// DISC
    {"CONSOLE",    false, "the serial boards", "CONSOLE -- enter it; ATTN returns"},  // CONS
    {"CONNECT",    false, "the serial boards", "CONNECT <id>:<u> <endpoint>"},        // CONN
    {"POWER",      true,  nullptr, "POWER -- power cycle. The only thing that loses RAM."},
    {"TRACE",      false, "the debugger", "TRACE ON|OFF [file] [MASK=...]"},
    {"STOP",       false, "the CPU", "STOP"},                         // STO
    {"SNAPSHOT",   false, "the debugger", "SNAPSHOT <file>"},         // SN
    {"RESTORE",    false, "the debugger", "RESTORE <file>"},          // REST
    {"RECORD",     false, "the debugger", "RECORD <file>"},           // REC
    {"REPLAY",     false, "the debugger", "REPLAY <file>"},           // REP
    {"NOBREAK",    false, "the debugger", "NOBREAK"},
    {"HELP",       true,  nullptr, "HELP  (or `?`)"},                 // HE  (HISTORY has H)
    // There is no EXIT. QUIT is the one word for leaving, because two words for one
    // action is two things to learn and nothing gained -- and EXIT was also the only
    // reason EXAMINE could not simply be `EX`.
    {"QUIT",       true,  nullptr, "QUIT"},
};

const std::vector<CommandDef>& commands() { return kCommands; }

const CommandDef* resolveCommand(const std::string& word) {
    if (word.empty()) return nullptr;

    std::string w;
    for (char ch : word) w += (char)std::toupper((unsigned char)ch);

    // First match wins. Nothing here treats a one-letter word specially -- it is
    // just a short prefix, and it lands on whatever is highest in the table.
    for (const CommandDef& c : kCommands) {
        std::string name = c.name;
        if (name.compare(0, w.size(), w) == 0) return &c;
    }
    return nullptr;
}

} // namespace altair
