#pragma once
//
// The Altair 680b onboard I/O -- docs/boards/mits-680io.md,
// reference/Altair 680b Theory of Operation.md §4.
//
// THE 680b's OWN SERIAL PORT, and it is a SEPARATE board from the CPU (mits-680cpu.h)
// on purpose: the 6800 card decodes nothing, and everything the main board answers on
// the bus -- the console 6850 and the config-strap read port -- lives here. This is
// what makes the machine talk to a terminal.
//
// MEMORY-MAPPED, NOT PORT-MAPPED. The 680b is a 6800: there is no IN/OUT space, so the
// ACIA answers at ADDRESSES, not ports (Theory §4). Three fixed locations, decoded by
// the main board's eight-input NAND gates:
//
//     F000   6850 ACIA -- Status (read) / Control (write)     [A0 = 0 -> RS = 0]
//     F001   6850 ACIA -- RxData (read) / TxData (write)      [A0 = 1 -> RS = 1]
//     F002   hardware config straps -- READ ONLY (Theory §4.3)
//
// THE 6850 IS THE SAME CHIP AS EVERYWHERE (chips/mc6850.h), and the SAME serial section
// the Turnkey and the 2SIO carry (chips/sio2port.h) -- one channel `tty`, base 0, so
// address F000 -> section port 0 (status/control) and F001 -> section port 1 (data).
// Copying the dispatch/connect/interrupt/snapshot glue onto this card is how the DCD
// latch gets fixed on one card and stays wrong here, so it is reused whole.
//
// THE STRAP PORT (F002) is the answer to the Programming Manual's deliberately-blank
// address: the monitor reads it ONCE at reset (MON680.ASM: `LDAB STRAPS`) and uses two
// bits -- bit 7 = No-Terminal (set -> jump to 0000 and drive the front panel; CLEAR ->
// a terminal is present, program the ACIA), and bit 2 = stop bits (Operator's Manual
// §3). Default 0x00: terminal present, two stop bits. It is a tri-state buffer of
// jumper straps, so it is READ-ONLY -- a write to F002 does nothing.

#include "chips/sio2port.h"
#include "core/board.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class Io680Board : public Board {
public:
    Io680Board();

    std::string type() const override { return "680io"; }

    // ---- bus (memory-mapped) ----
    bool    decodes(const BusCycle&) const override;
    uint8_t read(const BusCycle&) override;
    void    write(const BusCycle&) override;

    // Three addresses in page F0, not the whole page -- so the bus must ask per address,
    // not cache one answer for F0xx. See Board::decodeIsPageUniform().
    bool decodeIsPageUniform() const override { return false; }

    // ---- interrupts (the ACIA's IRQ -> the 6800's IRQ, Theory §4.1) ----
    bool    assertsInt() const override { return sio_.assertsInt(); }
    uint8_t assertsVi() const override { return sio_.assertsVi(); }

    // ---- lifecycle ----
    void reset(Reset) override;
    void power() override;
    void pump() override { sio_.pump(); }
    void clockAttached() override { sio_.attachClock(clock_); }
    void configChanged() override;

    // ---- reflection ----
    std::vector<Property> properties() override;
    std::vector<MapEntry> memMap() const override;

    // ---- serial units: the 6850's `tty`, delegated to the section ----
    std::vector<UnitDef>  units() const override { return sio_.units(); }
    std::vector<Property> unitProperties(const std::string& unit) override {
        return sio_.unitProperties(unit);
    }
    bool connect(const std::string& unit, const std::string& endpoint, std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override {
        return sio_.disconnect(unit, err);
    }
    ByteStream* unitStream(const std::string& unit) override { return sio_.unitStream(unit); }
    uint64_t    rxBytes() const override { return sio_.rxBytes(); }
    std::vector<std::string> drainLog() override;

    // ---- SNAPSHOT / RESTORE (DESIGN.md 13) ----
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // The endpoint resolver is the section's (shared with every SIO-bearing card).
    static void setResolver(EndpointResolver r) { Sio2Port::setResolver(std::move(r)); }

private:
    // The 6850 answers F000/F001; the strap buffer answers F002 (read only).
    static constexpr uint16_t kAciaBase = 0xF000;
    static constexpr uint16_t kStraps   = 0xF002;

    // The hardware-programmable bits (Operator's Manual §3), read at F002. Bit 7 =
    // No-Terminal (0 = terminal present), bit 2 = stop bits. Set once by jumpers; a
    // machine/board property, never guest-writable.
    uint8_t straps_ = 0x00;

    // The onboard 6850, its one channel named `tty`, at section-port 0 (base 0).
    Sio2Port sio_;

    std::vector<std::string> log_;
};

} // namespace altair
