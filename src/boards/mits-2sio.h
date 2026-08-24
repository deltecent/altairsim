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
// A PCB WITH CHIPS ON IT. The 6850 itself lives in src/chips/mc6850.h (a chip is
// not a card), and the SERIAL SECTION -- the two chips, the base-port jumper, the
// one card-owned deadline, and the glue that turns "IN 10h" into "read channel a's
// status register" -- lives in src/chips/sio2port.h. That section was extracted
// FROM this card so every other 6850-bearing board (turnkey, sbc, 88uio) could
// share it instead of copying it; this card now embeds one and forwards to it, the
// same as those. What is left HERE is only what makes it the 2SIO: the "2sio" type,
// two channels {"a",0} and {"b",2}, and the `port` jumper.
//
// Base port is a jumper: default 0x10, so channel A is 0x10 (control/status) and
// 0x11 (data), channel B is 0x12 and 0x13.

#include "chips/sio2port.h"
#include "core/board.h"

#include <memory>
#include <string>
#include <vector>

namespace altair {

class Sio2Board : public Board {
public:
    Sio2Board();

    std::string type() const override { return "2sio"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    // PIN 73 and VI0-VI7 -- the section aggregates the two chips' IRQ pins, filtered
    // by where each is jumpered. The card just forwards; the wire it drives is its own.
    bool    assertsInt() const override { return sio_.assertsInt(); }
    uint8_t assertsVi() const override { return sio_.assertsVi(); }

    void reset(Reset) override;
    void power() override;
    void pump() override { sio_.pump(); }

    // The card forwards its Clock to the section here -- the section is not a Board
    // and cannot reach clock_ itself (see mits-turnkey.h).
    void clockAttached() override { sio_.attachClock(clock_); }
    void configChanged() override;

    // SNAPSHOT/RESTORE (DESIGN.md 13). The section carries the two 6850s' state; the base
    // port is a strap. deserialize() re-arms the section's one deadline from the chip state
    // it just read -- no Clock::Handle is serialized.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // Both ports, because either can carry the transfer -- a byte on any line is a byte
    // arriving. (Board::rxBytes; the section sums its channels.)
    uint64_t rxBytes() const override { return sio_.rxBytes(); }

    // What the chips want said out loud -- today, only "the host cannot do that baud
    // rate". The section returns the raw messages; the card prefixes its id.
    std::vector<std::string> drainLog() override;

    std::vector<Property> properties() override;
    std::vector<Property> unitProperties(const std::string& unit) override {
        return sio_.unitProperties(unit);
    }

    std::vector<UnitDef>  units() const override { return sio_.units(); }
    std::vector<MapEntry> ioMap() const override;

    // `[board.unit.a]` in the config -- baud, interrupt, connect, per channel -- needs
    // NOTHING here. Unit properties are generic in the config layer, over units()/
    // unitProperties(), which is the same pair CONFIG SAVE writes them from.

    bool connect(const std::string& unit, const std::string& endpoint,
                 std::string& err) override;
    bool connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                       std::string& err) override {
        return sio_.connectStream(unit, std::move(s), err);
    }
    bool disconnect(const std::string& unit, std::string& err) override {
        return sio_.disconnect(unit, err);
    }

    // The endpoint resolver is the section's, shared with every SIO-bearing card. This
    // forwarder keeps `Sio2Board::setResolver(resolveEndpoint)` reading unchanged in
    // src/main.cpp (DESIGN.md 7.7).
    using EndpointResolver = altair::EndpointResolver;
    static void setResolver(EndpointResolver r) { Sio2Port::setResolver(std::move(r)); }

    Mc6850* channel(const std::string& name) { return sio_.channel(name); }

    // The connector behind a channel, for an operator that owns the endpoint (the MCP
    // console). Non-owning: the chip still owns the stream. See Board::unitStream.
    ByteStream* unitStream(const std::string& unit) override { return sio_.unitStream(unit); }

private:
    // The two 6850s, the base-port jumper, the one card-owned deadline, the interrupt
    // aggregation, connect/units/properties and SNAPSHOT all live in here now. The card
    // hands it the two channels the 2SIO has and binds its own intChanged() into it.
    Sio2Port sio_{{{"a", 0}, {"b", 2}}, [this] { intChanged(); }};
};

} // namespace altair
