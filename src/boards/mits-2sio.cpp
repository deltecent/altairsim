#include "boards/mits-2sio.h"

#include "core/statefile.h"
#include "host/endpoint.h"

#include <utility>
#include <vector>

namespace altair {

// ---------------------------------------------------------------------------
// Sio2Board -- the 2SIO card. Every serial concern is delegated to the Sio2Port
// section it embeds (src/chips/sio2port.h); what is left here is the card: the
// decode gate, the `port` jumper, and rebasing a machine-file path against the
// config dir before the section's resolver opens it.
// ---------------------------------------------------------------------------

Sio2Board::Sio2Board() {
    // The section rebases a declarative `[sio0.unit.a] connect = "in:tape.tap"` (applied
    // through the chip's `connect` property) against the machine file's dir; the card's
    // connect() rebases the CONNECT command separately. (mits-turnkey.cpp.)
    sio_.setRebase([this](const std::string& p) { return resolvePath(p); });
}

// Four ports: BA+0..BA+3. Channel A is BA+0 (control/status) and BA+1 (data);
// channel B is BA+2 and BA+3. The card gates on cycle type + enabled; the section
// owns which addresses within that are its.
bool Sio2Board::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    return sio_.decodesPort(c.port());
}

uint8_t Sio2Board::read(const BusCycle& c) { return sio_.read(c.port()); }

void Sio2Board::write(const BusCycle& c) { sio_.write(c.port(), c.data); }

// ---------------------------------------------------------------------------
// A BUS RESET DOES NOTHING TO A 6850 -- IT HAS NO RESET PIN (mc6850.h). RESET* reaches
// this card's address decoding and NOTHING ELSE. POWER-ON-CLEAR is different: the machine
// was switched on, and the chips get put in a known good state. The section handles both,
// and re-drives pin 73 either way because that wire is the CARD's. (mits-turnkey.cpp,
// chips/sio2port.cpp:reset.)
// ---------------------------------------------------------------------------
void Sio2Board::reset(Reset r) { sio_.reset(r); }

void Sio2Board::power() { reset(Reset::PowerOn); }

// A jumper moved: `port` (which may have moved the card in I/O space), or a SIO strap --
// `interrupt`, `baud`, `connect` -- which moved a deadline the section has set.
void Sio2Board::configChanged() {
    decodeChanged();  // `port` may have moved the card in the I/O space
    sio_.refresh();   // ...and a SIO strap moved a deadline; re-drive the pin, re-aim it
}

// What the chips have to say -- today, only "this cable cannot do that baud rate". The
// section returns the raw messages; the card stamps its id on each.
std::vector<std::string> Sio2Board::drainLog() {
    std::vector<std::string> out;
    for (auto& s : sio_.drainLog()) out.push_back(id + ":" + s);
    return out;
}

void Sio2Board::serialize(StateWriter& w) const {
    Board::serialize(w);
    sio_.serialize(w);  // [board...][chan a][chan b] -- the layout a bare 2SIO always wrote
}

void Sio2Board::deserialize(StateReader& r) {
    Board::deserialize(r);
    sio_.deserialize(r);  // re-drives pin 73 and re-arms the deadline from the restored state
}

std::vector<Property> Sio2Board::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name    = "port";
        x.help    = "Base address. The board decodes four ports: BASE+0 .. BASE+3";
        x.kind    = Kind::Int;
        x.radix   = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min     = 0;
        x.max     = 0xFC;
        x.get     = [this] { return Value::ofInt(sio_.base()); };
        x.set     = [this](const Value& v, std::string&) {
            sio_.setBase((uint8_t)v.i());
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<MapEntry> Sio2Board::ioMap() const {
    std::vector<MapEntry> m;
    uint8_t b = sio_.base();
    for (const auto& ch : sio_.channels())
        m.push_back({(uint32_t)(b + ch.offset), (uint32_t)(b + ch.offset + 1), "read/write",
                     "6850 '" + ch.name + "' -- status/control, data"});
    return m;
}

bool Sio2Board::connect(const std::string& unit, const std::string& endpoint, std::string& err) {
    // The Sio2Port has no config dir; the board is the only thing that knows one, so a
    // machine-file in:/out: PATH is rebased here before the resolver opens it. (Unlike the
    // parallel cards, the 6850 echoes stream->describe() for its `connect` property, so what
    // SHOW/CONFIG SAVE report is the resolved path -- which reloads idempotently.)
    std::vector<std::string> paths;
    std::string              spec = rebaseEndpointPaths(endpoint, [&](const std::string& p) {
        paths.push_back(p);
        return resolvePath(p);
    });
    if (!sio_.connect(unit, spec, err)) {
        for (const std::string& p : paths) err += pathNote(p);
        return false;
    }
    return true;
}

} // namespace altair
