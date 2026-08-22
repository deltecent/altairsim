#pragma once
//
// Intel 8253 (8253-5) Programmable Interval Timer -- a CHIP, NOT A CARD.
//
// Three independent 16-bit down-counters and a control-word register. The CompuPro
// System Support 1 carries one; the next card with an 8253 on it gets this for free.
// Modeled from the 8253 data sheet as reprinted in the System Support 1 manual
// (reference/CompuPro System Support 1.md sec. 3, reference/Intel 8253.md), NOT from
// any one program that drives it.
//
// It knows nothing about S-100. The CARD decodes A1/A0 into four ports -- counter 0/1/2
// (+0/+1/+2) and the control word (+3) -- and hands the accesses here. On the SS-1 the
// three counter clock inputs are all tied to the S-100 2 MHz signal (bus pin 49); that
// is the default here (`counterHz`), a strap for the J4-jumper external-clock case.
//
// ---------------------------------------------------------------------------
// TIME IS NOT STEPPED, IT IS COMPUTED (DESIGN.md 7.5). A real 8253 decrements each
// counter on every 2 MHz edge; doing that here would burn two million callbacks a
// second per counter for a chip nobody may be reading. Instead each counter remembers
// the T-state its count was loaded (`loadT_`) and the emulation DERIVES the current
// count and OUT level from how many counter-ticks have elapsed since -- exactly the
// deadline-not-a-loop stance the UART's transmitter and the Clock header describe.
//
// So the whole chip is a pure function of (the programmed state, clk.now()). readCounter
// computes; out() computes; nextEdge() -- the next T-state OUT flips, for the card's
// interrupt matrix -- computes. Nothing is scheduled here; the CARD arms one alarm when
// an OUT it routes to an enabled interrupt is going to move (Phase 4).
//
// ---------------------------------------------------------------------------
// WHAT IS MODELED, AND WHAT IS TIED OFF:
//
//   * Modes 0, 2, 3, 4 have their real OUT behavior; the count read-back is exact for
//     modes 0/2/4 and approximate for the square-wave mode 3 (a real 8253 decrements
//     the mode-3 count by two per tick and the read-back is rarely relied on -- OUT is
//     what feeds interrupts, and OUT is exact).
//   * Modes 1 and 5 are GATE-TRIGGERED one-shots. The SS-1 pulls every gate high and
//     ungated (§3.3), so no gate rising edge ever arrives -- a real board would never
//     trigger them, and here they idle their programmed initial OUT (high). This is the
//     faithful ungated behavior, not a stub.
//   * The gate inputs are pulled high (counting always enabled). No gate is modeled.
//   * BCD counting (control bit 0) is supported: the written count is BCD and the
//     read-back is BCD; the arithmetic runs in decimal with a 10000 modulus.

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class Clock;
class StateWriter;
class StateReader;

class Intel8253 {
public:
    explicit Intel8253(std::string name) : name_(std::move(name)) {}
    const std::string& name() const { return name_; }

    // ---- bus register access. The CARD decodes A1/A0 to these. ----
    void    writeControl(uint8_t v, const Clock& clk);   // +3, write-only
    void    writeCounter(int n, uint8_t v, const Clock& clk);  // +0/+1/+2
    uint8_t readCounter(int n, const Clock& clk);              // +0/+1/+2

    // POWER-ON: every counter mode 0, disarmed, OUT low. A real 8253's state is
    // indeterminate until programmed; low is the safe default and matches mode 0.
    void powerOn(const Clock& clk);

    // ---- straps (config; NOT serialized) ----
    long long counterHz = 2000000;  // the counter clock input. SS-1 ties it to 2 MHz.

    // ---- the OUT pins and live count, for the card's interrupt matrix and for SHOW ----
    bool     out(int n, const Clock& clk) const;    // the OUT pin level
    uint16_t count(int n, const Clock& clk) const;  // the count you'd read back (BCD if set)

    // The next absolute T-state at which counter n's OUT changes level, or 0 if it is
    // static from here (mode 0/4 past terminal count; ungated mode 1/5; disarmed). The
    // CARD's nextEdge() folds these in once an OUT feeds an enabled interrupt (Phase 4).
    uint64_t nextEdge(int n, const Clock& clk) const;

    std::string describe(const Clock& clk) const;  // "C0 mode2 count=1234 OUT=1; ..."

    void serialize(StateWriter& w) const;
    void deserialize(StateReader& r);

private:
    struct Counter {
        uint8_t  mode  = 0;      // 0-5 (control bits 3-1; 6->2, 7->3 alias as on silicon)
        bool     bcd   = false;  // control bit 0
        uint8_t  rl    = 1;      // read/load format: 1 LSB, 2 MSB, 3 LSB-then-MSB
        uint16_t initial = 0;    // the count as written (0 => the modulus, 65536 or 10000)

        // Write sequencing for the LSB-then-MSB format.
        bool     writeMsbNext = false;
        uint8_t  writeLsb     = 0;

        // Read sequencing (LSB-then-MSB) and the Counter Latch Command's frozen value.
        bool     readMsbNext = false;
        bool     latched     = false;
        uint16_t latchVal    = 0;

        uint64_t loadT_ = 0;      // the T-state counting began
        bool     armed  = false;  // a full count has been loaded -- counting is live
    };

    // The counting math, all in decimal counter-ticks (BCD is decoded at the edges).
    uint64_t elapsed(const Counter& c, const Clock& clk) const;  // ticks since load
    uint32_t modulus(const Counter& c) const;                    // 65536 or 10000
    uint32_t effectiveN(const Counter& c) const;                 // initial, 0 => modulus
    uint32_t liveCount(const Counter& c, const Clock& clk) const;  // decimal count now
    bool     outOf(const Counter& c, const Clock& clk) const;

    static uint16_t toBcd(uint32_t v);
    static uint32_t fromBcd(uint16_t v);

    std::string name_;
    Counter     c_[3];
};

}  // namespace altair
