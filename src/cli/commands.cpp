#include "cli/commands.h"

#include <cctype>

namespace altair {

// ---------------------------------------------------------------------------
// THE RANKING. Read it top to bottom: that IS the priority.
//
// The eight that own their prefix are Patrick's, 2026-07-11 (RUN took the slot GO
// held, and then RESET's -- 2026-07-13):
//   DUMP, STEP, RUN, HISTORY, MOUNT, BREAK, EDIT, CONFIG
//
// `D` is DUMP, which is what a ROM monitor's `D` has always been. SIMH made `D`
// DEPOSIT and `E` EXAMINE; this breaks with it deliberately. It also puts the
// shortest key on the keyboard on the command that cannot destroy anything, and
// makes you type two letters to change memory. DEPOSIT keeps the front panel's
// word -- it just costs `DE`.
//
// THE R-CLUSTER, and it is the same principle as `D` (Patrick, 2026-07-13): the
// shortest key goes to the command that cannot destroy anything, and the one that
// throws away the machine's state costs letters. A bare `R` must not reset. So RUN
// takes `R`, and the rest fall out of the order below with nobody deciding them:
//   R[UN]  RE[GS]  REC[ORD]  REP[LAY]  RES[ET]  REST[ORE]  REGI[ON]
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
     "\n"
     "A LINE IS THE STATE BEFORE THE INSTRUCTION ON IT RUNS -- the registers as they\n"
     "stand, and then what is about to happen to them. So a value you just loaded\n"
     "appears on the NEXT line, and the line the monitor leaves you on is where you\n"
     "have stopped: that instruction has not run.\n"
     "  S            one instruction\n"
     "  S 10         ten of them\n"
     "\n"
     "  altairsim> DEPOSIT 0 3E 05 06 0A 80 76        MVI A,5 / MVI B,0A / ADD B / HLT\n"
     "  altairsim> S 3\n"
     "  C0Z0M0E0I0 A=00 B=0000 D=0000 H=0000 S=0000 IE=0 P=0000  MVI A,05\n"
     "  C0Z0M0E0I0 A=05 B=0000 D=0000 H=0000 S=0000 IE=0 P=0002  MVI B,0A\n"
     "  C0Z0M0E0I0 A=05 B=0A00 D=0000 H=0000 S=0000 IE=0 P=0004  ADD B\n"
     "  C0Z0M0E1I0 A=0F B=0A00 D=0000 H=0000 S=0000 IE=0 P=0005  HLT\n"
     "Three instructions ran; the fourth line is the HLT waiting. A=05 shows up one\n"
     "line below the MVI that loaded it. The flags are the 8080's own five, in the\n"
     "Altair's lettering -- Carry, Zero, Minus, Even parity, Interdigit carry -- and\n"
     "E goes to 1 on the ADD because 0F has an even number of bits set."},
    // NEXT sits above NOBREAK so `N` -- the letter you reach for between two steps --
    // is NEXT, not NOBREAK. STEP took `S` and RUN took `R` for the same reason: the
    // command you type every few seconds wins the single letter. NOBREAK pays `NO`.
    {"NEXT", true, nullptr, "NEXT",  // N
     "STEP that does not descend. A CALL or RST runs to completion and stops at the\n"
     "return address instead of stepping into it; anything else is a plain single\n"
     "step. It is a temporary breakpoint at the return plus a RUN, so the callee is\n"
     "LIVE -- it can use the console, and ^E (ATTN) or ^C stops it.\n"
     "  N            over the CALL/RST at PC (else single-step)"},
    // RUN is the front panel's switch. It REPLACED GO (Patrick, 2026-07-12) -- there
    // was never a second thing for GO to be: a headless run is not a mode the operator
    // chooses, it is what happens when no unit holds the console, and the machine
    // already knows that. Whether your keys reach the guest is a fact about the
    // backplane, not a question for you.
    //
    // And it then took `R` from RESET (Patrick, 2026-07-13). It is the one you type
    // every session, and it is the one that costs nothing if you did not mean it --
    // whereas a bare `R` that resets is a machine you have to set up again. RESET
    // pays the letters: `RES`.
    {"RUN", true, nullptr, "RUN [addr]",  // R
     "Start the machine. `RUN <addr>` is EXAMINE + RUN -- it loads the PC first,\n"
     "exactly as you would on the panel.\n"
     "\n"
     "If a unit holds the console, the GUEST GETS THE KEYBOARD -- every key,\n"
     "including ^C, which a CP/M program is entitled to read. The way back is ATTN\n"
     "(^E), which the host takes before the guest is ever offered the byte, so the\n"
     "guest cannot disable it. ATTN STOPS the machine -- nothing executes while this\n"
     "prompt is up -- but it does not DISTURB it: ATTN is not RESET and not POWER, so\n"
     "every register, every byte and every disk survives, and a bare RUN resumes at the\n"
     "exact instruction. Stopped is not lost, and the debugger is at its most useful\n"
     "here: REGS, EXAMINE, DUMP, DISASM and STEP all work at this prompt.\n"
     "\n"
     "IT RUNS FLAT OUT unless the CPU board has a crystal. `clock_hz` defaults to 0,\n"
     "so a cassette that took a real Altair 110 seconds comes off in about one. `SET\n"
     "cpu0 clock_hz=2000000` buys back the 2 MHz machine AND its 110 seconds. What\n"
     "the guest sees is identical either way -- the tape still costs the same\n"
     "T-states -- so the crystal buys period FEEL, not period behaviour.\n"
     "\n"
     "With no console connected there is no keyboard to hand over, and nothing to\n"
     "pace against: it simply runs, ^C stops it. Either way it stops on a breakpoint\n"
     "or on a HLT nothing can wake, and it ALWAYS says which.\n"
     "  RUN F800     boot the monitor PROM\n"
     "  RUN          carry on from wherever the PC is"},
    {"HISTORY", true, nullptr, "HISTORY [n]",
     "The last n BUS CYCLES the machine ran, oldest first -- a flight recorder that\n"
     "is always running while the machine runs, so it already holds the run-up to a\n"
     "breakpoint or a crash when you ask. n is a count, so it is decimal; bare\n"
     "HISTORY shows the last 16. Each line is a cycle, not an instruction, and a DMA\n"
     "transfer's cycles are in there too.\n"
     "  HISTORY          the last 16 cycles\n"
     "  HISTORY 100      the last hundred"},
    {"MOUNT", true, nullptr, "MOUNT <id>[:<u>] <file> [WP]",
     "Put a disk in a drive, a tape in a recorder, or an image in a ROM socket.\n"
     "WP write-protects it: the guest may read it and may not write it.\n"
     "RO is accepted and means the same -- it is the word for a ROM, which is\n"
     "read-only because of what it is.\n"
     "\n"
     "A NAME IS CASE-BLIND, and you may leave off what carries no information: the\n"
     "trailing index when only one such board is in the machine, and the unit when the\n"
     "board has only one you could mount into. Anything genuinely plural you must say,\n"
     "and it will tell you so.\n"
     "  MOUNT dsk0:drive0 disks/cpm.dsk\n"
     "  MOUNT dsk0:drive1 disks/master.dsk WP\n"
     "  MOUNT mem0:rom0 roms/monitor.bin\n"
     "  MOUNT ACR tape.bin      the one cassette, its one tape: acr0:tape\n"
     "\n"
     "SHOW MOUNTS is the other half of this command: every socket in the machine,\n"
     "what is in it, and which are still empty. UNMOUNT takes it back out. A path is\n"
     "resolved as SHOW PATHS describes -- what you TYPE is relative to your shell."},
    {"BREAK", true, nullptr,
     "BREAK [<addr> [IF <expr>] | MEM R|W <addr> | IO R|W <port>] [TRACE ON|OFF]",
     "Bare BREAK lists them. Only the first kind is about the CPU at all -- the\n"
     "other two watch BUS CYCLES, so they catch a DMA transfer too, and they work\n"
     "unchanged on any processor.\n"
     "  BREAK FF13       stop when PC gets there\n"
     "  BREAK 2C00-2CFF  ...anywhere in a range\n"
     "  BREAK MEM W 100  stop when anything WRITES 0100\n"
     "  BREAK IO R 10    stop on an IN from port 10\n"
     "\n"
     "A plain address breakpoint may carry a CONDITION -- IF <expr> over the\n"
     "registers -- and stops only when it holds. A bare word that names a register IS\n"
     "that register, so a literal is written with a leading zero (0A is ten, A is the\n"
     "accumulator). == != < > <= >= compare; && || combine; & | mask.\n"
     "  BREAK 100 IF A==0\n"
     "  BREAK 100 IF HL==8000 && Z==1\n"
     "  BREAK 100 IF (A&0F)==0\n"
     "\n"
     "TRACE ON|OFF makes it a TRACEPOINT: instead of stopping, it turns TRACE on or\n"
     "off and the machine RUNS ON. Two of them trace a REGION and nothing else --\n"
     "which is how you trace one subroutine out of a program that would otherwise\n"
     "bury you. Unlike IF, this works on the MEM and IO kinds too, because it reads\n"
     "no registers. TRACE ON at an address traces the instruction AT it; TRACE OFF\n"
     "does not -- the region is [on, off).\n"
     "  BREAK 2C00 TRACE ON        start tracing when PC gets to 2C00\n"
     "  BREAK 2C40 TRACE OFF       ...and stop again at 2C40\n"
     "  BREAK MEM W 2000 TRACE ON  start when anything writes 2000 (that write is\n"
     "                             the first line -- a trace shows its own reason)\n"
     "  BREAK 200 IF HL==8000 TRACE ON    conditional, and still does not stop\n"
     "Where the trace GOES is TRACE's business, not the tracepoint's: set it up with\n"
     "TRACE ON <file> MASK=..., then TRACE OFF to arm it without emitting. An\n"
     "unconfigured tracepoint traces to the console."},
    {"EDIT", false, "the line editor", "EDIT <addr>  -- interactive; Enter advances", nullptr},
    {"CONFIG", true, nullptr, "CONFIG LOAD <f.toml> | CONFIG SAVE <f.toml>",
     "THE MACHINE, NOT WHAT IT IS DOING. SAVE writes the hardware you are actually\n"
     "running -- which boards, in what order, every property SET can write, what each\n"
     "unit is CONNECTed to, what is MOUNTed in each socket, and the startup list. It\n"
     "is the same format you would write by hand, and the same one a built-in is\n"
     "written in, so a saved machine is a first-class machine: LOAD it back, or name\n"
     "it on the command line, and you get exactly what you saved.\n"
     "\n"
     "IT DOES NOT SAVE STATE, and that is not a gap to be filled: a machine file\n"
     "describes hardware, and none of this is hardware. NOT saved --\n"
     "  RAM             what you DEPOSITed is gone. LOAD/SAVE <file> <range> is for\n"
     "                  memory, and it is a separate file for a reason.\n"
     "  the registers   PC included, so a LOADed machine has not started.\n"
     "  breakpoints     nor tracepoints, nor where TRACE was pointed.\n"
     "  CONSOLE         attn and the transforms are the HOST's terminal, not a board\n"
     "                  in the backplane. They survive CONFIG LOAD untouched.\n"
     "A SAVE IS A READ: it asks every property for its value and writes to nothing.\n"
     "\n"
     "LOAD IS THE WHOLE MACHINE, so it REPLACES the one you have: the boards you had\n"
     "are out of the backplane, the new ones are in, and it is powered up and running\n"
     "its startup list -- a file whose startup says RUN comes up running. Naming that\n"
     "same file on the command line does the identical thing; there is one road.\n"
     "\n"
     "AND IT IS ALL OR NOTHING. The machine is built off to one side first, so a file\n"
     "that will not load -- a key that does not parse, a disk image that is not there\n"
     "-- leaves you exactly where you were. What you do not get back is the machine\n"
     "you REPLACED: there is no undo but the file you saved it to.\n"
     "  CONFIG SAVE machines/mine.toml\n"
     "  CONFIG LOAD machines/mine.toml      ...and this is how you get it back"},

    // ---- everything else, ranked by how often you type it ----
    {"SET", true, nullptr, "SET <id> <k>=<v>",  // SE (beats SEARCH)
     "SHOW <id> lists every property, its value, and whether it can be set while\n"
     "the machine runs. A property's base is its own: a port is hex, a baud rate\n"
     "is decimal.\n"
     "  SET mem0 fill=zero\n"
     "  SET mem0 phantom=read"},
    {"SHOW", true, nullptr, "SHOW <id>|BUS [MAP|IO|IRQ|CONTENTION]|ROMS|MOUNTS|PATHS|CONSOLE|SYMBOLS|MACHINE",
     "  SHOW mem0        regions and properties\n"
     "  SHOW BUS MAP     who decodes what, and what floats\n"
     "  SHOW BUS IRQ     VI0-VI7: who is strapped where, who is pulling, who wins\n"
     "  SHOW MOUNTS      every disk, tape and ROM in the machine, and what is in it\n"
     "  SHOW PATHS       what a path resolves against -- and there is more than one answer\n"
     "  SHOW CONSOLE     which unit holds the keyboard, and its transforms\n"
     "  SHOW SYMBOLS     the loaded symbols (SHOW SYMBOLS SIO* filters); load them with SYMBOLS\n"
     "  SHOW ROMS        the ROM images built into this binary, and where each came from"},
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
    {"LOAD", true, nullptr, "LOAD <file> [AT <addr>] [FORMAT=BIN|HEX] [ROM]",
     "Put a file into memory. There are two kinds of file and they differ in ONE\n"
     "way -- whether the file knows where it goes.\n"
     "\n"
     "  HEX  Intel HEX. ASCII text, and it CARRIES ITS OWN ADDRESSES, so it needs\n"
     "       no AT. Each line is one record: a ':', a length, the address, a type\n"
     "       (00 data, 01 end-of-file), the bytes, and a checksum. Every checksum\n"
     "       is verified and a bad one FAILS the load and names the record -- a\n"
     "       half-loaded program is a miserable thing to debug.\n"
     "         :10010000C300F8AF32004D3E0132014D76C9AA5509\n"
     "         :00000001FF\n"
     "          ^^^^^^^^^^                              ^^ checksum: the record\n"
     "          |  |    |                                  sums to zero, byte-wise\n"
     "          |  |    type 00 = data (01 = end of file)\n"
     "          |  load address: these bytes go at 0100\n"
     "          10 = sixteen data bytes follow\n"
     "\n"
     "  BIN  A flat binary: bytes, and nothing else. It carries NO addresses, so\n"
     "       it cannot say where it goes and AT is REQUIRED. Without one, LOAD\n"
     "       refuses rather than guess.\n"
     "\n"
     "WHICH ONE IS DECIDED BY THE FILE'S CONTENTS, not its name -- Intel HEX\n"
     "announces itself, and a .bin full of HEX text is still HEX. FORMAT= overrides\n"
     "that when the sniff is wrong; it always wins.\n"
     "\n"
     "AT MEANS 'PUT IT HERE', for both kinds. On a HEX file it relocates: the\n"
     "file's FIRST data record lands at AT and everything else moves by the same\n"
     "amount, wrapping at FFFF (a file whose first record is F000, loaded AT 0,\n"
     "puts its F800 record at 0800). Without AT, a HEX file loads where it says.\n"
     "\n"
     "ROM is the PROM burner. LOAD writes through the bus, so a ROM never takes it\n"
     "-- a bus write cannot program a PROM on real hardware either, and LOAD says\n"
     "how many bytes landed nowhere rather than half-loading in silence. ROM goes\n"
     "behind the bus, straight into whichever chip answers reads at that address:\n"
     "the operator pulling the chip and putting it in a programmer. It is why the\n"
     "operator can write a ROM and the guest cannot.\n"
     "  LOAD dbl.hex                      where the file says, through the bus\n"
     "  LOAD monitor.bin AT F000 ROM      a flat binary, burned into the ROM there\n"
     "  LOAD prog.hex AT 100              relocate: first record goes to 0100\n"
     "  LOAD odd.txt AT 0 FORMAT=HEX      it IS hex, whatever it is called"},
    {"SAVE", true, nullptr, "SAVE <file> <range> [FORMAT=BIN|HEX]",
     "Memory to a file, through the bus -- so what you get is what the CPU would\n"
     "read, ROM included. The range is what to save; a byte nobody drives reads FF.\n"
     "\n"
     "THE NAME DECIDES THE FORMAT, and this is the other half of LOAD's rule rather\n"
     "than the same one: LOAD can open the file and see what it IS, and SAVE cannot\n"
     "-- the file does not exist yet. So a name ending .HEX writes Intel HEX and\n"
     "anything else writes a flat binary. FORMAT= says it outright when the name\n"
     "would guess wrong, and always wins.\n"
     "  SAVE out.hex 0-FFF                Intel HEX, by its name\n"
     "  SAVE out.bin F800-FFFF            a flat binary, by its name\n"
     "  SAVE out.dat 0-FFF FORMAT=HEX     hex, though it is not called .hex"},
    {"FILL", true, nullptr, "FILL <range> <byte>",
     "  FILL 0-3FF 00"},
    {"SEARCH", true, nullptr, "SEARCH <range> <bytes...>|\"str\"",  // SEA
     "  SEA 0-FFFF C3\n"
     "  SEA 0-FFFF \"CP/M\""},
    // Memory-to-memory only. A `COMPARE <range> <file>` form was advertised here for a
    // while and never existed -- the handler parses the third argument as an address and
    // says "not a number" on a path. Help that names a form the program refuses is worse
    // than no help: it sends you looking for the typo in your own command.
    {"COMPARE", true, nullptr, "COMPARE <range> <addr>", nullptr},  // COM
    {"MOVE", true, nullptr, "MOVE <range> <dest>", nullptr},               // MOV
    {"WHO", true, nullptr, "WHO <addr> | WHO IO <port>",
     "Who WOULD answer -- it looks without running a cycle, so nothing is consumed\n"
     "and no board is poked. Reports contention, and reports PHANTOM*.\n"
     "  WHO FF00\n"
     "  WHO IO 10"},
    // The name is PLURAL, so both spellings work and neither is an alias: BOARD is
    // a prefix of BOARDS, and a prefix is what this table resolves. `BO` too.
    {"BOARDS", true, nullptr, "BOARDS [LIST]|TYPES|ADD <type> <id> [k=v...]|REMOVE <id>",  // BO
     "The backplane: what is in it, what each board answers to, and what is in its\n"
     "sockets. A bare BOARDS lists them. RAM and ROM are named separately, and a\n"
     "ROM range says which image is in it -- an empty socket decodes nothing, so it\n"
     "is not in the memory column at all; it is in UNITS, marked (empty).\n"
     "  BOARDS                   the backplane\n"
     "  BOARD                    the same thing: a prefix of BOARDS\n"
     "  BOARDS TYPES             every board, and its properties\n"
     "  BOARDS ADD memory mem0"},
    // REGS is the first RE- word in the table, so it takes RE outright -- and it is
    // the one you type between two STEPs, which is as often as anything here.
    {"REGS", true, nullptr, "REGS | SET REG <r>=<v>",  // RE (beats RECORD, REPLAY, RESET, REGION)
     "The flags are registers too, so SET REG CY=1 works. A register value is on\n"
     "the wire, so it is HEX.\n"
     "  REGS\n"
     "  SET REG A=3F\n"
     "  SET REG PC=FF00"},
    {"REGION", true, nullptr, "REGION ADD <id> type=ram|rom at=<addr> [size=|mount=]",  // REGI
     "A region is a POPULATED part of a board. What is not covered by one is an\n"
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
    // SYMBOLS is not LOAD. LOAD is memory all the way down (every format it takes
    // becomes bytes in the address space); a symbol table has no address space to land
    // in -- it is the debugger's NAMES for one, host-side like a breakpoint, surviving
    // RESET and POWER. So it is its own verb, the way CONFIG is (DESIGN.md 10.3.2). SY
    // is free.
    {"SYMBOLS", true, nullptr, "SYMBOLS LOAD <file> [REPLACE] | SYMBOLS CLEAR",  // SY
     "Load an assembler's symbols so you can BREAK, DUMP and EXAMINE by NAME instead of\n"
     "by hex, and SHOW SYMBOLS to read the table. Two file kinds, and one absolute rule:\n"
     "\n"
     "  .PRN / .LST   an assembler LISTING -- CP/M ASM, Microsoft M80, DR MAC. It marks\n"
     "                an EQU, so a constant is told apart from a program label.\n"
     "  .SYM          the CP/M symbol file DR MAC/RMAC write and SID reads. A flat list\n"
     "                of name=value, no label/constant distinction. (L80 writes no .SYM.)\n"
     "\n"
     "ADDRESSES MUST BE ABSOLUTE. A relocatable M80 listing is refused, by the line --\n"
     "link it and load the .SYM, or assemble to an absolute origin.\n"
     "\n"
     "LOAD merges (the newest of a clashing name wins, and it says how many); REPLACE\n"
     "clears first; CLEAR empties the table. A file named in a machine's startup is\n"
     "reloaded on CONFIG LOAD and round-trips through CONFIG SAVE.\n"
     "  SYMBOLS LOAD prog.SYM\n"
     "  SYMBOLS LOAD roms/ALTMON/ALTMON.PRN\n"
     "  SYMBOLS CLEAR"},
    // UNMOUNT, not DISMOUNT (Patrick, 2026-07-11). It is the plain word, it takes U
    // -- which nothing else wanted -- and it gets out of DISASM's way, which drops
    // to DI now that the D-cluster is one shorter.
    {"UNMOUNT", true, nullptr, "UNMOUNT <id>:<u>",  // U
     "The socket is then EMPTY -- those pages float to FF, exactly as a card with\n"
     "no chip in it does.\n"
     "  U dsk0:drive0"},
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
    // `{endpoints}` is expanded by the HELP printer from endpointHelp(), which is the
    // one place the grammar lives (host/endpoint.cpp). It is a token and not a list
    // because the list WAS spelled out here, and it rotted: it went on saying "socket:
    // and serial: are coming" for as long as resolveEndpoint() had been implementing
    // both. A help string that copies somebody else's vocabulary is a second schema.
    //
    // The GLOSS below -- a line saying what each of those names means -- is that same
    // knife, and it is here on purpose: `null` and `scripted` are not self-describing,
    // and the authoritative list cannot say so in one line it also has to fit into an
    // error message. So the enumeration stays {endpoints}'s job and the gloss stays
    // prose, and test_cli.cpp asserts that EVERY name endpointHelp() offers is glossed
    // here. Add an endpoint without a word about it and that test fails, which is the
    // only reason this is allowed to be a copy at all.
    {"CONNECT", true, nullptr, "CONNECT <id>:<u> <endpoint>",  // CONN
     "PLUG IN THE OTHER END OF THE CABLE. A unit is a socket on the back of a board --\n"
     "one of the 2SIO's two ports, say; an ENDPOINT is the thing at the far end of the\n"
     "cable, on the HOST side of the machine. It is not a board, it has no address, and\n"
     "the guest cannot see it: the 6850 clocks bytes the same way whether the wire ends\n"
     "at your terminal, a telnet session, a real RS-232 port, or nothing at all. No board\n"
     "in the machine knows what any of these words mean.\n"
     "\n"
     "Endpoints: {endpoints}\n"
     "  console     the host's terminal -- the keyboard and screen you are typing at\n"
     "  null        a cable to nowhere: writes vanish, reads never yield a byte\n"
     "  loopback    the unit's own transmit wired back to its receive, for testing\n"
     "  scripted    a terminal with a caller in place of a human -- what the MCP tools\n"
     "              and the test suite type into. No tty need exist.\n"
     "  socket:     PORT alone LISTENS: that is the telnet-in case. HOST:PORT CALLS OUT.\n"
     "  serial:     a real port on this host. It is opened at 9600 8N1 and then\n"
     "              immediately re-programmed by the board, which is the only thing that\n"
     "              knows what it is strapped to.\n"
     "  file:       PATH -- a host file, write-only: a printout, or a capture of the\n"
     "              line. Opened fresh (truncated); bytes land byte-for-byte, 8-bit clean.\n"
     "\n"
     "Exactly ONE unit may hold the console; connecting a second STEALS it and says\n"
     "who from. Two boards reading one keyboard would each get half the characters.\n"
     "  CONN sio0:a console\n"
     "  CONN sio0:b null\n"
     "  CONN sio0:b loopback\n"
     "  CONN sio0:b socket:2323            `telnet localhost 2323` now reaches the guest\n"
     "  CONN sio0:b socket:bbs.example:23  the guest dials OUT, to somebody else's port\n"
     "  CONN sio0:b serial:/dev/tty.usbserial-AL009KFH    a real cable, real hardware\n"
     "  CONN sio0:b serial:COM3                           ...the same, on Windows\n"
     "  CONN lpt0:prn file:printout.txt                   capture a printer to a file\n"
     "DISCONNECT takes the cable out again; SHOW CONSOLE says which unit holds it."},
    // RESET sits with POWER, which is the other command that throws state away, and
    // BELOW REGS -- which is what costs it `R` and `RE` and leaves it `RES`. It has to
    // stay ABOVE RESTORE, or RESET's own name would resolve to RESTORE and there would
    // be no way left to type it: that is the one invariant, and test_cli.cpp guards it.
    {"RESET", true, nullptr, "RESET [CPU]",  // RES
     "A reset does NOT clear memory. Only removing power does that -- see POWER.\n"
     "RESET CPU is a debugging convenience, NOT a real signal: no wire on the\n"
     "backplane resets the processor and nothing else."},
    {"POWER", true, nullptr, "POWER",
     "Power cycle. THE ONLY THING THAT LOSES RAM -- a RESET does not, because on\n"
     "real hardware it does not."},
    {"TRACE", true, nullptr, "TRACE ON|OFF [file] [MASK=IN,OUT,IRQ,DMA,CONTENTION]",
     "Log every BUS CYCLE while the machine runs -- to the console, or to a file.\n"
     "A cycle, not an instruction: MR/MW are memory, IN/OUT are I/O, INTA is an\n"
     "interrupt acknowledge, and a granted DMA master's cycles are tagged [DMA].\n"
     "This watches the same stream every board sees, so it is not a CPU feature and\n"
     "works unchanged on any processor.\n"
     "\n"
     "MASK keeps only the cycles you name (no MASK keeps all): IN, OUT, IRQ, DMA,\n"
     "CONTENTION. A cycle is kept if it is any of them -- MASK=DMA is every cycle a\n"
     "master drove, whatever its type.\n"
     "  TRACE ON                    every cycle, to the console\n"
     "  TRACE ON run.log            ...to a file\n"
     "  TRACE ON MASK=IN,OUT        just the port traffic\n"
     "  TRACE OFF\n"
     "\n"
     "TRACE OFF stops the tracing but REMEMBERS where it was going -- a later TRACE\n"
     "ON, or a tracepoint, resumes to the same file and mask. That is what lets you\n"
     "aim a tracepoint at a file: TRACE ON run.log MASK=DMA, then TRACE OFF to arm\n"
     "it without emitting, then BREAK <addr> TRACE ON. See BREAK."},
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

// The shortest prefix NOTHING BUILT-IN CLAIMS -- see the header. `REW[IND]`.
std::string boardAbbreviation(const CommandDef& c) {
    std::string full = c.name;
    for (size_t n = 1; n <= full.size(); ++n) {
        std::string p = full.substr(0, n);
        if (!resolveCommand(p))  // the table declined it -- so the cards get asked
            return n < full.size() ? p + "[" + full.substr(n) + "]" : full;
    }
    // Every prefix of it, up to and including the whole name, is claimed by a
    // built-in. The verb cannot be typed at all. test_cli.cpp fails if a board ever
    // ships one.
    return full;
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
