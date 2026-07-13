#pragma once
//
// 88-2SIO -- MITS Dual Serial Interface. Two Motorola 6850 ACIAs on one card.
// See docs/boards/mits-2sio.md.
//
// THE PROOF VEHICLE. A fully-modeled 2SIO exercises every interface in the
// design at once -- ByteStream, units, per-unit properties, interrupts, multiple
// instances of one board -- which is why it is the only peripheral in milestone
// 1. If the interfaces are wrong, this is where it shows.
//
// A PCB WITH CHIPS ON IT, and since Phase 3 the file says so: the 6850 itself
// lives in src/chips/mc6850.h, because a chip is not a card and the next card
// with a 6850 on it should not have to reimplement the DCD latch. What is left
// here is what the CARD does -- where the ports are, where the IRQ is jumpered,
// which chip answers which address.
//
// Base port is a jumper: default 0x10, so channel A is 0x10 (control/status) and
// 0x11 (data), channel B is 0x12 and 0x13.

#include "chips/mc6850.h"
#include "core/board.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

// `IrqJumper` -- where a channel's IRQ is soldered -- is a BUS strap, and lives in
// core/board.h with the rest of pin 73's vocabulary (DESIGN.md 4.4).

class Sio2Board : public Board {
public:
    Sio2Board();
    ~Sio2Board() override;

    std::string type() const override { return "2sio"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    // PIN 73, combinational and pure. What the chips are asking for, filtered by
    // what is actually soldered to the wire.
    bool assertsInt() const override;

    // ...and the same for VI0-VI7. The two 6850s are independently strapped, so
    // this card can be pulling two lines at once -- hence a bitmask.
    uint8_t assertsVi() const override;

    void reset(Reset) override;
    void power() override;
    void pump() override;
    void configChanged() override;

    // What the chips want said out loud -- today, only "the host cannot do that baud
    // rate". Virtual on Board since 59a175b, which is what makes this possible at all.
    std::vector<std::string> drainLog() override;

    std::vector<Property> properties() override;
    std::vector<Property> unitProperties(const std::string& unit) override;

    std::vector<UnitDef> units() const override;
    std::vector<MapEntry> ioMap() const override;

    // `[board.unit.a]` in the config -- baud, interrupt, connect, and every
    // transform, per channel. The TOML loader already splits the dotted name and
    // hands us `unit = "a"`; it does not know what a channel is, and does not
    // need to.
    std::vector<std::string> subUnitTables() const override { return {"unit"}; }
    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;

    bool connect(const std::string& unit, const std::string& endpoint,
                 std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;

    // The monitor resolves an endpoint string to a stream; the BOARD is not
    // allowed to know what a socket is (DESIGN.md 7.7). This is how the one gets
    // handed to the other -- and the card passes it on down to the chips, which
    // are not allowed to know either. The alias is the chip layer's type, so
    // `Sio2Board::setResolver(resolveEndpoint)` in main.cpp reads unchanged.
    using EndpointResolver = altair::EndpointResolver;
    static void setResolver(EndpointResolver r);

    Mc6850* channel(const std::string& name);

private:
    // EVERYTHING THAT COULD HAVE MOVED PIN 73 HAS JUST HAPPENED. Advance the
    // receivers, re-drive the pin, and set the alarm clock for the next moment
    // either chip could move it with nobody touching it.
    //
    // Called after every register access, on pump(), on reset, when a jumper moves
    // -- and from the alarm clock itself, which is what lets the card act while the
    // CPU is halted waiting for it to.
    void refresh();

    Mc6850 a_{"a"};
    Mc6850 b_{"b"};
    uint8_t base_ = 0x10;

    // The one outstanding deadline, for whichever chip's edge comes first. ONE, not
    // one per chip: a card has a state, and re-deriving the earliest edge from
    // scratch on every refresh is both simpler and impossible to leak.
    Clock::Handle wake_ = Clock::kNone;
};

} // namespace altair
