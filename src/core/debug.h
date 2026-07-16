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

// WHY IT CAME BACK. A run that just... returns, with no reason given, is a debugger
// you cannot trust -- so every one of these has words, and the monitor says them.
enum class StopReason {
    Steps,        // ran the count it was asked for
    Breakpoint,
    Halted,       // HLT, and nothing is going to interrupt it
    Attn,         // the operator took the keyboard back (^E). NOT a fault, and not
                  // a stop the guest can tell happened -- a bare RUN resumes it.
    InputEnded,   // a SCRIPT's input ran out and the guest went quiet asking for
                  // more. Nobody stopped it; there is just nobody left to type.
    Interrupted,  // the operator pressed ^C
    NoCpu,        // there is no processor in this machine, which is a real machine
    StepTarget,   // NEXT ran to the return address of a stepped-over CALL/RST. Not a
                  // user breakpoint -- an internal one-shot the monitor set and cleared.
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

    // STEP-OVER's temporary breakpoint (NEXT). A run-scoped, one-shot PC target
    // the run loop stops at -- NOT a Breakpoint: no id, no hits, invisible to
    // BREAK, and it cannot collide with a user breakpoint's id. `pc == -1` clears
    // it. The monitor sets it before running the callee and clears it after, so it
    // survives across the slices runMachine drives run() in.
    void setStepTarget(int pc) { stepTarget_ = pc; }

    // ^C, from the signal handler. The ONLY thing the handler does is set this,
    // because that is the only thing it is safe to do.
    static void interrupt();
    static void clearInterrupt();

private:
    bool armObserver();
    void disarmObserver();

    // Grant pHLDA to any board pulling pHOLD (DESIGN.md 4.5), in slot order -- slot
    // order IS the daisy-chain priority, no arbitration register. Drives each granted
    // board's BusMaster while it keeps requesting, charging the stolen T-states to the
    // clock. Called at every instruction boundary; inert (one boolean per board) when
    // nobody wants the bus, which is every machine that has no DMA card in it.
    void serviceDma();

    Machine& m_;
    std::vector<Breakpoint> bps_;
    int nextId_ = 1;

    // Set by the bus observer when a cycle breakpoint matches. The run loop reads
    // it at the next instruction boundary rather than stopping mid-instruction,
    // because a real machine cannot stop mid-instruction either -- and half-run
    // instructions are how a debugger corrupts the state it exists to show you.
    int cycleHit_ = 0;
    int observer_ = 0;

    // STEP-OVER's internal one-shot PC target, or -1 for none. See setStepTarget.
    int stepTarget_ = -1;
};

} // namespace altair
