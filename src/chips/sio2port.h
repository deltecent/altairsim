#pragma once
//
// Sio2Port -- a card's 6850 SERIAL SECTION, not a whole card.
//
// A CHIP IS NOT A CARD (src/chips/mc6850.h), AND A SERIAL SECTION IS NOT ONE EITHER.
// The 88-2SIO card is two 6850s, a base-port jumper, one card-owned deadline, and the
// glue that turns "IN 10h" into "read channel a's status register" -- and every OTHER
// card that carries a 6850 needs that same glue. The MITS Turnkey Module has one 6850
// on it; a card built next year might have three. Copying Sio2Board's body onto each is
// how the DCD latch gets fixed on one card and stays wrong on the others.
//
// So the glue lives here, once, parameterised by the CHANNELS the card actually has --
// a list of {name, port-offset}. One channel or four, the decode, the dispatch (even ->
// status/control, odd -> data), the single earliest-edge Clock deadline, the interrupt
// aggregation, connect/units/properties, and SNAPSHOT all come from this object.
//
// NOT A Board. It has no type(), no id, no bus. The owning card holds one as a member
// and forwards the bus/lifecycle calls to it -- see src/boards/mits-turnkey.cpp. It
// cannot reach the card's protected clock_ or intChanged(), so it is handed both at
// construction: a Clock* (forwarded from the card's clockAttached()) and a callback the
// card binds to its own intChanged().

#include "chips/mc6850.h"
#include "core/board.h"   // Reset, Property, UnitDef, IrqJumper, viBit, EndpointResolver

#include <functional>
#include <string>
#include <vector>

namespace altair {

class Sio2Port {
public:
    // One 6850 on the card, its name (for `id:name` MOUNT/CONNECT and SHOW) and where
    // it sits relative to the base port. Offsets are EVEN: a channel owns BASE+offset
    // (status/control) and BASE+offset+1 (data). The 2SIO has {"a",0} and {"b",2}.
    struct ChannelDef {
        std::string name;
        uint8_t     offset;
    };

    // `onIntChanged` is the owning card's intChanged(), bound by the card at
    // construction. The section drives it whenever a chip's IRQ pin may have moved,
    // because the section is not a Board and cannot reach pin 73 itself.
    Sio2Port(std::vector<ChannelDef> channels, std::function<void()> onIntChanged);
    ~Sio2Port();

    Sio2Port(const Sio2Port&)            = delete;  // a self-referential deadline lives
    Sio2Port& operator=(const Sio2Port&) = delete;  // in here; it must never be moved

    // WHERE THE ENDPOINT GRAMMAR STOPS (DESIGN.md 7.7). The program installs this once
    // in src/main.cpp; every card that embeds a Sio2Port shares the one resolver, so
    // the Turnkey needs no setResolver of its own. The section hands it down to a chip
    // when `connect` turns an endpoint string into a stream.
    static void setResolver(EndpointResolver r);

    // The card forwards its Clock here from clockAttached(). Null until then, and an
    // unclocked 6850 reads as a dead card rather than dereferencing null.
    void attachClock(Clock* c) { clock_ = c; }

    // The base-port jumper lives on the CARD (it is the card's `port` property); the
    // section is told where it landed.
    void    setBase(uint8_t b) { base_ = b; }
    uint8_t base() const { return base_; }

    // ---- bus (the card gates on cycle type + enabled, then asks these) ----
    bool    decodesPort(uint8_t port) const;
    uint8_t read(uint8_t port);            // dispatch + refresh
    void    write(uint8_t port, uint8_t data);

    // ---- pin 73 / VI0-VI7 ----
    bool    assertsInt() const;
    uint8_t assertsVi() const;

    // ---- lifecycle ----
    void reset(Reset r);
    void power() { reset(Reset::PowerOn); }
    void pump();
    void refresh();   // re-poll, re-drive the pin, re-arm the one deadline

    // ---- SNAPSHOT / RESTORE. The owning card calls these AFTER its own Board::
    // serialize()/its own fields, so the byte layout is [board...][chan0][chan1]...
    void serialize(StateWriter& w) const;
    void deserialize(StateReader& r);

    std::vector<std::string> drainLog();   // raw messages; the card adds its id prefix
    uint64_t rxBytes() const;

    // ---- reflection / units ----
    std::vector<UnitDef>  units() const;
    std::vector<Property> unitProperties(const std::string& unit);
    bool connect(const std::string& unit, const std::string& endpoint, std::string& err);
    bool disconnect(const std::string& unit, std::string& err);
    ByteStream* unitStream(const std::string& unit);

    Mc6850* channel(const std::string& name);
    const std::vector<ChannelDef>& channels() const { return defs_; }

private:
    int chanIndexForPort(uint8_t port, bool& isData) const;

    std::vector<ChannelDef> defs_;
    std::vector<Mc6850>     chans_;   // parallel to defs_, built once in the ctor
    std::function<void()>   onIntChanged_;

    uint8_t       base_  = 0x10;
    Clock*        clock_ = nullptr;
    Clock::Handle wake_  = Clock::kNone;
};

} // namespace altair
