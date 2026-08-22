#pragma once
//
// Intel 8259A Programmable Interrupt Controller -- a CHIP, NOT A CARD.
//
// Eight prioritized interrupt request inputs (IR0-IR7), a mask, an in-service
// register, and the priority logic that turns "some lines are asking" into "put THIS
// vector on the bus". The CompuPro System Support 1 carries TWO of these in a
// master/slave cascade; the next card with an 8259A on it gets this for free. Modeled
// from the 8259A data sheet as reprinted in the System Support 1 manual
// (reference/CompuPro System Support 1.md sec. 4, reference/Intel 8259A.md), NOT from
// any one program that drives it.
//
// It knows nothing about S-100. The CARD decodes A0 into two ports (+0 and +1), routes
// the eight IR inputs from wherever the board's jumpers send them, and drives the INT
// output pin at pin 73 or into a master's IR line. The card also runs the
// interrupt-acknowledge sequence -- see acknowledge()/callAddress() below.
//
// ---------------------------------------------------------------------------
// THE INT OUTPUT IS A PURE FUNCTION OF (state, the LIVE IR input levels).
//
// The eight IR lines are wires the CARD drives from other chips -- a timer OUT, a
// UART's RxRDY, an S-100 VI line -- and every one of those is itself computed live
// (the 8253 derives its OUT from clk.now(); a VI line is a wire-OR the bus keeps). So
// this chip does NOT cache the request lines: winner()/intOut() TAKE the current
// levels as an argument and compute, exactly the way the 88-VI's assertsInt() reads
// bus.viLines() live. That is what lets the card's assertsInt() stay const and pure
// (DESIGN.md 4.4.1) while its inputs move on other chips' clocks.
//
// The one piece of state that is NOT a pure function of the inputs is the EDGE latch,
// for edge-triggered mode (ICW1 LTIM=0): a rising edge on an IR sets a latch that
// holds until the interrupt is acknowledged. senseEdges() updates it; the CARD calls
// that at its sampling points (refresh()/pump()). In LEVEL-triggered mode -- the
// System Support 1's documented default (ICW1 = 1D, LTIM=1) -- the request register
// simply follows the live pins and senseEdges() is harmless bookkeeping.
//
// ---------------------------------------------------------------------------
// WHAT IS MODELED, AND WHAT IS TIED OFF:
//
//   * ICW1-ICW4 initialization sequencing; OCW1 (mask), OCW2 (the EOI/rotate family),
//     OCW3 (read-register select and special mask mode).
//   * Fully-nested priority with rotation (set-priority and rotate-on-EOI). Special
//     Fully Nested Mode (ICW4 bit 4) is accepted but treated as ordinary fully-nested;
//     it only changes how a master handles two interrupts from ONE slave, an edge the
//     stock System Support 1 wiring (one slave) does not exercise.
//   * MCS-80/85 mode: the acknowledge drives a 3-byte CALL (callAddress()), which is
//     what an 8080/8085/Z80 executes. 8086 mode (ICW4 bit 0) is accepted and
//     vector8086() gives the single vector byte, but this simulator has no 8086 core,
//     so that path has no consumer -- see the .cpp.
//   * Poll mode (OCW3 P=1) and automatic-EOI (ICW4 bit 1) are NOT modeled: the manual's
//     own sample program (sec. 4.3) uses neither, and Intel advises against auto-EOI in
//     a cascade. Selecting them is a no-op documented at the write site.

#include <cstdint>
#include <string>

namespace altair {

class StateWriter;
class StateReader;

class Intel8259a {
public:
    explicit Intel8259a(std::string name) : name_(std::move(name)) {}
    const std::string& name() const { return name_; }

    // ---- bus register access. The CARD decodes A0 to these. ----
    void    write(bool a0, uint8_t v);
    uint8_t read(bool a0, uint8_t live) const;  // A0=1 -> IMR; A0=0 -> IRR or ISR (OCW3)

    // POWER-ON: everything masked (IMR = FF), nothing in service, init sequence idle.
    // A real 8259A is indeterminate until ICW1; all-masked is the safe default and means
    // an unprogrammed controller never drives an interrupt.
    void powerOn();

    // ---- the interrupt logic, computed from the LIVE IR input levels ----
    int  winner(uint8_t live) const;    // highest serviceable request, or -1
    bool intOut(uint8_t live) const { return winner(live) >= 0; }  // the INT pin

    // EDGE bookkeeping for edge-triggered mode; the CARD calls this at its sample points.
    // A no-op to intOut() in level-triggered mode (but it still tracks the pins).
    void senseEdges(uint8_t live);

    // ACKNOWLEDGE: latch the winner into ISR (and, in edge mode, drop its request
    // latch). Returns the level acknowledged, or -1 if nothing is serviceable.
    int  acknowledge(uint8_t live);

    // The MCS-80/85 CALL target for a level -- ICW2 (A15-A8) plus, from ICW1, the
    // address-interval bits and the level. This is the address bytes 2 and 3 of the
    // acknowledge CALL; byte 1 is always the CALL opcode (0xCD), which the card drives.
    uint16_t callAddress(int level) const;

    // The single vector byte an 8086 would read (ICW2 high bits | level). No 8086 core
    // exists here, so nothing consumes this today -- it is written for completeness and
    // tested at the chip level.
    uint8_t vector8086(int level) const;
    bool    is8086() const { return (icw4_ & 0x01) != 0; }

    // ---- for the card's SHOW and for tests ----
    uint8_t  imr() const { return imr_; }
    uint8_t  isr() const { return isr_; }
    uint8_t  irr(uint8_t live) const { return effectiveIrr(live); }
    bool     irEnabled(int ir) const { return (imr_ & (uint8_t)(1u << (ir & 7))) == 0; }
    bool     initialized() const { return icw1Seen_; }
    std::string describe(uint8_t live) const;

    void serialize(StateWriter& w) const;
    void deserialize(StateReader& r);

private:
    // The request register the priority logic sees: the live pins in level mode, the
    // edge latch in edge mode.
    uint8_t effectiveIrr(uint8_t live) const { return ltim_ ? live : edge_; }

    // The in-service bit of highest priority (honoring rotation), or -1.
    int highestIsr() const;

    void ocw2(uint8_t v);  // the EOI / rotate / set-priority family

    std::string name_;

    // ---- initialization command words, and what we cracked out of them ----
    uint8_t icw1_ = 0, icw2_ = 0, icw3_ = 0, icw4_ = 0;
    bool    ltim_ = false;  // ICW1 D3: 1 = level-triggered, 0 = edge-triggered
    bool    adi4_ = false;  // ICW1 D2: 1 = call-address interval 4, 0 = interval 8
    bool    sngl_ = false;  // ICW1 D1: 1 = single (no ICW3), 0 = cascade
    bool    ic4_  = false;  // ICW1 D0: 1 = ICW4 will follow
    int     initStep_  = 0; // 0 = done/idle, else the next ICW expected (2, 3 or 4)
    bool    icw1Seen_  = false;

    // ---- the live registers ----
    uint8_t imr_  = 0xFF;   // OCW1: the mask. Power-on = all masked.
    uint8_t isr_  = 0;      // in-service
    uint8_t edge_ = 0;      // edge-mode request latch
    uint8_t pins_ = 0xFF;   // last IR levels seen, for edge detection (FF = "edge sense
                            // reset": a currently-high line needs a fresh rising edge)
    int     lowPri_ = 7;    // rotation: IR(lowPri_+1) is highest priority. 7 = fully nested.
    bool    readIsr_ = false;  // OCW3: A0=0 reads ISR (true) or IRR (false)
    bool    smm_ = false;      // special mask mode (OCW3)
};

}  // namespace altair
