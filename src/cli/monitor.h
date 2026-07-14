#pragma once
//
// The monitor (DESIGN.md 10). SIMH/AltairZ80-flavored: stable and greppable.
//
// In milestone 1a there is no CPU, so THE MONITOR IS THE BUS MASTER. Every
// DUMP, DEPOSIT and LOAD runs a real bus cycle through the real decode -- which
// means the bus design is under test from the first keystroke, with no
// processor in the way to blame.

#include "core/machine.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace altair {

class Monitor {
public:
    explicit Monitor(Machine& m) : m_(m) {}

    // Run one command. Returns false when the monitor should exit.
    bool exec(const std::string& line, std::ostream& out);

    // Read-eval-print. `echo` prints each command first, for -c scripts.
    int repl(std::istream& in, std::ostream& out, bool interactive);

    bool failed() const { return failed_; }
    int exitCode() const { return failed_ ? 1 : 0; }

    // Run a machine's startup list (DESIGN.md 10.0). Anything you can type, a
    // config can do -- so `startup` is not a second language.
    void runStartup(std::ostream& out);

private:
    // ---- NUMBER BASE (Patrick, 2026-07-11; was open finding F3) ----
    //
    //     ON THE WIRE -> HEX.   NEVER ON THE WIRE -> DECIMAL.
    //
    // The base belongs to the OPERAND, not to the command line. `addr()` is for
    // the things the 8080 itself sees -- addresses, ports, data bytes -- and they
    // are hex, bare, as they are on the front panel and in every listing ever
    // printed. `count()` is for the things only the operator sees -- step counts,
    // dump widths, history depth, unit numbers -- and they are decimal, because
    // the machine never holds one of them.
    //
    // A PROPERTY carries its own `radix` in the reflection layer, which is the
    // same rule reaching the same answer: `SET sio2a port=10` is port 0x10 (a
    // wire), `SET sio2a baud=9600` is nine thousand six hundred (not a wire).
    //
    // `0x`/`$`/trailing-`h` force hex and `#` forces decimal, everywhere, in both
    // directions. A `K`/`M` suffix is always decimal and always wins.
    bool addr(const std::string& t, uint32_t& out, std::ostream& err);
    bool count(const std::string& t, uint32_t& out, std::ostream& err);
    bool range(const std::string& t, uint32_t& lo, uint32_t& hi, std::ostream& err);

    Board* board(const std::string& id, std::ostream& err);

    // WHAT THE COMMAND IS ABOUT TO DO WITH THE UNIT -- which is the only thing that
    // decides whether the unit's KIND is legal. It was a `bool wantMountable`, and
    // the bool had a third case hiding inside its `false`: SET is neither mounting
    // nor connecting, and it was being told a cassette is "not a serial port". Every
    // unit the 2SIO has is a serial one, so nothing noticed until a tape turned up.
    enum class UnitUse {
        Mount,    // MOUNT/UNMOUNT: media only -- a disk, a ROM, a tape
        Connect,  // CONNECT/DISCONNECT: a serial line only
        Any,      // SET/SHOW: a unit is a unit. Its properties are its own business.
    };

    // `id:unit` -> board + NAMED unit, with the kind checked against the command.
    bool subunit(const std::string& spec, Board*& b, UnitDef& u, UnitUse use,
                 std::ostream& err);

    // Every verb the cards in the machine declare right now, deduped by name, each
    // with the type of a card that brings it. Empty on a machine with no such card --
    // which is the point of the whole mechanism.
    //
    // BY VALUE, AND THAT IS NOT A STYLE CHOICE -- IT IS A SEGFAULT I ALREADY WROTE.
    // Board::commands() returns its table BY VALUE, so a `const CommandDef*` taken
    // into it dangles the moment the temporary dies, which is at the end of the very
    // loop that collected it. `REW` crashed the monitor. The struct is five
    // `const char*` pointing at string LITERALS: copying it is free, and the literals
    // outlive every board in the machine.
    std::vector<std::pair<std::string, CommandDef>> boardVerbs() const;

    // A VERB A CARD BROUGHT WITH IT (Board::commands()). Called ONLY after the
    // built-in table has failed to prefix-match, so the static menu always wins and
    // no card can move a built-in abbreviation by being plugged in.
    //
    // False means "nothing in the machine answers to this word" -- and the caller
    // then prints `unknown command`, which is the truth: with no 88-ACR in a slot
    // there IS no REWIND. True means it was handled, including handled badly.
    bool boardCommand(const std::vector<std::string>& a, std::ostream& out);

    // One line of disassembly, in a listing's shape: address, the raw bytes, the
    // instruction. Returns the instruction's length so the caller can walk on.
    //
    // It PEEKS. A disassembler that ran real bus cycles would consume a byte from
    // a UART sitting in the range you asked about -- so DISASM would silently eat
    // the console's input, and only when the memory map happened to be unlucky.
    uint8_t disasmLine(uint32_t addr, const class Disassembler& d, std::ostream& out);

    // Decode ONE instruction, no printing -- the status line wants the mnemonic
    // and nothing else. Peeks, for the same reason.
    struct Insn insnAt(uint32_t addr, const class Disassembler& d);

    // The active core, or null with a message already printed. Every CPU command
    // starts here, and none of them assume the machine has a processor -- because
    // a backplane without one is a machine you can build and the one 1a ran.
    CpuCore* needCpu(std::ostream& err);
    void showRegs(std::ostream& out);

    void showBoard(Board* b, std::ostream& out);
    void showBoards(std::ostream& out);  // the backplane: BOARDS
    void showProps(const std::vector<Property>& ps, std::ostream& out);

    // A sub-unit table's KEYS -- no value column, because the thing they describe does
    // not exist yet. (Board::subUnitProperties.)
    void showSchema(const std::vector<Property>& ps, std::ostream& out);
    void showBus(const std::vector<std::string>& args, std::ostream& out);

    // SHOW BUS IRQ -- the eight VI lines, who is strapped to them, who is pulling
    // them, and who wins. `table` is false for the summary bare SHOW BUS prints.
    void showBusIrq(std::ostream& out, bool table);

    // RUN. The machine runs until a breakpoint, a HLT nothing can wake, or ATTN.
    // If a unit holds the console the guest owns the keyboard while it does; if
    // none does, there is nothing to hand over and it simply runs. That is not a
    // mode -- it is a fact about the backplane, and the machine already knows it.
    void runMachine(std::ostream& out);
    void showConsole(std::ostream& out);
    void showRoms(std::ostream& out);
    void flush(std::ostream& out);  // print anything the bus or a board said

    Machine& m_;
    bool failed_ = false;
    bool quit_ = false;

    // WHOSE DIRECTORY THE COMMAND CURRENTLY RUNNING CAME OUT OF -- and "" whenever a
    // human is at the keyboard, which is nearly always (core/paths.h).
    //
    // Set only for the duration of runStartup(), because a `startup` entry is a command
    // WRITTEN IN a machine file and a path written in a machine file is relative to that
    // file. The instant the list is done this goes back to "", and `MOUNT dsk0:drive1
    // "scratch.dsk"` at the prompt means the scratch.dsk in the shell you are standing
    // in -- never the one beside somebody's example config.
    std::string startupDir_;

    // Where a bare DUMP resumes. A range moves it; nothing else does.
    uint32_t dumpNext_ = 0;

    // Where a bare DISASM resumes -- its own cursor, for the same reason EXAMINE
    // has one: you disassemble forward through a routine while dumping the table
    // it points at, and neither should drag the other along.
    //
    // A STEP or a GO moves it to the PC, because after the machine stops the thing
    // you want to look at is what it is about to do next, every time.
    uint32_t disasmNext_ = 0;

    // EXAMINE's cursor for `EXAMINE RAW <id>` ONLY -- the PROM burner's offset,
    // which is not a bus cycle and has no CPU in the loop.
    //
    // A BUS EXAMINE DOES NOT USE THIS. The panel has no address latch of its own:
    // it jams the switches into the PROGRAM COUNTER and lets the CPU drive the
    // address lines. The PC therefore IS the examine cursor, and EXAMINE NEXT steps
    // it (Patrick, 2026-07-12). Keeping a private copy here would be a second
    // counter shadowing the real one, and the two would diverge the moment you
    // STEP -- see monitor.cpp.
    //
    // It stays separate from dumpNext_ either way: DUMP walks a page and EXAMINE
    // walks a byte, and sharing one cursor would mean a DUMP silently threw your
    // EXAMINE position 256 bytes down the road.
    uint32_t examNext_ = 0;
};

std::vector<std::string> tokenize(const std::string& line);

} // namespace altair
