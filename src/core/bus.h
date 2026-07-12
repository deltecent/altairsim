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
    uint8_t data = 0;   // valid on writes

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
// both. See DESIGN.md 10.2: the operator writes ROM through Board::rawWrite(),
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
    // left to trust: setVerifyDecode(true) re-derives the decode the slow way on
    // every single cycle and screams if it disagrees with the table. The test
    // suite and the CPU validation gate both run with it on.
    void invalidateDecode() { dirty_ = true; }

    // Paranoid mode: check every cached decode against a fresh one, every cycle.
    // Slower than the original code. That is fine -- it is a proof, not a path.
    void setVerifyDecode(bool on) { verify_ = on; }
    bool verifyDecode() const { return verify_; }

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

    // pINT (pin 73), carried as the WIRE-OR of every board asserting it. That is
    // all the bus does with an interrupt: it does not pick a winner and it does
    // not hand anyone a vector (DESIGN.md 4.4). If it did, the 88-VI board could
    // not be written AS A BOARD, and every machine without one would be getting
    // vectored behavior its hardware never had.
    //
    // The vector, if there is one, comes from whoever claims the IntAck CYCLE --
    // like any other cycle. Nobody claims it in a machine with no VI card, so the
    // bus floats to 0xFF and the 8080 executes RST 7. That is not a fallback
    // hack; it is what the metal does, and it is why the PMMI's factory jumper
    // straight to pin 73 yields RST 7 with no vector logic anywhere.
    bool intPending() const;

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

    std::vector<std::pair<int, Observer>> observers_;
    int nextObserver_ = 1;
};

} // namespace altair
