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
    bool subunit(const std::string& spec, Board*& b, int& unit, std::ostream& err);

    void showBoard(Board* b, std::ostream& out);
    void showBus(const std::vector<std::string>& args, std::ostream& out);
    void showRoms(std::ostream& out);
    void flush(std::ostream& out);  // print anything the bus or a board said

    Machine& m_;
    bool failed_ = false;
    bool quit_ = false;

    // Where a bare DUMP resumes. A range moves it; nothing else does.
    uint32_t dumpNext_ = 0;

    // The front panel's address latch, for EXAMINE. Bare EXAMINE is EXAMINE NEXT.
    // It is DELIBERATELY separate from dumpNext_: DUMP walks a page at a time and
    // EXAMINE walks a byte at a time, and sharing one cursor would mean a DUMP
    // silently threw your EXAMINE position 256 bytes down the road.
    uint32_t examNext_ = 0;
};

std::vector<std::string> tokenize(const std::string& line);

} // namespace altair
