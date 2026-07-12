#pragma once
//
// The debugger (DESIGN.md 3.0.3).
//
// IT IS NOT A CPU FEATURE, AND THAT IS THE WHOLE DESIGN. If a core owned
// breakpoints, every core would reimplement them and they would differ in small
// maddening ways. They do not belong there:
//
//   BREAK IO / BREAK MEM / TRACE / HISTORY  are questions about BUS CYCLES, and
//     the bus already shows every cycle to everyone watching (Bus::observe).
//     They are CPU-agnostic, they work against a DMA transfer as readily as
//     against the processor, and the machinery already existed.
//
//   BREAK <addr>  is the one CPU-flavoured one, and it is just "PC equals X after
//     a step" -- one comparison against a register the reflection layer already
//     exposes. It does not know what an 8080 is.
//
// So an 8085 card, or a Z80, inherits the ENTIRE debugger on the day it lands
// without a line being written here.

#include "core/bus.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class Machine;

enum class BreakKind {
    Pc,        // PC lands here
    MemRead,   // ANY master read this address -- CPU, DMA, or the front panel
    MemWrite,
    IoRead,    // an IN from this port
    IoWrite,   // an OUT to this port
};

const char* breakKindName(BreakKind k);

struct Breakpoint {
    int id = 0;
    BreakKind kind = BreakKind::Pc;
    uint32_t lo = 0, hi = 0;  // inclusive. A single address has lo == hi.
    bool enabled = true;
    uint64_t hits = 0;
    std::string describe() const;
};

enum class StopReason {
    Steps,        // ran the count it was asked for
    Breakpoint,
    Halted,       // HLT, and nothing is going to interrupt it
    Interrupted,  // the operator pressed ^C
    NoCpu,        // there is no processor in this machine, which is a real machine
};

struct RunResult {
    StopReason why = StopReason::Steps;
    uint64_t steps = 0;
    uint64_t tStates = 0;
    uint16_t pc = 0;
    int bp = 0;   // which breakpoint, when why == Breakpoint
};

class Debugger {
public:
    explicit Debugger(Machine& m) : m_(m) {}

    int add(BreakKind k, uint32_t lo, uint32_t hi);
    bool remove(int id, std::string& err);
    void clear();
    const std::vector<Breakpoint>& breakpoints() const { return bps_; }

    // Run. `maxSteps == 0` means until something stops us -- a breakpoint, a HLT,
    // or ^C. Returns WHY it stopped, always: there is no "it just came back".
    RunResult run(uint64_t maxSteps);

    // ^C, from the signal handler. The ONLY thing the handler does is set this,
    // because that is the only thing it is safe to do.
    static void interrupt();
    static void clearInterrupt();

private:
    bool armObserver();
    void disarmObserver();

    Machine& m_;
    std::vector<Breakpoint> bps_;
    int nextId_ = 1;

    // Set by the bus observer when a cycle breakpoint matches. The run loop reads
    // it at the next instruction boundary rather than stopping mid-instruction,
    // because a real machine cannot stop mid-instruction either -- and half-run
    // instructions are how a debugger corrupts the state it exists to show you.
    int cycleHit_ = 0;
    int observer_ = 0;
};

} // namespace altair
