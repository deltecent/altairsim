#pragma once
//
// Altair 680b KCACR -- the Kansas City Audio Cassette Record interface.
// reference/Altair 680b KCACR.md.
//
// THE CASSETTE BOARD IS AN 88-ACR THAT GREW A MOTOR AND MOVED HOUSE. Its serial
// engine is the same 1602-family UART, its tape machinery is the same MOUNT /
// WIND / REWIND / WAV codec, and its recording is the same Kansas City FSK the
// 88-UIO's SW-1 selects. So this board DERIVES from AcrBoard and inherits every
// one of those (mits-88acr.h): the tape unit, the live counter, the auto-stop,
// SNAPSHOT of the head position. What the KCACR CHANGES from the 88-ACR is exactly
// the three things its manual documents that the plain ACR does not have -- and
// all three are the 6800's world, not the S-100's:
//
//   * IT IS MEMORY-MAPPED, NOT PORTED. The 6800 has no IN/OUT space, so the two
//     registers live in ordinary memory and are reached with LDA/STA. F010 is
//     Status(read)/Control(write); F011 is Read/Write Data. decodes()/read()/
//     write() answer MemRead/MemWrite at those two addresses, not I/O ports.
//     (The 88-UIO's cassette half at 0x06/0x07 is the ported cousin.)
//
//   * EVERY BIT IS ACTIVE-LOW -- "True = Logic 0" (reference section 2). RDA, TBE,
//     both interrupt enables and both motor bits assert as 0. So the status byte
//     is built here rather than borrowing SioBoard::statusByte(), whose layout is
//     the SIO's own inverted-differently word.
//
//   * IT HAS MOTOR CONTROL AND INTERRUPTS. Control D7=0 turns the recorder motor
//     on, D6=0 off; D0=0 enables the Read-Data interrupt, D1=0 the Transmit-Buffer
//     interrupt, and either enabled condition pulls the 6800 IRQ. The interrupt
//     enables auto-clear on any register access or a Motor-Off write (reference
//     section 4), which is how the handler acknowledges. The two enable flip-flops
//     are the inherited inIntEnabled_/outIntEnabled_ (read/write here); the motor
//     relay is this board's one added latch, mirroring the 88-UIO.
//
// The loader/punch PROM at FD00 is NOT on this board -- it is a ROM in the machine's
// memory map (like MON680 at FF00), so nothing about it is here. This board is the
// F010/F011 register facade plus the tape transport underneath it.

#include "boards/mits-88acr.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class KcacrBoard : public AcrBoard {
public:
    KcacrBoard();

    std::string type() const override { return "680kcacr"; }

    // ---- bus (memory-mapped, active-low) ----
    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    // Two scattered addresses in page F0, never a whole page -- ask per address.
    bool decodeIsPageUniform() const override { return false; }

    // ---- interrupts: an enabled RDA/TBE pulls the 6800 IRQ (reference section 4) ----
    bool    assertsInt() const override;
    uint8_t assertsVi() const override { return 0; }  // the 680b has no S-100 VI lines

    // ---- lifecycle: SioBoard's, plus the motor relay's power-up state ----
    void reset(Reset r) override;

    // ---- reflection: the tape's, minus the SIO's electrical straps, plus motor ----
    std::vector<Property> properties() override;
    std::vector<MapEntry> memMap() const override;
    std::vector<MapEntry> ioMap() const override { return {}; }  // not a ported board

    // ---- SNAPSHOT / RESTORE: AcrBoard (UART + int-enables + tape) then the motor ----
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // For the tests, so they can read the motor relay without going through SHOW.
    bool motorOn() const { return motorOn_; }

protected:
    // THE ONE MODEM THIS BOARD HAS: Kansas City Standard (2400/1200, 300 baud),
    // reference section 6. AcrBoard's own modem() is MITS 2400/1850 and refuses a
    // KC tape; the KCACR is the board that reads and writes KC, so it selects that
    // format -- the same shipping constant the 88-UIO's SW-1=kansas returns.
    std::vector<TapeFormat> modem() const override;

private:
    static constexpr uint16_t kStatusCtrl = 0xF010;  // read = status, write = control
    static constexpr uint16_t kData       = 0xF011;  // read = RX data, write = TX data

    // The status byte in the KCACR's active-low convention: not-asserted bits read 1,
    // an asserted condition reads 0. D0 = Read Data Available, D7 = Transmit Buffer
    // Empty; D1-D6 are unused and read 1.
    uint8_t statusByte() const;

    // THE TAPE-RECORDER MOTOR RELAY. Driven by control D7 (on) / D6 (off); normally
    // closed at power-up, the same power-up assumption the 88-UIO makes. Runtime
    // state, so it travels in a snapshot. Tape motion is byte-driven (host/tape.h),
    // so at the default rate=full the relay is cosmetic -- but the register is real
    // and must be swallowed without disturbing the UART underneath it.
    bool motorOn_ = true;
};

} // namespace altair
