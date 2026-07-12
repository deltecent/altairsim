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
    {"DUMP", true, nullptr, "DUMP [<addr>|<range>] [WIDTH=16]",
     "Hex and ASCII. A bare address runs to the END OF ITS PAGE, and a bare DUMP\n"
     "continues from there -- so the rows and the columns both stay page-aligned\n"
     "however you first landed. WIDTH is a count, so it is decimal.\n"
     "  D 100        0100-01FF, a whole page\n"
     "  D 0001       0001-00FF: stops on the boundary, last line full\n"
     "  D            the next page\n"
     "  D FF00-FF0F  an explicit range means exactly what it says\n"
     "  D 100/20     0100-011F (LEN is part of the address expression: hex)\n"
     "  D 0 WIDTH=8  eight bytes per line"},
    {"STEP", true, nullptr, "STEP [n]",
     "One instruction, with REAL bus cycles through the real decode. Prints each\n"
     "instruction as it goes; past 32 it runs quietly and reports. `n` is a count,\n"
     "so it is decimal.\n"
     "  S            one instruction\n"
     "  S 10         ten of them"},
    {"RESET", true, nullptr, "RESET [CPU]",
     "A reset does NOT clear memory. Only removing power does that -- see POWER.\n"
     "RESET CPU is a debugging convenience, NOT a real signal: no wire on the\n"
     "backplane resets the processor and nothing else."},
    {"HISTORY", false, "the debugger", "HISTORY [n]", nullptr},
    {"MOUNT", true, nullptr, "MOUNT <id>:<u> <file> [RO]",
     "Put a disk in a drive, or an image in a ROM socket.\n"
     "  MOUNT dsk:0 disks/cpm.dsk\n"
     "  MOUNT mem0:1 roms/monitor.bin"},
    {"BREAK", true, nullptr, "BREAK [<addr> | MEM R|W <addr> | IO R|W <port>]",
     "Bare BREAK lists them. Only the first kind is about the CPU at all -- the\n"
     "other two watch BUS CYCLES, so they catch a DMA transfer too, and they work\n"
     "unchanged on any processor.\n"
     "  BREAK FF13       stop when PC gets there\n"
     "  BREAK 2C00-2CFF  ...anywhere in a range\n"
     "  BREAK MEM W 100  stop when anything WRITES 0100\n"
     "  BREAK IO R 10    stop on an IN from port 10\n"
     "(BREAK ... IF <expr> is not built yet -- it is waiting on the expression\n"
     "parser, and the registers it will read are already reflected.)"},
    {"EDIT", false, "the line editor", "EDIT <addr>  -- interactive; Enter advances", nullptr},
    {"CONFIG", true, nullptr, "CONFIG LOAD <f.toml> | CONFIG SAVE <f.toml>",
     "SAVE writes the machine you are actually running, so it round-trips.\n"
     "  CONFIG SAVE machines/mine.toml"},
    // RUN is the front panel's switch, and it REPLACED GO (Patrick, 2026-07-12).
    // There was never a second thing for GO to be: a headless run is not a mode the
    // operator chooses, it is what happens when no unit holds the console -- and the
    // machine already knows that. Whether your keys reach the guest is a fact about
    // the backplane, not a question for you. RESET owns R, so RUN costs RU.
    {"RUN", true, nullptr, "RUN [addr]",  // RU
     "Start the machine. `RUN <addr>` is EXAMINE + RUN -- it loads the PC first,\n"
     "exactly as you would on the panel.\n"
     "\n"
     "If a unit holds the console, the GUEST GETS THE KEYBOARD -- every key,\n"
     "including ^C, which a CP/M program is entitled to read -- and the machine runs\n"
     "at the CPU card's real clock, so it is as fast as an Altair was and no faster.\n"
     "The way back is ATTN (^E), which the host takes before the guest is ever\n"
     "offered the byte, so the guest cannot disable it. ATTN does NOT stop the\n"
     "machine: a bare RUN resumes it.\n"
     "\n"
     "With no console connected there is no keyboard to hand over, so it simply runs\n"
     "-- full speed, ^C stops it. Either way it stops on a breakpoint or on a HLT\n"
     "nothing can wake, and it ALWAYS says which.\n"
     "  RUN F800     boot the monitor PROM\n"
     "  RUN          carry on from wherever the PC is"},

    // ---- everything else, ranked by how often you type it ----
    {"SET", true, nullptr, "SET <id> <k>=<v>",  // SE (beats SEARCH)
     "SHOW <id> lists every property, its value, and whether it can be set while\n"
     "the machine runs. A property's base is its own: a port is hex, a baud rate\n"
     "is decimal.\n"
     "  SET mem0 fill=zero\n"
     "  SET mem0 phantom=read"},
    {"SHOW", true, nullptr, "SHOW <id>|BUS [MAP|IO|CONTENTION]|ROMS|MACHINE",
     "  SHOW mem0        regions and properties\n"
     "  SHOW BUS MAP     who decodes what, and what floats\n"
     "  SHOW ROMS        the built-in images and their provenance"},
    {"DEPOSIT", true, nullptr, "DEPOSIT <addr> <bytes...>",  // DE
     "The front-panel switch. Runs a REAL bus write, so if no board decodes the\n"
     "address the byte is simply gone -- and DEPOSIT says so rather than lying.\n"
     "  DE 100 C3 00 F8"},
    // EXAMINE and DEPOSIT are the two switches on the front panel, and they belong
    // together -- DE and EX. EXAMINE is the quick look at ONE byte; bare EXAMINE
    // steps to the next, which is the panel's EXAMINE NEXT.
    {"EXAMINE", true, nullptr, "EXAMINE [<addr>]",  // EX
     "One byte: hex, ASCII, and the bits as the panel's LEDs showed them. Bare\n"
     "EXAMINE is the panel's EXAMINE NEXT -- it steps one byte. Its cursor is its\n"
     "own; a DUMP does not move it.\n"
     "  EX 100       0100  C3  .  11000011\n"
     "  EX           and the next byte, and the next"},
    {"IN", true, nullptr, "IN <port>",  // I
     "Runs a REAL IN cycle, with real side effects: an IN from a UART's data port\n"
     "consumes the byte and the guest never sees it. To look without touching, use\n"
     "WHO IO <port>. Reports whether anybody actually answered.\n"
     "  I 10         port 10 -> FF   (nobody answered -- the bus floated it)"},
    {"OUT", true, nullptr, "OUT <port> <byte>",  // O
     "Runs a REAL OUT cycle. Says so if no board decodes the port.\n"
     "  O 10 41"},
    {"LOAD", true, nullptr, "LOAD <file> [AT <addr>] [FORMAT=BIN|HEX] [RAW <id>]",
     "Format is autodetected. RAW <id> reaches BEHIND the bus into one board's\n"
     "store -- that is the PROM burner, and it is why the operator can write a ROM\n"
     "region while the guest cannot.\n"
     "  LOAD dbl.hex\n"
     "  LOAD monitor.bin AT F000 RAW mem0"},
    {"SAVE", true, nullptr, "SAVE <file> <range> [FORMAT=BIN|HEX] [RAW <id>]",
     "  SAVE out.hex 0-FFF"},
    {"FILL", true, nullptr, "FILL <range> <byte>",
     "  FILL 0-3FF 00"},
    {"SEARCH", true, nullptr, "SEARCH <range> <bytes...>|\"str\"",  // SEA
     "  SEA 0-FFFF C3\n"
     "  SEA 0-FFFF \"CP/M\""},
    {"COMPARE", true, nullptr, "COMPARE <range> <addr>|<file>", nullptr},  // COM
    {"MOVE", true, nullptr, "MOVE <range> <dest>", nullptr},               // MOV
    {"WHO", true, nullptr, "WHO <addr> | WHO IO <port>",
     "Who WOULD answer -- it looks without running a cycle, so nothing is consumed\n"
     "and no card is poked. Reports contention, and reports PHANTOM*.\n"
     "  WHO FF00\n"
     "  WHO IO 10"},
    {"BOARD", true, nullptr, "BOARD LIST|TYPES|ADD <type> <id> [k=v...]|REMOVE <id>",  // BO
     "  BOARD TYPES              every card, and its properties\n"
     "  BOARD ADD memory mem0"},
    {"REGS", true, nullptr, "REGS | SET REG <r>=<v>",  // REG (beats REGION)
     "The flags are registers too, so SET REG CY=1 works. A register value is on\n"
     "the wire, so it is HEX.\n"
     "  REGS\n"
     "  SET REG A=3F\n"
     "  SET REG PC=FF00"},
    {"REGION", true, nullptr, "REGION ADD <id> type=ram|rom at=<addr> [size=|mount=]",  // REGI
     "A region is a POPULATED part of a card. What is not covered by one is an\n"
     "empty socket: it decodes nothing and floats to FF. `at` is an address, so it\n"
     "is hex; `size` is a size, so it is decimal, and K/M work.\n"
     "  REGI ADD mem0 type=ram at=0 size=48K\n"
     "  REGI ADD mem0 type=rom at=FF00 mount=builtin:dbl"},
    {"DISASM", true, nullptr, "DISASM [<addr>|<range>] [n] [CPU=8080]",  // DI
     "It needs an INSTRUCTION SET, not a CPU -- so it works on an empty backplane.\n"
     "You normally never type CPU=: the active core says what it speaks, and DISASM\n"
     "asks it. It PEEKS, so it cannot consume a byte from a UART in the range.\n"
     "  DI FF00      sixteen instructions of the boot PROM\n"
     "  DI           carry on from there\n"
     "  DI 0-2F      exactly that range\n"
     "  DI FF00 CPU=8080   when there is no CPU in the machine to ask"},
    // UNMOUNT, not DISMOUNT (Patrick, 2026-07-11). It is the plain word, it takes U
    // -- which nothing else wanted -- and it gets out of DISASM's way, which drops
    // to DI now that the D-cluster is one shorter.
    {"UNMOUNT", true, nullptr, "UNMOUNT <id>:<u>",  // U
     "The socket is then EMPTY -- those pages float to FF, exactly as a card with\n"
     "no chip in it does.\n"
     "  U dsk:0"},
    {"DISCONNECT", true, nullptr, "DISCONNECT <id>:<u>",  // DISC
     "The line then goes nowhere. NOT an error: an unconnected 6850 sits there with\n"
     "TDRE set forever, and a program that writes to it works fine and talks to\n"
     "nobody -- which is exactly what the card does with no cable in it.\n"
     "  DISC sio0:b"},
    // CONSOLE CONFIGURES the console. It does not run the machine (Patrick,
    // 2026-07-12) -- RUN runs the machine, and a command that quietly started the
    // CPU because you asked to look at a setting is a trap.
    {"CONSOLE", true, nullptr, "CONSOLE [<k>=<v>...]",  // CONS
     "The host's terminal: what it is, who holds it, and how you get back from it.\n"
     "Bare CONSOLE shows it; CONSOLE k=v sets it. (SHOW CONSOLE and SET CONSOLE are\n"
     "the same thing said the long way.)\n"
     "\n"
     "ATTN is the key that takes the keyboard BACK from a running guest. The host\n"
     "intercepts it before the guest is ever offered the byte, so the guest cannot\n"
     "disable it -- and that is why it must not be a key the guest needs.\n"
     "  CONSOLE            what it is set to, and which unit holds it\n"
     "  CONSOLE attn=1D    make it ^]  (hex: it is a byte on the wire)\n"
     "To choose WHICH unit the console is wired to, that is CONNECT."},
    {"CONNECT", true, nullptr, "CONNECT <id>:<u> <endpoint>",  // CONN
     "Endpoints: console | null | loopback. (socket: and serial: are coming.)\n"
     "\n"
     "Exactly ONE unit may hold the console; connecting a second STEALS it and says\n"
     "who from. Two boards reading one keyboard would each get half the characters.\n"
     "  CONN sio0:a console\n"
     "  CONN sio0:b null"},
    {"POWER", true, nullptr, "POWER",
     "Power cycle. THE ONLY THING THAT LOSES RAM -- a RESET does not, because on\n"
     "real hardware it does not."},
    {"TRACE", false, "the debugger", "TRACE ON|OFF [file] [MASK=...]", nullptr},
    // STOP is still reserved, and CONSOLE mode has now made it MORE clearly a
    // separate thing rather than less. The machine runs while you are in CONSOLE,
    // but the monitor is not there -- the guest has the keyboard. STOP is for the
    // day the monitor and a running machine coexist, which needs a second thread
    // or a multiplexed input loop, and neither exists yet. ATTN is the way out
    // today, and it does not stop the machine: it takes the keyboard back.
    {"STOP", false, "a monitor that runs alongside the machine (ATTN leaves CONSOLE today)",
     "STOP", nullptr},  // STO
    {"SNAPSHOT", false, "the debugger", "SNAPSHOT <file>", nullptr},      // SN
    {"RESTORE", false, "the debugger", "RESTORE <file>", nullptr},        // REST
    {"RECORD", false, "the debugger", "RECORD <file>", nullptr},          // REC
    {"REPLAY", false, "the debugger", "REPLAY <file>", nullptr},          // REP
    {"NOBREAK", true, nullptr, "NOBREAK [id]",
     "Bare NOBREAK clears them all. An id is not on the wire, so it is decimal.\n"
     "  NOBREAK 2\n"
     "  NOBREAK"},
    {"HELP", true, nullptr, "HELP [<command>]",  // HE (HISTORY has H)
     "Bare HELP lists the commands and nothing else -- the whole set on a few\n"
     "lines, which is what you want when you are hunting for the name. HELP with a\n"
     "command gives the usage and the examples.\n"
     "  HELP         the list\n"
     "  HELP DUMP    the detail\n"
     "  ?            the same as HELP"},
    // There is no EXIT. QUIT is the one word for leaving, because two words for one
    // action is two things to learn and nothing gained -- and EXIT was also the only
    // reason EXAMINE could not simply be `EX`.
    {"QUIT", true, nullptr, "QUIT", nullptr},
};

const std::vector<CommandDef>& commands() { return kCommands; }

std::string abbreviation(const CommandDef& c) {
    std::string full = c.name;
    for (size_t n = 1; n <= full.size(); ++n) {
        std::string p = full.substr(0, n);
        const CommandDef* r = resolveCommand(p);
        if (r && std::string(r->name) == full)
            return n < full.size() ? p + "[" + full.substr(n) + "]" : full;
    }
    return full;  // unreachable: a name always resolves to itself
}

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
