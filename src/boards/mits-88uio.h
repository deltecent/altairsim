#pragma once
//
// MITS 88-UIO -- the Universal Input/Output board (docs/boards/mits-88uio.md).
//
// TWO CARDS ON ONE CARD. The 88-UIO puts a serial port and an audio-cassette (ACR)
// section on a single S-100 board, separately addressed, sharing only the address-
// decode PROM and the power supply. To the guest they are two ordinary peripherals
// that happen to occupy one slot -- and that is exactly how this class is built:
//
//   * THE CASSETTE HALF IS AN 88-ACR, VERBATIM. Same 1602-family UART (an AY-5-1013A
//     here, a COM2502 on the ACR -- the same part by another name, see the .md), same
//     inverted status word, same 300-baud FSK modem, same tape machinery. So this card
//     DERIVES from AcrBoard and inherits every bit of it: the tape MOUNT/UNMOUNT, the
//     WIND/REWIND verbs, the live tape counter, the WAV codec, SNAPSHOT of the head
//     position. What it CHANGES about the cassette is exactly two things the plain
//     88-ACR does not have and this board's manual documents that it does:
//
//       - MOTOR CONTROL. An OUT to the cassette status port drives a relay wired to the
//         recorder's "Remote" jack: D7 low = motor ON (OUT 6,127), D6 low = motor OFF
//         (OUT 6,191), contacts normally closed at power-up. The 88-ACR has none of this
//         -- there the operator pressed PLAY with a finger (mits-88acr.h). The UIO has a
//         switch on the board, so modelling it is reproducing hardware, not inventing it.
//
//       - A SW-1-SELECTED MODULATION. The plain ACR's modem is MITS-only (2400/1850) and
//         REFUSES Kansas City tapes, because its PLL physically cannot hear a 1200 Hz
//         space. The UIO's SW-1 selects MITS *or* Kansas City (2400/1200), so it reads
//         and writes exactly the one its switch is set to -- modem() below overrides
//         AcrBoard's to return the chosen format.
//
//   * THE SERIAL HALF IS A 6850, and it is the SAME reusable section the MITS Turnkey
//     Module carries (chips/sio2port.h): one Mc6850 at a base port, decode + dispatch +
//     one Clock deadline + interrupt aggregation, all in an object the card forwards its
//     bus/lifecycle calls to. So it is an EMBEDDED MEMBER here, exactly as on the
//     Turnkey -- see mits-turnkey.cpp, which this card mirrors line for line on the
//     serial side.
//
// So decodes()/read()/write()/interrupts/units/serialize each ask BOTH halves: the
// inherited SioBoard for the cassette ports (0x06/0x07 by default) and the embedded
// Sio2Port for the serial ports (0x10/0x11 by default). The two ranges are disjoint,
// and both are operator-settable (SW-2 moves the serial pair, SW-3 the cassette pair).

#include "boards/mits-88acr.h"
#include "chips/sio2port.h"

#include <string>
#include <vector>

namespace altair {

class UioBoard : public AcrBoard {
public:
    UioBoard();

    std::string type() const override { return "uio"; }

    // ---- The bus. Each question asks the serial section first, then the cassette
    //      (the inherited SioBoard) for its own two ports. ----
    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    // ---- pin 73 / VI0-VI7: OR the two sections. Either can be strapped and asking. ----
    bool    assertsInt() const override;
    uint8_t assertsVi() const override;

    // ---- lifecycle: both halves ----
    void reset(Reset r) override;
    void pump() override;
    void clockAttached() override { serial_.attachClock(clock_); }
    void configChanged() override;

    // ---- reflection: the cassette's (minus nothing) plus the serial section's ----
    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;

    std::vector<UnitDef>  units() const override;
    std::vector<Property> unitProperties(const std::string& unit) override;
    bool connect(const std::string& unit, const std::string& endpoint, std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;
    ByteStream* unitStream(const std::string& unit) override;

    uint64_t                 rxBytes() const override;
    std::vector<std::string> drainLog() override;

    // ---- SNAPSHOT / RESTORE: AcrBoard (UART + tape) then the serial section then the
    //      one runtime latch this card adds -- the motor relay. ----
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // Shared with every SIO-bearing card (the cassette half refuses CONNECT, so it needs
    // no resolver of its own -- only the serial section does).
    static void setResolver(altair::EndpointResolver r) { Sio2Port::setResolver(std::move(r)); }

    // For the tests, so they can read the motor relay without going through SHOW.
    bool motorOn() const { return motorOn_; }

protected:
    // THE MODEM SW-1 SELECTS. One format, because the switch picks one -- MITS 2400/1850
    // (OFF) or Kansas City 2400/1200 (ON), both already shipping constants (tapemodem.h).
    // A tape in the other standard is REFUSED, exactly as on the plain ACR; the switch is
    // which one this card can hear, not "try both".
    std::vector<TapeFormat> modem() const override;

private:
    // The name of the serial unit, and the true cassette port. `serial` is CONNECTed;
    // the inherited `tape` unit is MOUNTed.
    static bool isSerial(const std::string& unit);

    // TWO SECTIONS ON ONE CARD MUST NOT OVERLAP. Each owns a 2-port pair (BASE, BASE+1),
    // and because decodes()/read()/write() ask the serial section FIRST, an overlap would
    // let it silently shadow the cassette -- and the bus's cross-BOARD conflict check
    // cannot see it, because the clash is inside one card. So both base setters reject it.
    // The pairs [a,a+1] and [b,b+1] overlap iff their bases are within one of each other.
    static bool portPairsOverlap(uint8_t a, uint8_t b) {
        return (a > b ? (uint8_t)(a - b) : (uint8_t)(b - a)) <= 1;
    }

    // ONE 6850, base 0x10 (== 2SIO Port A) by default -- SW-2 moves it to 0x18. The
    // section drives the card's intChanged() when a chip's IRQ pin may have moved.
    Sio2Port serial_{{{"serial", 0}}, [this] { intChanged(); }};

    // THE TAPE-RECORDER MOTOR RELAY. Normally closed at power-up (the manual). Latched
    // from D6/D7 of an OUT to the cassette status port; runtime state, so it travels in a
    // snapshot. Tape motion is byte-driven here (host/tape.h), so at the default
    // rate=full the motor is cosmetic -- but the register is real and must be swallowed
    // cleanly, never corrupting the UART underneath it.
    bool motorOn_ = true;

    // SW-1. A hardware switch, so it is config (set from the machine file, re-applied on
    // load), not snapshot state. "mits" | "kansas".
    std::string standard_ = "mits";
};

} // namespace altair
