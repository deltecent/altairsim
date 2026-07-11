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
#include <string>
#include <vector>

namespace altair {

class Board;

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
    std::vector<Board*> decoders(const BusCycle& c) const;
    void reportContention(const BusCycle& c, const std::vector<Board*>& who);

    // Third pass, once per cycle: show the completed cycle to every board.
    // The bus is not notifying anyone -- the cycle was on the backplane the whole
    // time and they could all see it. This is only where we let them LATCH it.
    void settle(const BusCycle& c);

    std::vector<Board*> boards_;
    std::vector<std::string> log_;
    Contention policy_ = Contention::Warn;
    bool unclaimed_ = false;
};

} // namespace altair
