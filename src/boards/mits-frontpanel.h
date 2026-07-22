#pragma once
//
// The MITS Altair 8800 front panel -- docs/boards/mits-frontpanel.md.
//
// THE PANEL IS A CARD, AND THE SENSE SWITCHES ARE NOT A CONFIG VALUE.
//
// They used to be: `Machine::sense`, a byte on the machine, parsed from TOML,
// printed by SHOW MACHINE -- and wired to NOTHING. No board decoded port 0xFF, so
// `IN 0FFH` fell through to the floating bus and returned 0xFF forever, whatever
// the operator had configured. The DBL boot PROM reads bit 4 of it. It was reading
// a floating bus.
//
// The fix is not to teach the bus about a machine-level byte. It is to put the
// switches where the schematic puts them: on the Display/Control board, which is a
// card in the machine like any other (DESIGN.md 3).
//
// ---------------------------------------------------------------------------
// THE SENSE SWITCHES ARE THE TOP EIGHT ADDRESS SWITCHES. THERE IS ONE ROW.
//
// Not "eight switches that happen to be next to the address switches" -- the SAME
// EIGHT SWITCHES, SA8..SA15, and the reason is visible on schematic 880-106. The
// D/C board already had a bank of 7405 open-collector buffers whose whole job was
// strobing SA8..SA15 onto D0..D7: that is the HIGH BYTE OF THE JMP that EXAMINE
// jams into the processor (Theory of Operation, "EXM" -- count 1 strobes 303, the
// JMP opcode; count 2 strobes SA0..SA7; count 3 strobes SA8..SA15).
//
// The sense-switch input is that same bank, enabled a second time. So the panel
// gets an input port for the cost of one gate, and the operator gets eight
// switches that mean two things depending on what the machine is doing. That is
// not a quirk to be modeled around. It is why `sense` is a SLICE of sw_ below and
// not a field of its own -- two fields would let them disagree, and on the real
// panel they physically cannot.
//
// ---------------------------------------------------------------------------
// THE DECODE, FROM THE SCHEMATIC (880-106) AND NOT FROM MEMORY.
//
// An 8-input NAND (IC L, 7430) fed by A8..A15, gated with sINP. All eight high and
// an input cycle in progress -> the SA8..SA15 buffers drive the data bus.
//
//   * It is on A8..A15, not A0..A7, and that is NOT a second port. The 8080 puts
//     the port byte on BOTH halves of the address bus during IN, so decoding the
//     high half IS decoding the port. `port() == 0xFF` is the same wire.
//
//   * A FULL EIGHT-BIT DECODE. Every address line is in the NAND; there is no
//     don't-care and no mask. Port 0xFF and nothing else.
//
//   * INPUT ONLY. The enable is gated with sINP. There is no sOUT anywhere near
//     this bank -- an `OUT 0FFH` is not ours, goes unclaimed, and the byte is
//     discarded by the backplane, which is exactly what the hardware does with it.
//
// The Theory of Operation says the rest out loud: SSW DSB (bus pin 53) "disables
// the data input buffers so the input from the sense switches may be strobed onto
// the bidirectional data bus right at the processor... This is necessary since the
// sense switch inputs are tied directly to the bidirectional data bus at the
// processor." We model the byte, not the pin: nothing else in this machine can
// observe SSW DSB, because nothing else is inside the 8080's own data bus buffers.
//
// ---------------------------------------------------------------------------
// THE LAMPS ARE WIRED TO THE BACKPLANE. That is not a metaphor, and it is why this
// card needs no new bus concept to have LEDs: snoop() already hands a board every
// cycle that crosses the bus, which is precisely what an LED on a bus line sees.
//
// WHAT WE DO NOT LIGHT, AND WHY -- because the honest sentence here is the opposite
// of the 88-ACR's. The ACR has no motor control because THE CARD HAS NONE. This
// card HAS these lamps; the BUS does not carry what lights them:
//
//   * M1 -- the panel has an M1 lamp and we cannot light it. An opcode fetch and an
//     operand read are both Cycle::MemRead in BusCycle (core/bus.h); the 8080's
//     status byte distinguishes them and ours does not. Lighting it needs a
//     Cycle::Fetch or an m1 flag set by the CPU. Not done, not faked.
//
//   * INTE, PROT, WAIT, HLDA, HLTA, STACK -- these are PINS, on the processor and
//     on the memory cards. They are not bus cycles and snoop() will never see them.
//     INTE is the 8080's interrupt-enable flip-flop; WAIT and HLDA are what the
//     panel does TO the CPU, not what it watches.
//
// So: MEMR, INP, OUT and WO* are derived from the cycle and are true. The rest are
// absent rather than wrong. When a graphical panel wants them, the fix is on the
// CPU and the bus, and it will be a real fix -- not a lookup table on this card.
//
// AND THE MANUAL'S OWN DISCLAIMER, which is the best argument for not overfitting
// this: "While running a program, however, LEDs may appear to give erroneous
// indications." (Operator's Manual, INDICATOR LEDs.) The lamps show the last cycle
// that went by. At 2 MHz that is a blur, and it was a blur in 1975.

#include "core/board.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

// The status lamps we can honestly derive from a bus cycle. See the header note
// above for the ones that are missing and what it would take to light them.
enum StatusLamp : uint8_t {
    LampMemR = 1 << 0,  // the cycle is a memory read
    LampInp  = 1 << 1,  // ...an input cycle
    LampOut  = 1 << 2,  // ...an output cycle
    LampWo   = 1 << 3,  // WO* is ACTIVE LOW on the panel: lit means WRITE.
    LampInt  = 1 << 4,  // INTA -- the interrupt acknowledge cycle
};

class FrontPanelBoard : public Board {
public:
    std::string type() const override { return "fp"; }

    // Port 0xFF, read, and nothing else. See the decode note above.
    bool decodes(const BusCycle& c) const override {
        return enabled_ && c.type == Cycle::IoRead && c.port() == 0xFF;
    }

    // SA8..SA15 onto D0..D7.
    uint8_t read(const BusCycle&) override { return sense(); }

    // A READ WITH NO SIDE EFFECT IS A PEEK, and this one qualifies: eight switches
    // and a buffer. Nothing latches, nothing is consumed. (peek() is memory-only,
    // so this card never gets asked -- but the contract in board.h is about side
    // effects, and it is worth being able to say we honor it.)

    bool wantsSnoop() const override { return true; }
    void snoop(const BusCycle& c) override;

    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;

    // A TOGGLE IS A TOGGLE. Nothing on a real panel moves a switch except a finger,
    // and neither RESET* nor POC* is a finger. Both resets are deliberately absent.

    // ...but the LAMPS go out. There is no light without power.
    void power() override;

    // ---- The switch row, and the lamps. The graphical panel reads THESE. ----

    uint16_t switches() const { return sw_; }
    void setSwitches(uint16_t v) { sw_ = v; }

    // A8..A15 -- what `IN 0FFH` returns.
    uint8_t sense() const { return (uint8_t)(sw_ >> 8); }
    void setSense(uint8_t v) { sw_ = (uint16_t)((sw_ & 0x00FF) | ((uint16_t)v << 8)); }

    uint16_t addressLamps() const { return addrLeds_; }
    uint8_t dataLamps() const { return dataLeds_; }
    uint8_t statusLamps() const { return status_; }

    // SNAPSHOT/RESTORE (DESIGN.md 13). The switch row is machine-visible state -- the
    // guest reads the sense switches at port FF, and only a finger moves them, so
    // they survive resets and must travel. The lamp latches travel too; the next
    // snooped cycle would refresh them, but restoring them makes the panel look right
    // the instant the machine stops.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

private:
    // ONE ROW OF SIXTEEN. SA0..SA15. The low eight are the DATA switches the panel
    // deposits with; the high eight are the sense switches. Model the row once and
    // they cannot drift apart, which is the situation on the actual sheet metal.
    uint16_t sw_ = 0x0000;

    // The lamp latch. Written ONLY in snoop(), which is the clocked half of the
    // board interface (board.h) -- the combinational halves, decodes() and
    // assertsPhantom(), must stay pure and this is why the LEDs are not updated
    // from read().
    uint16_t addrLeds_ = 0;
    uint8_t  dataLeds_ = 0;
    uint8_t  status_   = 0;
};

} // namespace altair
