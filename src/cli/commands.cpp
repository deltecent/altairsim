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
//   R[UN]  RE[GS]  RES[ET]  REST[ORE]  REGI[ON]
//
// `built = false` means a command RESOLVES but does not run yet, and says so. That
// is on purpose: `S` must mean STEP from the first day, so that it does not mean
// SHOW until the CPU lands and then quietly change under someone's fingers. No
// command wears it today (RECORD/REPLAY/STOP were dropped, not deferred), but the
// mechanism stays for the next one that needs to claim a prefix before it works.
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
     "One instruction, with REAL bus cycles through the real decode. Prints one line\n"
     "per instruction; past 32 it runs quietly and reports. `n` is a count, so it is\n"
     "decimal.\n"
     "\n"
     "A LINE IS THE STATE AFTER THE INSTRUCTION RAN -- the registers as they now\n"
     "stand, and the instruction the PC has reached next. So `S 3` prints three lines,\n"
     "one per step, and the last line is where the monitor has left you: the next\n"
     "instruction, not yet run.\n"
     "  S            one instruction\n"
     "  S 10         ten of them\n"
     "\n"
     "  altairsim> DEPOSIT 0 3E 05 06 0A 80 76        MVI A,5 / MVI B,0A / ADD B / HLT\n"
     "  altairsim> EX 0\n"
     "  altairsim> S 3\n"
     "  C0Z0M0E0I0 A=05 B=0000 D=0000 H=0000 S=0000 IE=0 P=0002  MVI B,0A\n"
     "  C0Z0M0E0I0 A=05 B=0A00 D=0000 H=0000 S=0000 IE=0 P=0004  ADD B\n"
     "  C0Z0M0E1I0 A=0F B=0A00 D=0000 H=0000 S=0000 IE=0 P=0005  HLT\n"
     "Three instructions ran, three lines. Each shows the result: A=05 lands on the\n"
     "line for the MVI that loaded it, and the last line is the HLT waiting, not yet\n"
     "run. The flags are the 8080's own five, in the Altair's lettering -- Carry,\n"
     "Zero, Minus, Even parity, Interdigit carry -- and E goes to 1 on the ADD\n"
     "because 0F has an even number of bits set."},
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
    {"HISTORY", true, nullptr, "HISTORY [BUS|CPU] [n]",
     "The last n INSTRUCTIONS the machine ran, oldest first -- a flight recorder that\n"
     "is always running while the machine runs, so it already holds the run-up to a\n"
     "breakpoint or a crash when you ask. Each line is exactly what STEP prints: the\n"
     "registers and flags as the machine stood, and the decoded mnemonic it was about\n"
     "to run. n is a count, so it is decimal; bare HISTORY shows the last 16.\n"
     "\n"
     "The mnemonic is decoded from the bytes that ACTUALLY ran at that address, not\n"
     "from what the address holds by the time you look -- so code that rewrote itself\n"
     "(a DDT breakpoint going back, an overlay) reads truthfully. The line reflects\n"
     "the CPU that is in the socket now: on the twin-core card, switching cores makes\n"
     "earlier lines read in the new core's terms.\n"
     "\n"
     "HISTORY BUS is the other recorder -- the raw BUS CYCLES, no registers and no\n"
     "mnemonics: T-STATE, TYPE (MR/MW a memory read/write, IN/OUT a port, INTA an\n"
     "interrupt ack), ADDR (or the I/O port), DATA, and then the two that make it a\n"
     "BUS trace rather than a CPU one -- who DROVE the cycle and who ANSWERED it, so\n"
     "a DMA transfer's cycles are in there and say whose they were. HISTORY CPU names\n"
     "the default out loud.\n"
     "\n"
     "Each recorder is a FIXED ring of its last 8192: it overwrites its own oldest and\n"
     "never grows, so it costs the same whether the machine ran for a second or a\n"
     "week. Ask for more than 8192 and you get the 8192 it holds.\n"
     "  HISTORY          the last 16 instructions\n"
     "  HISTORY 100      the last hundred instructions\n"
     "  HISTORY BUS      the last 16 bus cycles\n"
     "  HISTORY BUS 100  the last hundred cycles"},
    {"MOUNT", true, nullptr, "MOUNT <id>[:<u>] <file> [WP] [CREATE] [extract[=<base>]] [k=v...]",
     "Put a disk in a drive, a tape in a recorder, or an image in a ROM socket.\n"
     "WP write-protects it: the guest may read it and may not write it.\n"
     "RO is accepted and means the same -- it is the word for a ROM, which is\n"
     "read-only because of what it is.\n"
     "\n"
     "CREATE makes the file first if it is not there (empty), then mounts it -- a\n"
     "fresh hard-sector disk to FORMAT, or a blank cassette. Without CREATE a missing\n"
     "file is a 'no such file', because a mistyped name is a mistake, not a new disk.\n"
     "\n"
     "A TRAILING k=v SETS A UNIT PROPERTY, applied the moment the medium is in --\n"
     "the same properties SHOW <id> lists and SET <id>:<unit> writes, said at the one\n"
     "moment you were going to say them anyway. A unit with no such property says so\n"
     "rather than ignoring you.\n"
     "\n"
     "EXTRACT is not a property and runs after the mount: it splits a cassette WAV\n"
     "into one .TAP per program on it, exactly as the EXTRACT verb does.\n"
     "extract=<base> names those files instead of taking the default.\n"
     "\n"
     "A NAME IS CASE-BLIND, and you may leave off what carries no information: the\n"
     "trailing index when only one such board is in the machine, and the unit when the\n"
     "board has only one you could mount into. Anything genuinely plural you must say,\n"
     "and it will tell you so.\n"
     "  MOUNT dsk0:drive0 disks/cpm.dsk\n"
     "  MOUNT dsk0:drive1 disks/master.dsk WP\n"
     "  MOUNT dsk0:drive1 new.dsk CREATE    a blank disk to FORMAT from the guest\n"
     "  MOUNT mem0:rom0 roms/monitor.bin\n"
     "  MOUNT ACR tape.bin      the one cassette, its one tape: acr0:tape\n"
     "  MOUNT ACR new.wav CREATE mode=record   a blank tape, in and recording\n"
     "  MOUNT sol0:tape1 TRK80.WAV extract     mount a WAV and split it into .TAP files\n"
     "\n"
     "SHOW MOUNTS is the other half of this command: every socket in the machine,\n"
     "what is in it, and which are still empty. UNMOUNT takes it back out. A path is\n"
     "resolved as SHOW PATHS describes -- what you TYPE is relative to your shell."},
    {"BREAK", true, nullptr,
     "BREAK [<addr> | MEM R|W <addr> | IO R|W <port> | TAPE STOP] [IF <expr> | LOADS <expr>] "
     "[TRACE ON|OFF]",
     "Bare BREAK lists them. Only the first kind is about the CPU at all -- MEM and IO\n"
     "watch BUS CYCLES, so they catch a DMA transfer too and work unchanged on any\n"
     "processor; TAPE STOP watches a DEVICE, halting when a cassette deck reaches its\n"
     "auto-stop mark -- the way to stop right after a load lands without knowing where\n"
     "the loader ends.\n"
     "  BREAK FF13       stop when PC gets there\n"
     "  BREAK 2C00-2CFF  ...anywhere in a range\n"
     "  BREAK MEM W 100  stop when anything WRITES 0100\n"
     "  BREAK IO R 10    stop on an IN from port 10\n"
     "  BREAK TAPE STOP  stop when a cassette deck auto-stops after a load\n"
     "\n"
     "A breakpoint may carry a CONDITION and stop only when it holds. IF <expr> tests\n"
     "the registers. A bare word that names a register IS that register, so a literal\n"
     "is written with a leading zero (0A is ten, A is the accumulator). == != < > <= >=\n"
     "compare; && || combine; & | mask.\n"
     "\n"
     "IF works on EVERY kind. On a plain BREAK <addr> it is the PC-arrival state. On a\n"
     "MEM or IO breakpoint it is judged at the instruction BOUNDARY, against the state\n"
     "the instruction began with -- its inputs -- so a conditional cycle breakpoint\n"
     "stops just AFTER the access, where an unconditional one stops just before it.\n"
     "\n"
     "BREAK IO R also takes LOADS <expr>: like IF, but judged AFTER the IN retires, so\n"
     "it sees the byte the port just delivered. IF gates on the inputs; LOADS on the\n"
     "value read. (The Z80 reads a port into any register, or memory; name the one the\n"
     "IN targets. On an 8080 it is always A.)\n"
     "\n"
     "The names are the ACTIVE CPU's own -- exactly the set REGS shows, every register\n"
     "and flag in it. On an 8080 that is A, BC/DE/HL, SP, PC and CY/Z/S/P/AC; a Z80\n"
     "adds IX, IY, the alternate bank and its own flags. A name the running CPU does\n"
     "not have is an error, so IF IX==0 waits for a Z80 to be the one in the socket.\n"
     "  BREAK 100 IF A==0\n"
     "  BREAK 100 IF HL==8000 && Z==1\n"
     "  BREAK 100 IF (A&0F)==0\n"
     "  BREAK IO R 10 IF B==5      stop on an IN from 10, but only while B==5\n"
     "  BREAK IO R 10 LOADS A>7F   ...only when port 10 hands back a byte over 7F\n"
     "  BREAK 100 IF IX==8000      Z80 -- IX is not an 8080 register\n"
     "\n"
     "TRACE ON|OFF makes it a TRACEPOINT: instead of stopping, it turns TRACE on or\n"
     "off and the machine RUNS ON. Two of them trace a REGION and nothing else --\n"
     "which is how you trace one subroutine out of a program that would otherwise\n"
     "bury you. Like IF, it works on the MEM and IO kinds too. TRACE ON at an address\n"
     "traces the instruction AT it; TRACE OFF\n"
     "does not -- the region is [on, off).\n"
     "  BREAK 2C00 TRACE ON        start tracing when PC gets to 2C00\n"
     "  BREAK 2C40 TRACE OFF       ...and stop again at 2C40\n"
     "  BREAK MEM W 2000 TRACE ON  start when anything writes 2000 (that write is\n"
     "                             the first line -- a trace shows its own reason)\n"
     "  BREAK 200 IF HL==8000 TRACE ON    conditional, and still does not stop\n"
     "Where the trace GOES is TRACE's business, not the tracepoint's: set it up with\n"
     "TRACE ON <file> MASK=..., then TRACE OFF to arm it without emitting. An\n"
     "unconfigured tracepoint traces to the console."},
    {"EDIT", true, nullptr, "EDIT <addr> [ROM]",  // ED
     "Interactive DEPOSIT. The prompt shows an address and the byte that is there;\n"
     "type a new value and Enter writes it and drops to the next byte, bare Enter\n"
     "leaves it and drops to the next, and '.' returns to the monitor. Runs REAL bus\n"
     "writes, so it says so if no board decodes the address; ROM burns instead, the\n"
     "way LOAD ... ROM does -- behind the bus, into the chip that answers there.\n"
     "If the machine has a CPU, type an INSTRUCTION where a byte would go and it is\n"
     "assembled in place -- the prompt then drops by the instruction's length, not one\n"
     "byte. Operands are numbers in the console base (an H or Q suffix overrides); a\n"
     "bare value is still a plain byte. Look at four bytes, patch two instructions in,\n"
     "and read them back:\n"
     "  altairsim> EDIT 100\n"
     "  0100 C3 IN 10       assembles DB 10, on to 0102\n"
     "  0102 00 LXI H,FF13  assembles 21 13 FF, on to 0105\n"
     "  0105 76 .           '.' returns to the monitor\n"
     "  altairsim> DISASM 100 2\n"
     "  0100  DB 10     IN 10\n"
     "  0102  21 13 FF  LXI H,FF13\n"
     "  (needs an interactive or piped session -- with none, use DEPOSIT)"},
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
    {"SET", true, nullptr, "SET <id>[:<u>]|CONSOLE|DISPLAY|REG|BUS <k>=<v>",  // SE (beats SEARCH)
     "SHOW <id> lists every property, its value, and whether it can be set while\n"
     "the machine runs. A property's base is its own: a port is hex, a baud rate\n"
     "is decimal.\n"
     "\n"
     "A UNIT HAS PROPERTIES OF ITS OWN, and <id>:<unit> is how you reach them: the\n"
     "tape in the recorder rather than the recorder, the disk in the drive rather\n"
     "than the controller. SHOW <id> prints both tables, the board's and each unit's.\n"
     "\n"
     "CONSOLE and DISPLAY are the HOST's terminal and video window rather than\n"
     "boards, and they take settings the same way. REG is a CPU register (see REGS),\n"
     "and BUS is the backplane's own diagnostics rather than anything plugged into it.\n"
     "  SET mem0 fill=zero\n"
     "  SET mem0 phantom=read\n"
     "  SET acr0:tape mode=record   the tape in the recorder, not the recorder\n"
     "  SET vdm0 width=1024      how wide the video window opens, in pixels (auto = ~half the screen)\n"
     "  SET DISPLAY focus=on     the video window takes the keyboard, not the terminal\n"
     "  SET REG A=3F             a register in the CPU that is in the socket\n"
     "  SET BUS UNCLAIMED=WARN   warn on a cycle no board answered\n"
     "                           (also CONTENTION=WARN|ERROR|SILENT, UNCLAIMED=WARN|HALT|SILENT)"},
    {"SHOW", true, nullptr,
     "SHOW <id>|BOARDS|BOARD <type> [UNITS]|MACHINES|MACHINE [<name>]|BUS [MAP|IO|IRQ|CONTENTION]|"
     "ROMS|MOUNTS|PATHS|CONSOLE|DISPLAY|SYMBOLS|VERSION",
     "  SHOW mem0        regions and properties\n"
     "  SHOW BOARDS      the board types you can add\n"
     "  SHOW BOARD sol   one type's description and properties (add UNITS for just those)\n"
     "  SHOW MACHINES    the built-in machines you can boot\n"
     "  SHOW MACHINE     the current machine (add a name for a built-in's detail)\n"
     "  SHOW BUS MAP     who decodes what, and what floats\n"
     "  SHOW BUS IRQ     VI0-VI7: who is strapped where, who is pulling, who wins\n"
     "  SHOW MOUNTS      every disk, tape and ROM in the machine, and what is in it\n"
     "  SHOW PATHS       what a path resolves against -- and there is more than one answer\n"
     "  SHOW CONSOLE     which unit holds the keyboard, and its transforms\n"
     "  SHOW DISPLAY     the host video window, and whether it takes the keyboard\n"
     "  SHOW JOYSTICKS   the host game controllers a D+7A can read (SDL builds)\n"
     "  SHOW SYMBOLS     the loaded symbols (SHOW SYMBOLS SIO* filters); load them with SYMBOLS\n"
     "  SHOW ROMS        the ROM images built into this binary, and where each came from\n"
     "  SHOW VERSION     which build this is, and the commit it was built from"},
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
    {"SAVE", true, nullptr, "SAVE <file> <range> [FORMAT=BIN|HEX|OCTAL|PRN]",
     "Memory to a file, through the bus -- so what you get is what the CPU would\n"
     "read, ROM included. The range is what to save; a byte nobody drives reads FF.\n"
     "\n"
     "THE NAME DECIDES THE FORMAT, and this is the other half of LOAD's rule rather\n"
     "than the same one: LOAD can open the file and see what it IS, and SAVE cannot\n"
     "-- the file does not exist yet. So a name ending .HEX writes Intel HEX, .OCT an\n"
     "octal listing, .PRN (or .LST) a disassembly listing, and anything else a flat\n"
     "binary. FORMAT= says it outright when the name would guess wrong, and wins.\n"
     "\n"
     "OCTAL and PRN are LISTINGS, not load formats: OCTAL is split-octal addresses\n"
     "and octal bytes, the way the MITS manuals and front panel showed memory; PRN\n"
     "is the DISASM listing -- address, object bytes, mnemonic and any SYMBOLS labels\n"
     "-- written to a file for reading and marking up. Both follow the console base.\n"
     "LOAD does not read either back: BIN and HEX round-trip, OCTAL and PRN do not.\n"
     "  SAVE out.hex 0-FFF                Intel HEX, by its name\n"
     "  SAVE out.bin F800-FFFF            a flat binary, by its name\n"
     "  SAVE out.oct 100-1FF              an octal listing, by its name\n"
     "  SAVE out.prn 100-1FF              a disassembly listing, by its name\n"
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
    {"COMPARE", true, nullptr, "COMPARE <range> <addr>",  // COM
     "Byte for byte, a <range> against the SAME LENGTH starting at <addr> -- memory to\n"
     "memory, both in the machine's address space. Every mismatch prints both\n"
     "addresses and their bytes; then a total. It changes nothing and runs no cycle.\n"
     "  COMPARE 0-FF 200        page 0 against page 2\n"
     "  COMPARE FF00-FFFF E000  the boot PROM against a copy up at E000"},
    {"MOVE", true, nullptr, "MOVE <range> <dest> [ROM]",  // MOV
     "Copy a range of memory to <dest>. It reads the WHOLE range before it writes, so\n"
     "source and dest may overlap either way without a block eating its own tail. The\n"
     "writes are real bus cycles; ROM burns instead, the way EDIT and DEPOSIT do.\n"
     "  MOVE 100-1FF 200    page 1 up to page 2\n"
     "  MOVE 0-FFF 1000     the first 4K, up by 4K"},
    {"WHO", true, nullptr, "WHO <addr> | WHO IO <port>",
     "Who WOULD answer -- it looks without running a cycle, so nothing is consumed\n"
     "and no board is poked. Reports contention, and reports PHANTOM*.\n"
     "  WHO FF00\n"
     "  WHO IO 10"},
    // The name is PLURAL, so both spellings work and neither is an alias: BOARD is
    // a prefix of BOARDS, and a prefix is what this table resolves. `BO` too.
    {"BOARDS", true, nullptr, "BOARDS [LIST]|ADD <type> <id> [k=v...]|REMOVE <id>",
     "The backplane: what is in it, what each board answers to, and what is in its\n"
     "sockets. A bare BOARDS lists them. RAM and ROM are named separately, and a\n"
     "ROM range says which image is in it -- an empty socket decodes nothing, so it\n"
     "is not in the memory column at all; it is in UNITS, marked (empty).\n"
     "  BOARDS                   the backplane\n"
     "  BOARD                    the same thing: a prefix of BOARDS\n"
     "  BOARDS ADD memory mem0   fit one -- SHOW BOARDS lists the types\n"
     "  BOARDS REMOVE mem0       pull one out"},
    // REGS is the first RE- word in the table, so it takes RE outright -- and it is
    // the one you type between two STEPs, which is as often as anything here.
    {"REGS", true, nullptr, "REGS | SET REG <r>=<v>",  // RE (beats RECORD, REPLAY, RESET, REGION)
     "The flags are registers too, so SET REG CY=1 works. A register value is on\n"
     "the wire, so it is HEX.\n"
     "\n"
     "What it shows is the ACTIVE CPU's own set: an 8080 prints A, the pairs, SP, PC\n"
     "and its flags; a Z80 prints those plus IX, IY, I and IM, and keeps the alternate\n"
     "bank (AF' BC' DE' HL'), R and the register halves reachable by name though they\n"
     "are off the line. SET REG takes any name REGS knows -- and only those, so SET\n"
     "REG IX=0 needs a Z80. BREAK ... IF reads the very same names.\n"
     "  REGS\n"
     "  SET REG A=3F\n"
     "  SET REG PC=FF00\n"
     "  SET REG IX=8000   Z80 only"},
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
     "\n"
     "n is how many INSTRUCTIONS to decode -- a count, so it is decimal, and 16 when\n"
     "you leave it off. It only applies to a start address: give a RANGE and the range\n"
     "decides where to stop. CPU= is an instruction set, one of 8080 or z80, and is\n"
     "only for when the machine has no CPU to ask.\n"
     "  DI FF00      sixteen instructions of the boot PROM\n"
     "  DI FF00 40   forty of them instead\n"
     "  DI           carry on from there\n"
     "  DI 0-2F      exactly that range\n"
     "  DI FF00 CPU=z80    decode as Z80 when nothing in the machine can say"},
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
     "The host's terminal -- your keyboard and screen -- and the knobs that shape\n"
     "how bytes cross it. Bare CONSOLE prints those settings (and which board unit\n"
     "is wired to the terminal); CONSOLE k=v changes one. It is pure shorthand:\n"
     "CONSOLE alone does what SHOW CONSOLE does, and CONSOLE k=v does what SET\n"
     "CONSOLE k=v does -- the same two commands, spelled short.\n"
     "\n"
     "The keys k, and the value v each takes:\n"
     "  attn       a control byte 01-1F (HEX): the key that returns to the monitor\n"
     "  base       hex | octal -- the operator's number base for what it PRINTS\n"
     "  history    lines saved in .altairsim_history in the launch dir (default 50; 0 = off)\n"
     "  upper      on|off: fold typed input to uppercase (much period software insists)\n"
     "  strip7in   on|off: mask the high bit on input\n"
     "  strip7out  on|off: mask the high bit on output (MITS BASIC's end-of-message)\n"
     "  crlf       on|off: add LF after every CR the guest prints -- usually WRONG\n"
     "  echo       on|off: local echo, for half-duplex hardware\n"
     "  bell       on|off: pass 07 through to the host bell\n"
     "  bsdel      off | bs (fold DEL->BS) | del (fold BS->DEL)\n"
     "These are the TERMINAL's, not a board's; a board's own line coding (baud,\n"
     "data_bits) is SHOW sio0. SHOW CONSOLE lists these with their current values.\n"
     "\n"
     "ATTN is the key that takes the keyboard BACK from a running guest. The host\n"
     "intercepts it before the guest is ever offered the byte, so the guest cannot\n"
     "disable it -- and that is why it must not be a key the guest needs.\n"
     "  CONSOLE            the settings, and which board unit is wired to the terminal\n"
     "  CONSOLE attn=1D    make it ^]  (hex: it is a byte on the wire)\n"
     "  CONSOLE upper=on strip7out=on   two at once, the classic MITS BASIC pair\n"
     "This command does NOT choose which board is the console -- CONNECT does that\n"
     "(CONNECT <id>:<unit> console); bare CONSOLE only reports the one now wired."},
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
     "\n"
     "  console     the host's terminal -- the keyboard and screen you are typing at\n"
     "  null        a cable to nowhere: writes vanish, reads never yield a byte\n"
     "  loopback    the unit's own transmit wired back to its receive, for testing\n"
     "  scripted    a terminal with a caller in place of a human -- what the MCP tools\n"
     "              and the test suite type into. No tty need exist.\n"
     "  socket:     PORT alone LISTENS: that is the telnet-in case. HOST:PORT CALLS OUT.\n"
     "  serial:     a real port on this host. It is opened at 9600 8N1 and then\n"
     "              immediately re-programmed by the board, which is the only thing that\n"
     "              knows what it is strapped to.\n"
     "  in:         PATH -- a host file as a READER (a paper-tape reader): its bytes\n"
     "              feed the line. ?cps=N (or ?baud=N) paces it -- in:tape.tap?cps=300\n"
     "              is the 88-HSR; no option means full speed.\n"
     "  out:        PATH -- a host file as a PUNCH: the line's bytes land on disk,\n"
     "              8-bit clean, from position 0 and never truncating. Combine them\n"
     "              for a bidirectional line: in:TAPE.TAP,out:TAPE.PUN\n"
     "  terminal    a windowed terminal the simulator draws itself, no telnet client\n"
     "              needed. ?emulation=vt100|adm3a|vt52|h19 picks the dialect (vt100\n"
     "              default), ?size=COLSxROWS the geometry (80x24 default). Needs a\n"
     "              window, so only in an SDL build -- headless refuses it at CONNECT.\n"
     "  printer:    QUEUE -- a real print queue on this host (only where the build found\n"
     "              one). The bytes buffer into a JOB, submitted after a few idle seconds\n"
     "              (?idle=N, 0=never), on a form feed (?onff), or at a byte ceiling\n"
     "              (?max=N). 8-bit clean -- a printer control language is not text.\n"
     "  <endpoint>|FILE   a TAP: append |FILE to ANY endpoint above to also log the line,\n"
     "              both directions, to a hex FILE -- a poor man's protocol analyzer. The\n"
     "              guest cannot tell it is there. ?fmt=dump|cols|jsonl picks the layout,\n"
     "              ?ts=elapsed|wall|none the timestamps, ?pins=off drops the modem edges.\n"
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
     "  CONN lpt0:prn out:printout.txt                    capture a printer to a file\n"
     "  CONN 4pio0:ja in:TAPE.TAP?cps=300                 a paper-tape reader (88-HSR)\n"
     "  CONN 4pio0:jb out:TAPE.PUN                        a paper-tape punch\n"
     "  CONN sio0:a terminal?emulation=adm3a              a windowed ADM-3A of its own\n"
     "  CONN lpt0:prn printer:linewriter                  print to a real host queue\n"
     "  CONN sio0:b socket:2323|bbs.hex?fmt=cols          telnet in, and TAP it to a log\n"
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
    {"TYPE", true, nullptr, "TYPE \"text\"",  // TY (TRACE has T, TR)
     "Put characters in the console's input buffer as if they were typed at the\n"
     "guest -- the same keystrokes the VDM window or a terminal would send. The\n"
     "guest reads them when it next looks at the keyboard, so they are TYPE-AHEAD:\n"
     "they wait in the buffer and are not forced on a program that is not reading.\n"
     "\n"
     "Escapes: \\r carriage return, \\n line feed, \\t tab, \\\\ backslash, \\\" quote.\n"
     "ENTER sends a carriage return, so a command line for the guest ends in \\r.\n"
     "\n"
     "This is how a machine file starts a program the monitor cannot reach. `startup`\n"
     "runs MONITOR commands; `XE TRK80` is input to SOLOS, a program INSIDE the machine.\n"
     "Put TYPE before the RUN that starts the guest and the guest reads it at its first\n"
     "prompt -- in a TOML the backslash doubles (\\\\r), because the config parser keeps\n"
     "one for the command:\n"
     "  startup = [\"MOUNT sol0:tape1 \\\"TRK80.WAV\\\"\", \"TYPE \\\"XE TRK80\\\\r\\\"\", \"RUN C000\"]\n"
     "  TYPE \"XE TRK80\\r\"        type it now, at a running guest\n"
     "\n"
     "A program that clears its keyboard as it starts drops keystrokes sent before it\n"
     "is ready; TYPE cannot help there, no more than a fast typist could."},
    {"SNAPSHOT", true, nullptr, "SNAPSHOT <file>",                        // SN
     "Write the machine's STATE to a file: the CPU, the clock, and every board's\n"
     "registers, RAM and latches. NOT its configuration -- a snapshot is state, the\n"
     "way a machine file is configuration. RESTORE reads it back.\n"
     "  SNAPSHOT before-boot.snap\n"},
    {"RESTORE", true, nullptr, "RESTORE <file>",                          // REST
     "Load a SNAPSHOT back into THIS machine. The machine must be the same shape the\n"
     "snapshot was taken from -- the same boards, same ids, same order (build it with\n"
     "the same machine file, or a CONFIG LOAD, first) -- and a file that does not match\n"
     "is refused with the reason, the running machine untouched.\n"
     "  RESTORE before-boot.snap\n"},
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
    {"QUIT", true, nullptr, "QUIT",
     "Leave the monitor and end the program. It does NOT ask: the machine lives in\n"
     "memory, so anything you have not written out -- CONFIG SAVE, SAVE, SNAPSHOT --\n"
     "is gone with it. There is no EXIT; QUIT is the one word.\n"
     "  QUIT"},
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
