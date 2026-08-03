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

#include <array>
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

    // DEVICE EVENTS. Not a bus fact and not a PC fact -- a board reached a named
    // hardware state (a cassette hit its auto-stop mark). There is no bus cycle for
    // "the tape ran out", so these are POLLED at the instruction boundary, the way SET
    // BUS UNCLAIMED=HALT is (DESIGN.md 4.6.1), never observed on the backplane. See
    // kDeviceEvents below -- one table drives the whole family.
    TapeStop,  // a cassette deck reached auto-stop (BREAK TAPE STOP)
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
    TapeStop,     // a BREAK TAPE STOP device-event breakpoint fired -- a cassette deck
                  // reached its auto-stop mark. Polled at the boundary, like Unclaimed.
};

// A DEVICE-EVENT BREAKPOINT KIND: BREAK <kind> <action>. The first (and, for now, only)
// member is BREAK TAPE STOP -- stop when a cassette deck reaches its auto-stop mark, so
// you can halt right after a load lands without knowing the loader's end address. It is
// polled at the boundary like SET BUS UNCLAIMED=HALT, NOT observed on the bus, because
// "the tape ran out" is not a bus cycle -- so this is genuinely new machinery, distinct
// from the MEM/IO cycle kinds.
//
// ONE TABLE DRIVES THE WHOLE FAMILY -- the monitor's parser, describe(), the run-loop
// poll and the help text all read it -- so a new member (PRINTER PAGE, LINE CARRIER,
// DISK SEEK, ...) is one row and cannot drift between where it is parsed and where it is
// named, the same discipline endpointHelp() uses for the CONNECT schemes.
struct DeviceEvent {
    const char* kind;    // the <kind> word, uppercase for matching: "TAPE"
    const char* action;  // the <action> word, uppercase for matching: "STOP"
    BreakKind   bk;      // the BreakKind it arms
    StopReason  sr;      // ...and why the run stops when it fires
};
inline constexpr DeviceEvent kDeviceEvents[] = {
    {"TAPE", "STOP", BreakKind::TapeStop, StopReason::TapeStop},
    // {"PRINTER", "PAGE",    BreakKind::..., StopReason::...},   <- future: one row each,
    // {"LINE",    "CARRIER", BreakKind::..., StopReason::...},      and nothing else moves
    // {"DISK",    "SEEK",    BreakKind::..., StopReason::...},
};

// The device-event row for a BreakKind, or nullptr if `k` is a bus/PC kind. Lets
// describe() and the run loop treat the family generically.
const DeviceEvent* deviceEventForKind(BreakKind k);

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
        // WHO drove it and WHO answered, as INTERNED HANDLES -- indices into the
        // debugger's board-name table, NOT pointers and NOT strings. A pointer would
        // dangle once a board left the backplane; a string would allocate on every one
        // of the 8192 records in an always-on ring. A small int does neither: the name
        // is looked up once, at record time, and stored by number. -1 is "nobody" --
        // the floating bus for `responder`, and the processor itself for `master`
        // (a normal cycle originates at the CPU; only a DMA grant names a board here).
        int16_t master = -1;     // who ORIGINATED the cycle: -1 = the CPU, else a DMA board
        int16_t responder = -1;  // who ANSWERED it: -1 = nobody drove (floating bus)
    };

    // The last `n` cycles, OLDEST FIRST. n past what is held returns all of it.
    std::vector<CycleRec> history(size_t n) const;
    void clearHistory();

    // The board-name table the CycleRec handles index into. Its slot i is the id of
    // the board interned as handle i; -1 handles resolve to a literal ("cpu" for the
    // master, "--" for the responder) and never touch this. The monitor reads it to
    // render HISTORY BUS; the observer passes it to formatCycle for a live TRACE line.
    const std::vector<std::string>& boardHandles() const { return boardHandles_; }

    // Render one recorded cycle the way TRACE and HISTORY both print it. `handles` is
    // boardHandles() -- passed in because this is static (a trace line and a HISTORY
    // line format identically, and neither should carry a Debugger to say so).
    static std::string formatCycle(const CycleRec&, const std::vector<std::string>& handles);

    // A recorded INSTRUCTION, for CPU HISTORY -- the sibling of CycleRec. It holds the
    // machine as it stood at the instruction boundary: the register VALUES in the active
    // core's registers() order, the PC, and the opcode bytes that ran there. That is
    // enough for the monitor to render the exact DDT-style line STEP prints -- faithfully,
    // because the stored bytes are what EXECUTED, not whatever the address holds by the
    // time you look (self-modifying code stays honest). The core said which registers are
    // lamps and what they are called; this only carries their numbers, so it never learns
    // what an 8080 is. Formatting lives in the monitor (it needs the disassembler, the
    // symbol table and the operator's number base), so there is no formatInsn() here.
    struct InsnRec {
        uint16_t pc = 0;
        std::vector<uint32_t> regs;      // one value per RegDef, in registers() order
        std::array<uint8_t, 3> bytes{};  // opcode + up to two operand bytes at pc
        uint8_t nbytes = 0;
    };

    // The last `n` instructions, OLDEST FIRST -- the CPU counterpart of history().
    std::vector<InsnRec> insnHistory(size_t n) const;
    void clearInsnHistory();

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

    // Match one bus cycle against the CYCLE-kind breakpoints (MEM/IO, read/write) --
    // count the hit, flip trace for a tracepoint, and set cycleHit_ for a Stop. Called
    // from TWO places so the bookkeeping lives once: the pre-access veto (for a CPU
    // cycle, so the machine stops BEFORE the access) and the post-access observer (for
    // a DMA-driven cycle, which has no CPU instruction to roll back). PC and device-event
    // kinds are not its business and are skipped.
    void matchCycleBreak(const BusCycle& c);

    // Grant pHLDA to any board pulling pHOLD (DESIGN.md 4.5), in slot order -- slot
    // order IS the daisy-chain priority, no arbitration register. Drives each granted
    // board's BusMaster while it keeps requesting, charging the stolen T-states to the
    // clock. Called at every instruction boundary; inert (one boolean per board) when
    // nobody wants the bus, which is every machine that has no DMA card in it.
    void serviceDma();

    Machine& m_;
    std::vector<Breakpoint> bps_;
    int nextId_ = 1;

    // Set by matchCycleBreak when a Stop-action cycle breakpoint matches, from either
    // of its two callers. For a CPU cycle it is set by the PRE-access veto, which then
    // throws CycleBreakBefore -- the instruction unwinds untouched and the machine stops
    // with the PC on it, nothing executed. For a DMA-driven cycle it is set by the
    // POST-access observer and the run loop stops at the next boundary: a board's own
    // transfer has no CPU instruction to roll back, so pre/post makes no difference there.
    int cycleHit_ = 0;
    int observer_ = 0;

    // ONE-SHOT RESUME past a pre-access cycle breakpoint. Stopping BEFORE the access
    // restores the PC onto the very instruction that tripped, so a bare RUN/STEP would
    // re-issue the identical cycle and the veto would fire again, forever -- the operator
    // could never get past the line. So on such a stop we remember the PC and arm a
    // one-shot: the next run lets a matching cycle at that SAME PC through exactly once
    // (no hit counted, no stop), then disarms -- so a later pass over the same instruction
    // in a loop traps normally. Guarded on the PC so moving the PC between stops (a jump,
    // a DEPOSIT) never swallows an unrelated hit.
    bool skipArmed_ = false;
    uint16_t resumeCyclePc_ = 0;

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

    // Which board holds the bus right now, while inDma_ is true. serviceDma() sets it
    // before it drives a granted master, so the observer can name WHO originated a DMA
    // cycle (the CPU needs no entry -- a non-DMA cycle's master is always the CPU).
    Board* dmaMaster_ = nullptr;

    // The interning table behind CycleRec's `master`/`responder` handles. A board's id
    // string is stored ONCE, the first time it drives or answers during a run, and the
    // ring keeps only the small index -- so the always-on recorder never allocates per
    // cycle and never holds a pointer that could dangle after a board is removed.
    //
    // boardHandles_ is the handle -> id table; it only ever GROWS (a ring record from an
    // earlier run must still resolve), so handles are stable across runs. internPtrs_ is
    // the pointer -> handle map used to answer "have I seen this board this run?"; it is
    // rebuilt each run (armObserver clears it) because a raw Board* is only good for the
    // run that produced it. internMru_/internMruH_ are a one-entry cache in front of it:
    // consecutive cycles overwhelmingly hit the same board (a loop hammering one RAM
    // card), so the common case is a single pointer compare and no scan at all.
    std::vector<std::string> boardHandles_;
    std::vector<std::pair<Board*, int>> internPtrs_;
    Board* internMru_ = nullptr;
    int internMruH_ = -1;
    int internBoard(Board* b);

    // HISTORY's ring. Fixed capacity, overwrite-oldest -- a flight recorder, always
    // running while the machine runs, so it has the run-up to a stop without anyone
    // having had to ask in advance. Below capacity the records sit in order at the
    // front; once full, ringHead_ is the oldest and the ring wraps.
    static constexpr size_t kHistoryCap = 8192;
    std::vector<CycleRec> ring_;
    size_t ringHead_ = 0;

    // The CPU instruction recorder -- the sibling of the bus ring above, and it runs on
    // the same terms: always on while the machine runs, overwrite-oldest, one record per
    // instruction retired. Fed from the run loop at the boundary, before the instruction
    // executes, so a record is the machine STEP would have shown for it.
    static constexpr size_t kInsnHistoryCap = 8192;
    std::vector<InsnRec> insnRing_;
    size_t insnRingHead_ = 0;

    // STEP-OVER's internal one-shot PC target, or -1 for none. See setStepTarget.
    int stepTarget_ = -1;
};

} // namespace altair
