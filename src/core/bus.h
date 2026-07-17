#pragma once
//
// The S-100 bus (DESIGN.md 4) -- the load-bearing spec.
//
// THE RULE THIS FILE EXISTS TO ENFORCE:
//   The bus carries signals and moves bytes. It does not invent behavior.
//
// It arbitrates no overlay, vectors no interrupt, knows no bank, and has never
// heard of ROM. Every one of those lives in a board. When you are tempted to
// add "if (board is a ROM)" here, you have found a bug in your board instead.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace altair {

class Board;
class Bus;

enum class Cycle { MemRead, MemWrite, IoRead, IoWrite, IntAck };

struct BusCycle {
    Cycle type = Cycle::MemRead;
    uint16_t addr = 0;  // memory address; for I/O the port is addr & 0xFF
    // Valid on writes. On a READ it is zero while the cycle is in flight -- nobody
    // has driven the bus yet when decodes() and read() are asked -- but it is
    // BACK-FILLED with the byte that came back (a board's, or the floating bus's
    // 0xFF) before snoop() and the observers see it. See Bus::settle().
    uint8_t data = 0;

    // Set by the bus's first pass, from whatever board pulled PHANTOM* (pin 67).
    // A board strapped to honor it takes ITSELF off the bus for this cycle --
    // the bus picks no winner. That is the entire overlay mechanism.
    bool phantom = false;

    uint8_t port() const { return (uint8_t)(addr & 0xFF); }
    bool isWrite() const { return type == Cycle::MemWrite || type == Cycle::IoWrite; }
};

// NOTE there is deliberately NO `origin` field (Cpu/Monitor/Dma). A real
// backplane cycle carries no such tag -- which is exactly WHY a front-panel
// DEPOSIT is indistinguishable from a CPU write, and why a real ROM ignores
// both. See DESIGN.md 10.2: the operator writes ROM through Machine::burn(),
// which is a PROM burner and not a bus operation at all.

enum class Reset {
    PowerOn,  // POC* -- Altair bus pin 76. Board-specific; the board just needs
              //   to know it happened. Guest code cannot see it.
    Bus       // RESET* -- the front-panel button.
};
// NEITHER RESET CLEARS MEMORY. Only removing power does (DESIGN.md 6). A RAM
// chip has no POC* pin.

enum class Contention { Silent, Warn, Error };

// ---------------------------------------------------------------------------
// BOARDS RESPOND TO BUS CYCLES. A BUS MASTER ORIGINATES THEM. (DESIGN.md 3)
//
// Two concepts, and a CPU card is both. This lives with the bus and not with the
// CPU on purpose: S-100 has pHOLD/pHLDA precisely BECAUSE a backplane can have
// more than one master, so when a DMA card arrives (a Dazzler, a disk controller
// stealing cycles) it is a Board that BECOMES a BusMaster when granted the bus,
// driving the very same cycles through the very same interface. DMA is then not
// a bolted-on path through the bus -- it is the mechanism the CPU already uses.
// ---------------------------------------------------------------------------
enum class RunStatus {
    Ok,      // an instruction retired
    Halted,  // HLT. The CPU is still powered and still watching pINT.
};

// An explicit result, NEVER a sentinel T-state count (DESIGN.md 3.1, 16). The
// prototype's `return 0 means something went wrong` is exactly how the RLC/RRC
// bug hid: a legal instruction that happened to take zero cycles was
// indistinguishable from a failure, so nobody looked.
struct StepResult {
    uint32_t tStates = 0;
    RunStatus status = RunStatus::Ok;
};

class BusMaster {
public:
    virtual ~BusMaster() = default;
    virtual StepResult step(Bus&) = 0;
};

// A row of SHOW BUS MAP / SHOW BUS IO.
struct MapEntry {
    uint32_t lo = 0, hi = 0;  // inclusive
    std::string what;         // "ram", "rom", "read/write"
    std::string note;         // "bank 3", "phantom:all", "empty socket"
};

class Bus {
public:
    void attach(Board* b);
    void detach(Board* b);
    const std::vector<Board*>& boards() const { return boards_; }

    // ---- The decode is CACHED, because on real hardware it is WIRED ----
    //
    // A card's address decoder is combinational logic -- a PAL, a row of gates --
    // wired to the address lines and to the status lines sMEMR/sINP/sOUT. It does
    // not "answer a question" per cycle. It settles, and it only CHANGES when
    // something latches: a bank strap, PHANTOM*, a card pulled from the backplane.
    //
    // Re-deriving it on every cycle was costing more than everything else in the
    // simulator combined, AND it was asking cards questions they are not even
    // wired for: an I/O-only 2SIO was asked to decode every memory read, and its
    // first act was to throw the question away (mits-2sio.cpp:296). A real 2SIO
    // has no connection to the memory read strobe. It is not in that conversation.
    //
    // So the bus asks the SAME questions -- decodes(), assertsPhantom(), of every
    // board, in slot order -- and asks them ONCE, storing the answer. The board
    // still owns the entire decision. The bus still invents nothing. It just
    // stopped asking sixty-five thousand times a second.
    //
    // A board whose decode changes MUST say so (Board::decodeChanged()). If one
    // forgets, the tables go stale and the machine lies quietly -- so that is not
    // left to trust: setVerify(true) re-derives the decode the slow way on
    // every single cycle and screams if it disagrees with the table. The test
    // suite and the CPU validation gate both run with it on.
    void invalidateDecode() { dirty_ = true; }

    // Paranoid mode: re-derive the decode AND the interrupt wire the slow way, and
    // check them against what we cached -- on every cycle and every instruction.
    // Slower than the original code. That is fine: it is a proof, not a path.
    void setVerify(bool on) { verify_ = on; }
    bool verify() const { return verify_; }

    // The four cycles. Each is a two-pass affair:
    //   pass 1: ask every board whether it pulls PHANTOM* -> BusCycle::phantom
    //   pass 2: ask every board whether it decodes; exactly one should answer
    // Carrying a signal, then running a decode. No decisions.
    uint8_t memRead(uint16_t addr);
    void memWrite(uint16_t addr, uint8_t data);
    uint8_t ioRead(uint8_t port);
    void ioWrite(uint8_t port, uint8_t data);

    // An unvectored interrupt acknowledge floats to 0xFF, which the 8080 reads
    // as RST 7. Same floating-bus rule as unmapped memory -- not a special case.
    uint8_t intAck();

    // ---- pINT (pin 73) -- A WIRE, NOT A POLL ----
    //
    // "In a real system, the bus doesn't poll a board for interrupt status. The
    //  board sets high/low signals on the bus that the CPU reads from the bus. The
    //  board then clears the int signal based on its design." (Patrick, 2026-07-12)
    //
    // He was describing the hardware; he was also describing a bug. This used to
    // walk the backplane and ask every card `assertsInt()` -- ONCE PER INSTRUCTION,
    // sixty million times a second, to compute a boolean that changes about a
    // thousand times a second on a busy machine. It cost more per instruction than
    // the entire rest of the bus once the decode was cached.
    //
    // Now a board PULLS the pin (Board::intChanged()) and the bus keeps the
    // wire-OR as a running count. Reading pin 73 is an integer test. The bus still
    // does exactly what it did before -- carry the OR of every board asserting it,
    // pick no winner, hand out no vector (DESIGN.md 4.4) -- it just stopped
    // conducting a survey to find out what was already on the wire.
    //
    // The vector, if there is one, comes from whoever claims the IntAck CYCLE --
    // like any other cycle. Nobody claims it in a machine with no VI card, so the
    // bus floats to 0xFF and the 8080 executes RST 7. That is not a fallback hack;
    // it is what the metal does, and it is why the PMMI's factory jumper straight
    // to pin 73 yields RST 7 with no vector logic anywhere.
    bool intPending() const;

    // A board's pin moved. Called by Board::intChanged(), and by nothing else.
    void intWireChanged(bool pulling) { intCount_ += pulling ? 1 : -1; }

    // ---- pHOLD (pin 74) -- A WIRE, EXACTLY LIKE pin 73 ----
    //
    // A DMA card PULLS pHOLD and HOLDS it, and the run loop reads the wire at an
    // instruction boundary -- it does NOT interrogate every card per instruction
    // asking "do you want the bus?". That is the same poll §4.4.1 tore out for the
    // interrupt line, and it would be the same mistake: a bus does not survey its
    // cards, it reads what they have driven. So the bus keeps a running wire-OR count
    // (holdWireChanged(), from Board::holdChanged()) and the answer is one integer
    // test. Only when SOMEONE is pulling does the run loop walk the backplane to find
    // who -- and grant them in slot order (DESIGN.md 4.5).
    //
    // Read every instruction by the run loop's serviceDma(), so -- exactly like
    // intPending()/verifyInt() -- it is where setVerify(true) re-derives the wire the
    // slow way and aborts if a board changed its pHOLD and forgot to say so.
    bool holdPending() const;

    // A board's pHOLD moved. Called by Board::holdChanged(), and by nothing else.
    void holdWireChanged(bool pulling) { holdCount_ += pulling ? 1 : -1; }

    // ---- VI0-VI7 (pins 4-11) -- EIGHT MORE WIRES, CARRIED AND NOT ARBITRATED ----
    //
    // The bus does for these exactly what it does for pin 73 and NOT ONE THING MORE:
    // it carries the wire-OR of every board pulling each line, and it hands out no
    // vector, picks no winner and applies no mask. Priority IS a chip -- an 8214, on
    // an 88-VI card -- and pretending the backplane knows about it would be the same
    // mistake §4.4 warns about with PHANTOM*.
    //
    // So an 88-VI is an ordinary board that happens to READ these wires, drive pin 73
    // itself, and claim the IntAck cycle. With no 88-VI in the machine the eight
    // lines go nowhere, which is what an empty slot does.
    uint8_t viLines() const;

    // A board's VI pins moved: it WAS pulling `before`, it is NOW pulling `after`.
    // Called by Board::intChanged(), and by nothing else.
    void viWireChanged(uint8_t before, uint8_t after) {
        uint8_t was = viMask_;
        for (int i = 0; i < 8; ++i) {
            uint8_t bit = (uint8_t)(1u << i);
            if ((before & bit) == (after & bit)) continue;
            viCount_[i] += (after & bit) ? 1 : -1;
        }
        recomputeVi();
        if (viMask_ != was) notifyViWatchers();
    }

    // ---- The cycle stream (DESIGN.md 3.0.3, 4.2.2) ----
    //
    // Every board already sees every cycle -- that is what a backplane IS. An
    // observer watches the SAME stream from outside the backplane, and that is
    // the whole implementation of BREAK IO, BREAK MEM, TRACE and HISTORY.
    //
    // So those are NOT CPU features and must never live in a core: they are
    // questions about bus cycles, they are answered here, and an 8085 or a Z80
    // inherits every one of them on the day it lands without writing a line.
    using Observer = std::function<void(const BusCycle&)>;
    int observe(Observer fn);   // returns a handle
    void unobserve(int handle);

    // Look without running a cycle -- for DISASM, TRACE and the debugger's
    // display, none of which are allowed to have side effects. Runs the SAME
    // decode (PHANTOM* and all), so a shadowed board is invisible to it exactly
    // as it is to a real read; it just never strobes anybody. Floats to 0xFF when
    // nobody can answer, which is what the bus would have done anyway.
    uint8_t peek(uint16_t addr) const;

    // Reverse lookup: who answers here, and why. Backs WHO and the contention
    // detector. Returns every board that ACTUALLY decodes -- so a board that is
    // phantomed out does not appear, which is why a shadow is not contention.
    std::vector<Board*> respondersTo(const BusCycle& c) const;

    void setContentionPolicy(Contention p) { policy_ = p; }
    Contention contentionPolicy() const { return policy_; }

    // Every message the bus has emitted (contention, discarded writes). The
    // monitor prints these; MCP returns them as structured data.
    const std::vector<std::string>& drain() const { return log_; }
    void clearLog() { log_.clear(); }

    // The last cycle answered nobody. DEPOSIT uses this to say "byte discarded"
    // out loud instead of silently doing nothing -- silence here is a bug that
    // takes hours to find.
    bool lastUnclaimed() const { return unclaimed_; }

    // More than one board answered the last cycle. Contention is a BUS fact (unlike
    // origin, which is not -- see BusCycle) and already computed on every cycle; the
    // observer reads it so TRACE's CONTENTION mask and HISTORY can flag the cycle.
    bool lastContended() const { return contended_; }

private:
    bool anyAssertsPhantom(const BusCycle& c) const;

    // THE SAME QUESTION AS decoders(), WITHOUT THE VECTOR.
    //
    // decoders() returns its answer by value, so asking it costs a heap
    // allocation -- and the cycle functions ask it on EVERY guest read and EVERY
    // guest write. That malloc/free pair was measured at two thirds of the cost of
    // a memory access (72ns -> 26ns when removed), which is to say: most of the
    // time this simulator spent was spent allocating a vector to hold the number 1.
    //
    // Nothing about the MODEL changes here. Every board is still asked, in slot
    // order, and the board still owns the whole decision. We just stopped putting
    // the answer on the heap.
    //
    // One decoder is the overwhelming case, so that is the case with no allocation
    // at all. Contention is rare, already a fault, and about to print a line of
    // text -- it can afford decoders().
    struct Decode {
        Board* first = nullptr;  // the first board in slot order that drives
        int n = 0;               // how many drive. >1 is contention.
    };
    Decode scan(const BusCycle& c) const;

    std::vector<Board*> decoders(const BusCycle& c) const;
    void reportContention(const BusCycle& c, const std::vector<Board*>& who);

    // ---- The cached decode ----
    //
    // One entry per 256-byte page (memory) or per port (I/O), per cycle class.
    // Four tables, because a card is wired to sMEMR, sINP and sOUT separately and
    // a ROM region famously does not decode a WRITE at all -- so "who answers
    // here" has a different answer for a read than for a write, and that fell out
    // of the model rather than being bolted onto it.
    struct Slot {
        Board* who = nullptr;   // the single board that drives. null: nobody -> floats 0xFF
        bool phantom = false;   // PHANTOM* as resolved for this page and this cycle class
        bool slow = false;      // more than one driver. Contention: take the exact path.
    };

    // MEMORY DECODE IS PAGE-GRANULAR (256 bytes), and that is a CONTRACT on
    // Board::decodes(), written down in board.h. It is not a guess: real S-100
    // memory decoding is done from the high address lines at 1K granularity at the
    // very finest, and our own MemoryBoard is built on a 256-entry page map
    // already. I/O needs no such contract -- all 256 ports are stored exactly.
    Slot memRead_[256], memWrite_[256];
    Slot ioRead_[256], ioWrite_[256];

    Slot resolve(Cycle t, uint16_t addr) const;
    void rebuild();
    void verifySlot(const BusCycle& c, const Slot& s) const;
    void verifyInt() const;
    void verifyHold() const;

    // pINT as a WIRE-OR: how many enabled boards are pulling it down right now.
    // Maintained by intWireChanged(); never recomputed on the hot path.
    int intCount_ = 0;

    // pHOLD as a WIRE-OR: how many enabled boards want the bus right now. Maintained
    // by holdWireChanged(); read once per instruction as holdPending(), so a machine
    // with no DMA card in it pays a single integer test and never a per-board poll.
    int holdCount_ = 0;

    // The same, eight times over, for VI0-VI7 -- plus the bitmask an 88-VI actually
    // reads, kept in step so that viLines() is a load and not a loop.
    int     viCount_[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t viMask_     = 0;
    void    recomputeVi() {
        uint8_t m = 0;
        for (int i = 0; i < 8; ++i)
            if (viCount_[i] > 0) m |= (uint8_t)(1u << i);
        viMask_ = m;
    }
    void verifyVi() const;

    // A VI line moved, so every card WATCHING those lines has a pin 73 that may have
    // moved with it. The guard is not paranoia: a watcher's intChanged() can itself
    // land back here (an 88-VI whose own RTC sits on a VI line), and one bounce is
    // enough -- its VI output does not depend on the VI inputs, so it settles.
    // (Out of line: Board is only forward-declared up here.)
    bool inViNotify_ = false;
    void notifyViWatchers();

    // The exact path. THIS IS THE DEFINITION OF CORRECTNESS; the tables above are
    // a cache OF it, and the verifier checks them AGAINST it.
    uint8_t memReadExact(uint16_t addr);
    void memWriteExact(uint16_t addr, uint8_t data);
    uint8_t ioReadExact(uint8_t port);
    void ioWriteExact(uint8_t port, uint8_t data);

    bool dirty_ = true;
    bool verify_ = false;

    // Boards that WATCH cycles they do not answer. Almost none do -- the Tarbell
    // does -- so calling snoop() on every card on every cycle was N virtual calls
    // to reach a do-nothing default. They opt in with Board::wantsSnoop().
    std::vector<Board*> snoopers_;

    // Third pass, once per cycle: show the completed cycle to every board.
    // The bus is not notifying anyone -- the cycle was on the backplane the whole
    // time and they could all see it. This is only where we let them LATCH it.
    void settle(const BusCycle& c);

    std::vector<Board*> boards_;
    std::vector<std::string> log_;
    Contention policy_ = Contention::Warn;
    bool unclaimed_ = false;
    bool contended_ = false;

    std::vector<std::pair<int, Observer>> observers_;
    int nextObserver_ = 1;
};

} // namespace altair
