#include "core/debug.h"

#include "core/machine.h"

#include <atomic>
#include <cstdio>

namespace altair {

// The ONE thing a signal handler is allowed to touch. Not a std::string, not the
// machine, not a stream -- a lock-free flag, and nothing else.
static std::atomic<bool> g_interrupt{false};

void Debugger::interrupt() { g_interrupt.store(true); }
void Debugger::clearInterrupt() { g_interrupt.store(false); }

const char* breakKindName(BreakKind k) {
    switch (k) {
    case BreakKind::Pc:       return "pc";
    case BreakKind::MemRead:  return "mem r";
    case BreakKind::MemWrite: return "mem w";
    case BreakKind::IoRead:   return "io r";
    case BreakKind::IoWrite:  return "io w";
    }
    return "?";
}

std::string Breakpoint::describe() const {
    char buf[80];
    bool io = (kind == BreakKind::IoRead || kind == BreakKind::IoWrite);
    int w = io ? 2 : 4;
    if (lo == hi)
        std::snprintf(buf, sizeof buf, "%-6s %0*X", breakKindName(kind), w, (unsigned)lo);
    else
        std::snprintf(buf, sizeof buf, "%-6s %0*X-%0*X", breakKindName(kind), w, (unsigned)lo,
                      w, (unsigned)hi);
    return buf;
}

int Debugger::add(BreakKind k, uint32_t lo, uint32_t hi) {
    Breakpoint b;
    b.id = nextId_++;
    b.kind = k;
    b.lo = lo;
    b.hi = hi;
    bps_.push_back(b);
    return b.id;
}

bool Debugger::remove(int id, std::string& err) {
    for (size_t i = 0; i < bps_.size(); ++i) {
        if (bps_[i].id == id) {
            bps_.erase(bps_.begin() + (long)i);
            return true;
        }
    }
    err = "no breakpoint " + std::to_string(id);
    return false;
}

void Debugger::clear() { bps_.clear(); }

// ---------------------------------------------------------------------------
// The cycle breakpoints, as a bus observer.
//
// ARMED ONLY WHILE RUNNING, and that is deliberate. The monitor's own DUMP and
// DEPOSIT are real bus cycles -- that is the point of them -- so an always-armed
// BREAK MEM W would "trigger" on the operator's own DEPOSIT, with no program
// running to stop. Breaking is something that happens to a RUNNING machine; the
// observer exists only for as long as one is.
// ---------------------------------------------------------------------------
bool Debugger::armObserver() {
    bool anyCycle = false;
    for (const Breakpoint& b : bps_)
        if (b.enabled && b.kind != BreakKind::Pc) anyCycle = true;
    if (!anyCycle) return false;

    cycleHit_ = 0;
    observer_ = m_.bus.observe([this](const BusCycle& c) {
        for (Breakpoint& b : bps_) {
            if (!b.enabled || b.kind == BreakKind::Pc) continue;

            bool match = false;
            switch (b.kind) {
            case BreakKind::MemRead:  match = c.type == Cycle::MemRead;  break;
            case BreakKind::MemWrite: match = c.type == Cycle::MemWrite; break;
            case BreakKind::IoRead:   match = c.type == Cycle::IoRead;   break;
            case BreakKind::IoWrite:  match = c.type == Cycle::IoWrite;  break;
            case BreakKind::Pc:       break;
            }
            if (!match) continue;

            bool io = (b.kind == BreakKind::IoRead || b.kind == BreakKind::IoWrite);
            uint32_t a = io ? c.port() : c.addr;
            if (a < b.lo || a > b.hi) continue;

            ++b.hits;
            if (!cycleHit_) cycleHit_ = b.id;   // the FIRST one to fire wins the report
        }
    });
    return true;
}

void Debugger::disarmObserver() {
    if (observer_) {
        m_.bus.unobserve(observer_);
        observer_ = 0;
    }
}

// ---------------------------------------------------------------------------
// pHOLD / pHLDA -- bus mastering (DESIGN.md 4.5).
//
// At an instruction boundary the run loop offers the bus to any board pulling
// pHOLD. It is the exact analogue of interrupt sampling: the CPU never stops
// mid-instruction, so a grant happens between instructions and not inside one.
//
// The board that is granted becomes a bus master through the SAME
// BusMaster::step(Bus&) the CPU uses, and its stolen T-states are charged to the
// clock right here -- which is the whole of "the CPU genuinely loses time". A
// deadline that comes due mid-transfer (a UART draining, a cycle-steal re-arm)
// fires from clock.advance() exactly as it does on the CPU's own path.
//
// Slot order is the priority: bus_.boards() is backplane order, and the first
// board still pulling pHOLD wins. That IS the S-100 daisy chain -- there is no
// arbitration register, and inventing one would be the mistake DESIGN.md 4.4
// warns about with PHANTOM* and the VI lines.
//
// There is no separate "keep the machine alive for DMA" signal: a periodic master
// (a Dazzler refreshing a frame) re-asks for the bus by arming a Clock deadline,
// and the run loop's halt test already keeps a halted machine alive while anything
// is queued (clock.queued() != 0). So a DMA transfer runs through a HLT for free,
// through the deadline queue that was already there.
// ---------------------------------------------------------------------------
void Debugger::serviceDma() {
    // The fast path, and the common one: nobody is pulling pHOLD, so this is a single
    // integer test and we are gone. The backplane walk below happens ONLY when a wire
    // says someone wants the bus (Bus::holdPending()) -- we do not survey the cards.
    if (!m_.bus.holdPending()) return;

    for (Board* b : m_.bus.boards()) {
        if (!b->requestsBus()) continue;
        BusMaster* dm = b->busMaster();
        if (!dm) continue;  // pulled pHOLD but has nothing to drive: ignore, not a grant

        // Drive it while it keeps the bus. A burst controller holds pHOLD for the
        // whole block and drains here in one grant; a cycle-stealing one drops the
        // request after a single transfer and re-arms via a Clock deadline, so the
        // CPU runs an instruction before the next grant. The loop does not know or
        // care which -- the board's own requestsBus() decides the grain.
        while (b->requestsBus()) {
            StepResult ds = dm->step(m_.bus);
            m_.clock.advance(ds.tStates);

            // A granted bus cycle costs time. A master that steals zero-time cycles
            // and never releases would wedge this loop -- the same failure the
            // StepResult sentinel ban guards against (DESIGN.md 3.1, 16). Stop
            // draining it; a real transfer always advances the clock.
            if (ds.tStates == 0) break;
        }
    }
}

// ---------------------------------------------------------------------------
// The run loop. This is the debugger, and it asks only generic questions.
// ---------------------------------------------------------------------------
RunResult Debugger::run(uint64_t maxSteps) {
    RunResult r;

    CpuCore* cpu = m_.cpu();
    BusMaster* master = m_.master();
    if (!cpu || !master) {
        // A BACKPLANE WITH NO PROCESSOR IN IT IS A REAL MACHINE, and it is the one
        // milestone 1a ran. So this is not an internal error -- it is a fact about
        // the machine, reported as one.
        r.why = StopReason::NoCpu;
        return r;
    }

    clearInterrupt();
    bool armed = armObserver();
    m_.running = true;

    for (;;) {
        StepResult s = master->step(m_.bus);
        ++r.steps;
        r.tStates += s.tStates;

        // EMULATED TIME ADVANCES HERE AND NOWHERE ELSE (DESIGN.md 7.5). Exactly
        // the T-states the CPU said it took -- so the UART's idea of when a
        // character has finished going out is derived from the same instruction
        // stream the guest is timing it with, and the two cannot drift.
        m_.clock.advance(s.tStates);

        // pHLDA: hand the bus to any board pulling pHOLD, now that we are at an
        // instruction boundary. Its stolen T-states are charged to the clock inside,
        // so the CPU genuinely loses time (DESIGN.md 4.5). Inert with no DMA card in
        // the machine. Ordered AFTER clock.advance so a cycle-steal re-arm deadline
        // that came due during this instruction is already live before we offer.
        serviceDma();

        // A CYCLE breakpoint fired somewhere inside that instruction. We finish the
        // instruction and stop at the boundary -- never mid-instruction, because
        // real hardware cannot do that either, and a half-executed instruction is
        // a corrupted machine wearing a debugger's clothes.
        if (armed && cycleHit_) {
            r.why = StopReason::Breakpoint;
            r.bp = cycleHit_;
            break;
        }

        // BREAK <addr>: PC equals X after a step. One comparison, and it knows
        // nothing about what CPU it is asking.
        uint16_t pc = cpu->pc();
        bool hit = false;
        for (Breakpoint& b : bps_) {
            if (!b.enabled || b.kind != BreakKind::Pc) continue;
            if (pc < b.lo || pc > b.hi) continue;
            ++b.hits;
            r.why = StopReason::Breakpoint;
            r.bp = b.id;
            hit = true;
            break;
        }
        if (hit) break;

        // STEP-OVER's one-shot target: PC reached the return address of a CALL/RST
        // the operator asked NEXT to run through. Same one comparison a PC
        // breakpoint is, but off the books -- see setStepTarget. A real breakpoint
        // above wins if the callee happens to hit one first, which is what you want.
        if (stepTarget_ >= 0 && pc == (uint16_t)stepTarget_) {
            r.why = StopReason::StepTarget;
            break;
        }

        // HLT with nobody to wake it is the end of the program. HLT with a live
        // interrupt source is NOT -- the CPU is parked, time still passes, and the
        // board that will interrupt it is clocked by the very T-states we are
        // still counting.
        //
        // AND NEITHER IS A HLT WITH A DEADLINE STILL ON THE BOOKS, which is the half
        // this used to get wrong. The old code asked "is anyone pulling pINT RIGHT
        // NOW?" and called that "can anything ever wake it?". They are not the same
        // question, and a 2SIO tells them apart: jumper its transmit interrupt, send
        // a character, and halt -- which is an entirely ordinary thing for a driver
        // to do. At the instant of the HLT nothing is pulling pINT, because the
        // character is still going out. The interrupt is two thousand T-states away
        // and absolutely certain to arrive. The old test declared that program
        // finished, and it would have been right about the wire and wrong about the
        // machine.
        //
        // The clock knows better: if anything at all is still scheduled, this
        // machine has a future. And it terminates -- a queue with nothing in it is
        // the honest definition of a machine that will never do anything again.
        if (s.status == RunStatus::Halted && !m_.bus.intPending() && m_.clock.queued() == 0) {
            r.why = StopReason::Halted;
            break;
        }

        if (g_interrupt.load()) {
            r.why = StopReason::Interrupted;
            break;
        }

        if (maxSteps && r.steps >= maxSteps) {
            r.why = StopReason::Steps;
            break;
        }
    }

    m_.running = false;
    disarmObserver();
    r.pc = cpu->pc();
    return r;
}

} // namespace altair
