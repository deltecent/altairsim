#include "boards/mits-680io.h"

#include "core/statefile.h"
#include "host/endpoint.h"

#include <utility>

namespace altair {

Io680Board::Io680Board()
    // The section is handed the card's intChanged() so it can drive the 6800's IRQ pin --
    // it is not a Board and cannot reach it. `this` is valid here: the base subobject is
    // built. One channel, `tty`, at offset 0; the card puts the section's base at 0 so
    // that address F000 maps to section-port 0 and F001 to port 1.
    : sio_({{"tty", 0}}, [this] { intChanged(); }) {
    sio_.setBase(0);
    // The section rebases a declarative `[680io0.unit.tty] connect = "in:tape.tap"`
    // (applied through the chip's `connect` property) against the machine file's dir;
    // the card's connect() rebases the CONNECT command separately.
    sio_.setRebase([this](const std::string& p) { return resolvePath(p); });
}

// ---------------------------------------------------------------------------
// Decode. F000/F001 are the 6850; F002 is the read-only strap buffer.
// ---------------------------------------------------------------------------
bool Io680Board::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::MemRead && c.type != Cycle::MemWrite) return false;

    if (c.addr == kAciaBase || c.addr == (uint16_t)(kAciaBase + 1))
        return sio_.decodesPort((uint8_t)(c.addr - kAciaBase));

    // The strap buffer is a tri-state that only enables on a READ (Theory §4.3); a write
    // to F002 is not ours (it falls through to whatever else may answer, i.e. nothing).
    if (c.addr == kStraps && c.type == Cycle::MemRead) return true;

    return false;
}

uint8_t Io680Board::read(const BusCycle& c) {
    if (c.type == Cycle::MemRead) {
        if (c.addr == kAciaBase || c.addr == (uint16_t)(kAciaBase + 1))
            return sio_.read((uint8_t)(c.addr - kAciaBase));
        if (c.addr == kStraps) return straps_;
    }
    return 0xFF;
}

void Io680Board::write(const BusCycle& c) {
    if (c.type == Cycle::MemWrite &&
        (c.addr == kAciaBase || c.addr == (uint16_t)(kAciaBase + 1)))
        sio_.write((uint8_t)(c.addr - kAciaBase), c.data);
    // F002 is a strap buffer: read-only. A write there does nothing, exactly like the card.
}

// ---------------------------------------------------------------------------
// Lifecycle.
// ---------------------------------------------------------------------------
void Io680Board::reset(Reset r) { sio_.reset(r); }

void Io680Board::power() { reset(Reset::PowerOn); }

void Io680Board::configChanged() {
    sio_.refresh();  // a SIO strap (baud/interrupt/connect) moved a deadline
    // The decode is fixed (F000-F002); only `straps` can change and it does not move it.
}

bool Io680Board::connect(const std::string& unit, const std::string& endpoint, std::string& err) {
    // The Sio2Port has no config dir; the board is the only thing that knows one, so a
    // machine-file in:/out: PATH is rebased here before the resolver opens it.
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

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> Io680Board::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "straps";
        x.help  = "Config straps read at F002: bit7 No-Terminal (0=terminal), bit2 stop bits";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xFF;
        x.get   = [this] { return Value::ofInt(straps_); };
        x.set   = [this](const Value& v, std::string&) {
            straps_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<MapEntry> Io680Board::memMap() const {
    return {
        {(uint32_t)kAciaBase, (uint32_t)(kAciaBase + 1), "read/write",
         "6850 ACIA 'tty' -- F000 status/control, F001 Rx/Tx data"},
        {(uint32_t)kStraps, (uint32_t)kStraps, "read",
         "hardware config straps (No-Terminal / stop bits / Baudot)"},
    };
}

std::vector<std::string> Io680Board::drainLog() {
    std::vector<std::string> out;
    for (auto& s : sio_.drainLog()) out.push_back(id + ":" + s);
    for (auto& s : log_) out.push_back(std::move(s));
    log_.clear();
    return out;
}

// ---------------------------------------------------------------------------
// SNAPSHOT / RESTORE. The straps are config (re-applied from the machine file), but
// they are one byte and travel harmlessly; the 6850's live state travels via the
// section (DESIGN.md 13).
// ---------------------------------------------------------------------------
void Io680Board::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u8(straps_);
    sio_.serialize(w);
}

void Io680Board::deserialize(StateReader& r) {
    Board::deserialize(r);
    straps_ = r.u8();
    sio_.deserialize(r);
}

} // namespace altair
