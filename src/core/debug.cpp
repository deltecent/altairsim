#include "core/debug.h"

#include "core/machine.h"
#include "cpu/cpu.h"

#include <atomic>
#include <cctype>
#include <cstdio>
#include <vector>

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
    case BreakKind::TapeStop: return "tape stop";
    }
    return "?";
}

const DeviceEvent* deviceEventForKind(BreakKind k) {
    for (const DeviceEvent& de : kDeviceEvents)
        if (de.bk == k) return &de;
    return nullptr;
}

const char* breakActionName(BreakAction a) {
    switch (a) {
    case BreakAction::Stop:     return "stop";
    case BreakAction::TraceOn:  return "trace on";
    case BreakAction::TraceOff: return "trace off";
    }
    return "?";
}

std::string Breakpoint::describe() const {
    // A device-event breakpoint has no address to print -- it is a named hardware event,
    // not a place on the bus. Render it "<kind> <action>" lowercased from the one table,
    // so a new member names itself here the moment its row lands (no drift with the parser).
    if (const DeviceEvent* de = deviceEventForKind(kind)) {
        std::string s;
        for (const char* p = de->kind; *p; ++p) s += (char)std::tolower((unsigned char)*p);
        s += ' ';
        for (const char* p = de->action; *p; ++p) s += (char)std::tolower((unsigned char)*p);
        if (action != BreakAction::Stop) s += std::string(" ") + breakActionName(action);
        return s;
    }

    char buf[80];
    bool io = (kind == BreakKind::IoRead || kind == BreakKind::IoWrite);
    int w = io ? 2 : 4;
    if (lo == hi)
        std::snprintf(buf, sizeof buf, "%-6s %0*X", breakKindName(kind), w, (unsigned)lo);
    else
        std::snprintf(buf, sizeof buf, "%-6s %0*X-%0*X", breakKindName(kind), w, (unsigned)lo,
                      w, (unsigned)hi);
    std::string s = buf;
    if (cond) s += " if " + cond->text();
    // Mirrors the order it was typed in: BREAK 100 IF A==3 TRACE ON. Stop is the
    // default and saying so on every ordinary breakpoint would be noise on every
    // line of the listing.
    if (action != BreakAction::Stop) s += std::string(" ") + breakActionName(action);
    return s;
}

int Debugger::add(BreakKind k, uint32_t lo, uint32_t hi, std::shared_ptr<const Expr> cond,
                  BreakAction action) {
    Breakpoint b;
    b.id = nextId_++;
    b.kind = k;
    b.lo = lo;
    b.hi = hi;
    b.cond = std::move(cond);
    b.action = action;
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

// A board -> small handle, for the HISTORY "who" columns. The hot path: a one-entry
// cache (the same board answered the last cycle), then a short scan of the boards seen
// so far this run, then -- only on a board's first appearance -- the id string is
// interned into the stable name table. So a warmed recorder does no allocation and, in
// the common case, one pointer compare. See the members' comment in debug.h.
int Debugger::internBoard(Board* b) {
    if (!b) return -1;
    if (b == internMru_) return internMruH_;
    for (const auto& pr : internPtrs_)
        if (pr.first == b) {
            internMru_ = b;
            internMruH_ = pr.second;
            return pr.second;
        }
    // First time this board has driven or answered this run. Give its id a stable
    // handle -- reusing one from an earlier run if the same id is back -- and remember
    // the pointer so the rest of this run answers from the scan above.
    int h = -1;
    for (size_t i = 0; i < boardHandles_.size(); ++i)
        if (boardHandles_[i] == b->id) {
            h = (int)i;
            break;
        }
    if (h < 0) {
        h = (int)boardHandles_.size();
        boardHandles_.push_back(b->id);
    }
    internPtrs_.emplace_back(b, h);
    internMru_ = b;
    internMruH_ = h;
    return h;
}

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
    // ALWAYS ARMED while running, now -- not only when a cycle breakpoint is set.
    // HISTORY is a flight recorder that has to be running BEFORE the stop it explains,
    // and TRACE can be turned on for a run that has no breakpoints at all. The three
    // jobs share one observer so a cycle costs one std::function call, not three.
    // (The monitor's own DUMP/DEPOSIT are still safe: the observer exists only for as
    // long as a run does, so a front-panel poke while stopped records nothing.)
    cycleHit_ = 0;
    // The pointer->handle map is only good for one run (a Board* does not outlive the
    // machine that held it). Drop it and the cache; the id NAME table (boardHandles_)
    // survives, so a record still in the ring from a previous run keeps its name.
    internPtrs_.clear();
    internMru_ = nullptr;
    internMruH_ = -1;
    observer_ = m_.bus.observe([this](const BusCycle& c) {
        CycleRec rec;
        rec.type = c.type;
        rec.addr = c.addr;
        rec.data = c.data;
        rec.dma = inDma_;
        rec.contended = m_.bus.lastContended();
        rec.t = m_.clock.now();
        // WHO drove it, WHO answered -- interned to handles here, at record time. A
        // non-DMA cycle originates at the CPU, which needs no board entry (-1 renders
        // "cpu"); a DMA grant names the board serviceDma() handed the bus to.
        rec.master = (int16_t)(inDma_ ? internBoard(dmaMaster_) : -1);
        rec.responder = (int16_t)internBoard(m_.bus.lastResponder());

        // HISTORY: overwrite-oldest ring.
        if (ring_.size() < kHistoryCap) {
            ring_.push_back(rec);
        } else {
            ring_[ringHead_] = rec;
            ringHead_ = (ringHead_ + 1) % kHistoryCap;
        }

        // Cycle breakpoints: the CYCLE kinds only. BREAK <addr> is a PC comparison
        // and lives in the run loop; these watch the stream itself.
        //
        // MATCHED BEFORE THE TRACE LINE IS EMITTED, and the order is the feature: a
        // tracepoint can turn tracing on RIGHT HERE, and the cycle that turned it on
        // is the one you wanted to see. BREAK MEM W 2000 TRACE ON puts the write to
        // 2000 at the top of the trace instead of one cycle above it.
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

            // A tracepoint acts and the machine runs on -- so it does NOT set
            // cycleHit_, and a Stop breakpoint on the same cycle still stops.
            if (b.action != BreakAction::Stop) {
                traceActive_ = (b.action == BreakAction::TraceOn);
                continue;
            }
            if (!cycleHit_) cycleHit_ = b.id;   // the FIRST one to fire wins the report
        }

        // TRACE: one line per cycle the mask admits.
        if (tracing() && traceShows(rec)) *traceSink_ << formatCycle(rec, boardHandles_) << "\n";
    });
    return true;
}

// TRACE's filter. An empty mask shows everything; otherwise a cycle is shown if any
// of its categories is selected. IN/OUT/IRQ come off the cycle type; DMA and
// contention are carried on the record.
bool Debugger::traceShows(const CycleRec& r) const {
    if (traceMask_ == 0) return true;
    unsigned cat = 0;
    switch (r.type) {
    case Cycle::IoRead:  cat |= InCycle;  break;
    case Cycle::IoWrite: cat |= OutCycle; break;
    case Cycle::IntAck:  cat |= Irq;      break;
    default: break;
    }
    if (r.dma) cat |= Dma;
    if (r.contended) cat |= Contended;
    return (cat & traceMask_) != 0;
}

std::string Debugger::formatCycle(const CycleRec& r, const std::vector<std::string>& handles) {
    const char* what = "?";
    bool io = false, none = false;
    switch (r.type) {
    case Cycle::MemRead:  what = "MR";  break;
    case Cycle::MemWrite: what = "MW";  break;
    case Cycle::IoRead:   what = "IN";  io = true;   break;
    case Cycle::IoWrite:  what = "OUT"; io = true;   break;
    case Cycle::IntAck:   what = "INTA"; none = true; break;
    }

    char buf[80];
    if (none)
        std::snprintf(buf, sizeof buf, "%10llu  %-4s      = %02X", (unsigned long long)r.t, what,
                      r.data);
    else if (io)
        std::snprintf(buf, sizeof buf, "%10llu  %-4s   %02X = %02X", (unsigned long long)r.t, what,
                      (unsigned)(r.addr & 0xFF), r.data);
    else
        std::snprintf(buf, sizeof buf, "%10llu  %-4s %04X = %02X", (unsigned long long)r.t, what,
                      (unsigned)r.addr, r.data);

    // WHO drove it -> WHO answered. A handle indexes the name table; -1 is a literal:
    // the CPU for a master (a normal cycle originates there), and the floating bus --
    // nobody -- for a responder. An out-of-range handle (a table that lost the id)
    // shows "?" rather than reading past the end.
    auto name = [&handles](int16_t h, const char* dflt) -> std::string {
        if (h < 0) return dflt;
        if ((size_t)h < handles.size()) return handles[(size_t)h];
        return "?";
    };
    // Master padded to a fixed width so the responder column lines up row to row; the
    // responder is last (bar an optional [DMA]/[CONTENTION] flag) and is NOT padded, so
    // an ordinary line carries no trailing whitespace.
    char who[48];
    std::snprintf(who, sizeof who, "   %-8s -> %s", name(r.master, "cpu").c_str(),
                  name(r.responder, "--").c_str());

    std::string s = buf;
    s += who;
    if (r.dma) s += "  [DMA]";
    if (r.contended) s += "  [CONTENTION]";
    return s;
}

std::vector<Debugger::CycleRec> Debugger::history(size_t n) const {
    size_t have = ring_.size();
    if (n > have) n = have;
    bool full = (have == kHistoryCap);
    size_t start = full ? ringHead_ : 0;   // oldest
    size_t skip = have - n;                // keep the LAST n
    std::vector<CycleRec> out;
    out.reserve(n);
    for (size_t i = skip; i < have; ++i) out.push_back(ring_[(start + i) % have]);
    return out;
}

void Debugger::clearHistory() {
    ring_.clear();
    ringHead_ = 0;
}

std::vector<Debugger::InsnRec> Debugger::insnHistory(size_t n) const {
    size_t have = insnRing_.size();
    if (n > have) n = have;
    bool full = (have == kInsnHistoryCap);
    size_t start = full ? insnRingHead_ : 0;   // oldest
    size_t skip = have - n;                     // keep the LAST n
    std::vector<InsnRec> out;
    out.reserve(n);
    for (size_t i = skip; i < have; ++i) out.push_back(insnRing_[(start + i) % have]);
    return out;
}

void Debugger::clearInsnHistory() {
    insnRing_.clear();
    insnRingHead_ = 0;
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
            // Tag every cycle this grant drives as DMA for the observer -- the
            // BusCycle itself carries no origin (bus.h), but the loop that handed out
            // the bus knows exactly whose cycles these are, and which board it is.
            inDma_ = true;
            dmaMaster_ = b;
            StepResult ds = dm->step(m_.bus);
            inDma_ = false;
            dmaMaster_ = nullptr;
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

    // DEVICE-EVENT BREAKPOINTS (BREAK TAPE STOP, and its future siblings). Are any armed?
    // If none are, we never poll the boards for them and a machine without one pays
    // nothing -- the same spirit as `armed` guarding the cycle observer. If one IS armed,
    // drain every board's edge latch ONCE here to sync it to the here-and-now, so a device
    // already sitting at its mark from an earlier load does not fire the instant this run
    // begins -- only a fresh arrival during THIS run does (core/board.h takeAutoStop).
    bool watchDeviceEvent = false;
    for (const Breakpoint& b : bps_)
        if (b.enabled && deviceEventForKind(b.kind)) { watchDeviceEvent = true; break; }
    if (watchDeviceEvent)
        for (const auto& bd : m_.boards()) bd->takeAutoStop();

    // Reflected registers for BREAK <addr> IF <expr>. The RegDef list is snapshotted
    // ONCE -- it is stable across a run (its get() closures read live state), so a
    // conditional breakpoint costs no per-step allocation -- and looked up by name,
    // so it never learns what an 8080 is and a Z80 inherits it (DESIGN.md 3.0.3).
    std::vector<RegDef> regs = cpu->registers();
    Expr::Resolver resolveReg = [&regs](const std::string& name, uint32_t& out) -> bool {
        for (const RegDef& rd : regs) {
            if (rd.name.size() != name.size()) continue;
            bool eq = true;
            for (size_t i = 0; i < name.size(); ++i)
                if (std::toupper((unsigned char)rd.name[i]) !=
                    std::toupper((unsigned char)name[i])) {
                    eq = false;
                    break;
                }
            if (eq) {
                out = rd.get();
                return true;
            }
        }
        return false;
    };

    for (;;) {
        // The bus cannot see the PC, so hand it this instruction's address before the
        // cycles run -- it is what an unclaimed-port warning names (DESIGN.md 4.6.1).
        // Captured here, at the boundary, because by the time an OUT cycle fires the
        // CPU's own PC has already stepped past the opcode and its operand.
        m_.bus.setInstrPc(cpu->pc());

        // CPU HISTORY: snapshot the machine as it stands right now -- the same state STEP
        // would print for the instruction about to run -- into the instruction ring. The
        // sibling of the bus flight recorder in armObserver(); always on while running.
        // A ring slot's regs vector keeps its capacity across wraps, so a warmed ring
        // records without allocating. Reuses the once-snapshotted `regs` for the getters.
        {
            InsnRec* slot;
            if (insnRing_.size() < kInsnHistoryCap) {
                insnRing_.emplace_back();
                slot = &insnRing_.back();
            } else {
                slot = &insnRing_[insnRingHead_];
                insnRingHead_ = (insnRingHead_ + 1) % kInsnHistoryCap;
            }
            slot->pc = cpu->pc();
            slot->regs.resize(regs.size());
            for (size_t i = 0; i < regs.size(); ++i) slot->regs[i] = regs[i].get();
            slot->nbytes = 3;
            for (uint8_t k = 0; k < 3; ++k)
                slot->bytes[k] = m_.bus.peek((uint16_t)(slot->pc + k));
        }

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

        // SET BUS UNCLAIMED=HALT: the guest reached an I/O port no board decodes.
        // The bus armed this during the cycle; we stop at the boundary, exactly as a
        // cycle breakpoint does, and for the same reason (DESIGN.md 4.6.1).
        if (m_.bus.takeUnclaimedHalt()) {
            r.why = StopReason::Unclaimed;
            r.port = m_.bus.haltPort();
            r.write = m_.bus.haltWasWrite();
            break;
        }

        // BREAK TAPE STOP: a cassette deck reached its auto-stop mark since the last
        // boundary. There is no bus cycle for "the tape ran out", so unlike a MEM/IO
        // breakpoint we POLL the boards here, at the boundary, exactly as the unclaimed
        // halt above does. Every board is drained each pass (read-and-clear keeps their
        // edge latches in step); the event is DECK-NEUTRAL, so any deck reaching its mark
        // fires a BREAK TAPE STOP. Then act just like the PC path below: count the hit,
        // flip trace for a tracepoint, stop otherwise.
        if (watchDeviceEvent) {
            bool tapeStopped = false;
            for (const auto& bd : m_.boards())
                if (bd->takeAutoStop()) tapeStopped = true;
            if (tapeStopped) {
                bool stop = false;
                for (Breakpoint& b : bps_) {
                    if (!b.enabled || b.kind != BreakKind::TapeStop) continue;
                    ++b.hits;
                    if (b.action != BreakAction::Stop) {
                        traceActive_ = (b.action == BreakAction::TraceOn);
                        continue;
                    }
                    r.why = StopReason::TapeStop;
                    r.bp = b.id;
                    stop = true;
                }
                if (stop) break;
            }
        }

        // BREAK <addr>: PC equals X after a step. One comparison, and it knows
        // nothing about what CPU it is asking.
        uint16_t pc = cpu->pc();
        bool hit = false;
        for (Breakpoint& b : bps_) {
            if (!b.enabled || b.kind != BreakKind::Pc) continue;
            if (pc < b.lo || pc > b.hi) continue;
            // A conditional breakpoint that does not hold is not a stop -- and it does
            // not count as a hit either. `hits` means "times it ACTED".
            if (b.cond && !b.cond->eval(resolveReg)) continue;
            ++b.hits;

            // A tracepoint flips TRACE and we keep going. It must NOT break out of
            // this loop: an ordinary breakpoint at the same PC still has to stop, and
            // a tracepoint that swallowed it would be a debugger hiding a breakpoint
            // from you. PC lands here BEFORE the instruction at this address runs, so
            // BREAK 100 TRACE ON traces the instruction at 100, and BREAK 200 TRACE
            // OFF does not trace the one at 200 -- the region is [100,200).
            if (b.action != BreakAction::Stop) {
                traceActive_ = (b.action == BreakAction::TraceOn);
                continue;
            }

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
