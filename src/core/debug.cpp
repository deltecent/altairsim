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

        // HLT with nobody to wake it is the end of the program. HLT with a live
        // interrupt source is NOT -- the CPU is parked, time still passes, and the
        // board that will interrupt it is clocked by the very T-states we are
        // still counting. So we only stop if nothing can ever raise pINT.
        if (s.status == RunStatus::Halted && !m_.bus.intPending()) {
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
