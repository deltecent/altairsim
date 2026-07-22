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
#include "core/expr.h"

#include <cstdint>
#include <memory>
#include <ostream>
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

// What a match DOES. Stopping is only the default, not the definition: a breakpoint
// is a place the debugger recognises, and stopping is one thing it can do there.
//
// The trace actions do NOT stop -- they flip TRACE's active flag and the machine
// runs on -- which is how you trace a REGION of a program instead of all of it.
// And unlike IF, a trace toggle is safe on the CYCLE kinds too, because it reads no
// registers: there is nothing to be boundary-inconsistent about.
enum class BreakAction {
    Stop,      // the breakpoint everyone knows
    TraceOn,   // start tracing here, keep running
    TraceOff,  // stop tracing here, keep running
};

const char* breakActionName(BreakAction a);

struct Breakpoint {
    int id = 0;
    BreakKind kind = BreakKind::Pc;
    uint32_t lo = 0, hi = 0;  // inclusive. A single address has lo == hi.
    bool enabled = true;
    BreakAction action = BreakAction::Stop;

    // Times it ACTED -- stopped you, or flipped the trace. Not times it matched: a
    // conditional breakpoint whose condition does not hold did nothing, and saying
    // it "hit" would be a lie the hits column tells every time you look at it.
    uint64_t hits = 0;

    // BREAK <addr> IF <expr>. A PC breakpoint that only stops when the condition is
    // true -- one comparison against reflected registers, so it is CPU-agnostic like
    // everything else here (DESIGN.md 3.0.3). Null on an unconditional breakpoint;
    // it applies to the PC kind only, since the cycle kinds fire mid-instruction
    // where a register read is not a boundary-consistent question. The Expr carries
    // its own source text, so describe() needs no second copy of it.
    std::shared_ptr<const Expr> cond;

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
    WindowClosed, // the operator closed the display window. Like Attn, not a fault
                  // and invisible to the guest -- a bare RUN resumes it, into the
                  // same window (host/display.h takeQuitRequest).
    NoCpu,        // there is no processor in this machine, which is a real machine
    StepTarget,   // NEXT ran to the return address of a stepped-over CALL/RST. Not a
                  // user breakpoint -- an internal one-shot the monitor set and cleared.
    Unclaimed,    // SET BUS UNCLAIMED=HALT and the guest reached an I/O port no board
                  // decodes. Stopped at the boundary, like a cycle breakpoint (4.6.1).
};

struct RunResult {
    StopReason why = StopReason::Steps;
    uint64_t steps = 0;
    uint64_t tStates = 0;
    uint16_t pc = 0;
    int bp = 0;   // which breakpoint, when why == Breakpoint
    uint8_t port = 0;    // which port, when why == Unclaimed
    bool write = false;  // OUT (true) or IN, when why == Unclaimed
};

class Debugger {
public:
    explicit Debugger(Machine& m) : m_(m) {}

    int add(BreakKind k, uint32_t lo, uint32_t hi, std::shared_ptr<const Expr> cond = nullptr,
            BreakAction action = BreakAction::Stop);
    bool remove(int id, std::string& err);
    void clear();
    const std::vector<Breakpoint>& breakpoints() const { return bps_; }

    // ---- TRACE and HISTORY: bus-observer facilities (DESIGN.md 3.0.3) ----
    //
    // Both watch the SAME cycle stream every board already sees, from outside the
    // backplane -- so they are NOT CPU features, they catch a DMA transfer as
    // readily as the processor, and a Z80 inherits them the day it lands. They are
    // fed by the run loop's observer, which is live only WHILE the machine runs.

    // The mask on TRACE. A cycle is shown if ANY of its categories is selected; an
    // empty mask (0) shows everything. IN/OUT/IRQ come straight off the cycle type;
    // Dma is set while a granted bus master is driving; Contention is a bus fact.
    enum TraceCat {
        InCycle   = 1 << 0,  // an IN  (I/O read)
        OutCycle  = 1 << 1,  // an OUT (I/O write)
        Irq       = 1 << 2,  // an interrupt-acknowledge cycle
        Dma       = 1 << 3,  // any cycle a granted bus master drove
        Contended = 1 << 4,  // more than one board answered
    };
    // WHERE trace goes is one question; WHETHER it is running is another. They used
    // to be one boolean, and a tracepoint is exactly the thing that pulls them apart:
    // BREAK 100 TRACE ON has to turn tracing on WITHOUT being told a sink, which
    // means the sink outlives the off state. So TRACE OFF keeps the file open and
    // the mask set, ready to be turned back on -- by TRACE ON, or by a tracepoint.
    //
    // The monitor owns the stream (a file, or the console); we only write to it.
    void traceTo(std::ostream* sink, unsigned mask) {   // configure AND start: TRACE ON
        traceSink_ = sink;
        traceMask_ = mask;
        traceActive_ = true;
    }
    void traceOn() { traceActive_ = true; }    // start with whatever is configured
    void traceOff() { traceActive_ = false; }  // stop, but KEEP the sink and the mask
    bool tracing() const { return traceActive_ && traceSink_; }
    bool traceConfigured() const { return traceSink_ != nullptr; }

    // A recorded cycle, for HISTORY and for formatting a trace line. Small by
    // design -- a ring of these records is cheap enough to keep always, so the
    // flight recorder has already caught what led up to a stop before you ask.
    struct CycleRec {
        Cycle type = Cycle::MemRead;
        uint16_t addr = 0;
        uint8_t data = 0;
        bool dma = false;
        bool contended = false;
        uint64_t t = 0;   // clock T-state at the cycle
    };

    // The last `n` cycles, OLDEST FIRST. n past what is held returns all of it.
    std::vector<CycleRec> history(size_t n) const;
    void clearHistory();

    // Render one recorded cycle the way TRACE and HISTORY both print it.
    static std::string formatCycle(const CycleRec&);

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

    // One observer folds three jobs into a single call per cycle: record HISTORY,
    // emit a TRACE line, and match cycle breakpoints. This decides whether a cycle
    // passes the current TRACE mask (empty mask -> everything).
    bool traceShows(const CycleRec&) const;

    // TRACE's configuration (where, and what it keeps) and, separately, whether it is
    // currently emitting -- see traceTo/traceOn/traceOff.
    std::ostream* traceSink_ = nullptr;
    unsigned traceMask_ = 0;
    bool traceActive_ = false;

    // True while serviceDma() is driving a granted master, so the observer can tag a
    // cycle as DMA -- the origin is deliberately NOT on the BusCycle (a real
    // backplane carries no such tag, bus.h), but the loop that GRANTED the bus knows.
    bool inDma_ = false;

    // HISTORY's ring. Fixed capacity, overwrite-oldest -- a flight recorder, always
    // running while the machine runs, so it has the run-up to a stop without anyone
    // having had to ask in advance. Below capacity the records sit in order at the
    // front; once full, ringHead_ is the oldest and the ring wraps.
    static constexpr size_t kHistoryCap = 8192;
    std::vector<CycleRec> ring_;
    size_t ringHead_ = 0;

    // STEP-OVER's internal one-shot PC target, or -1 for none. See setStepTarget.
    int stepTarget_ = -1;
};

} // namespace altair
