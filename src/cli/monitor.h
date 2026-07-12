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

    // `id:unit` -> board + NAMED unit, with the kind checked against the command.
    // `wantMountable` is true for MOUNT/UNMOUNT, false for CONNECT/DISCONNECT.
    bool subunit(const std::string& spec, Board*& b, UnitDef& u, bool wantMountable,
                 std::ostream& err);

    // One line of disassembly, in a listing's shape: address, the raw bytes, the
    // instruction. Returns the instruction's length so the caller can walk on.
    //
    // It PEEKS. A disassembler that ran real bus cycles would consume a byte from
    // a UART sitting in the range you asked about -- so DISASM would silently eat
    // the console's input, and only when the memory map happened to be unlucky.
    uint8_t disasmLine(uint32_t addr, const class Disassembler& d, std::ostream& out);

    // The active core, or null with a message already printed. Every CPU command
    // starts here, and none of them assume the machine has a processor -- because
    // a backplane without one is a machine you can build and the one 1a ran.
    CpuCore* needCpu(std::ostream& err);
    void showRegs(std::ostream& out);

    void showBoard(Board* b, std::ostream& out);
    void showProps(const std::vector<Property>& ps, std::ostream& out);
    void showBus(const std::vector<std::string>& args, std::ostream& out);

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
