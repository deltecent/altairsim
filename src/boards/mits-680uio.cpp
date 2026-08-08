#include "boards/mits-680uio.h"

#include "core/statefile.h"
#include "host/endpoint.h"

#include <cctype>
#include <utility>

namespace altair {
namespace {

Uio680Board::EndpointResolver g_resolver;

// The four PIA sections, in index order: PIA-C A/B then PIA-B A/B.
const char* const kLineNames[4] = {"p1a", "p1b", "p2a", "p2b"};

bool unitEq(const std::string& a, const char* b) {
    std::string lo;
    lo.reserve(a.size());
    for (char c : a) lo += (char)std::tolower((unsigned char)c);
    return lo == b;
}

} // namespace

void Uio680Board::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

Uio680Board::Uio680Board()
    // One serial channel `serial` at section-offset 0; the card translates the
    // window's base+6/base+7 to section ports 0/1. The section drives the card's
    // intChanged() -- it is not a Board and cannot reach the 6800's IRQ pin.
    : sio_({{"serial", 0}}, [this] { intChanged(); }) {
    sio_.setBase(0);
    sio_.setRebase([this](const std::string& p) { return resolvePath(p); });
    for (Line& ln : line_) ln.stream = std::make_unique<NullStream>();
}

// ---------------------------------------------------------------------------
// Decode. A relocatable serial+PIA window, plus fixed F003 and F010-F013.
// ---------------------------------------------------------------------------
bool Uio680Board::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::MemRead && c.type != Cycle::MemWrite) return false;
    uint16_t a = c.addr;

    // Serial 6850 at base+6 / base+7.
    if (a == (uint16_t)(base_ + 6) || a == (uint16_t)(base_ + 7))
        return sio_.decodesPort((uint8_t)(a - (base_ + 6)));

    // PIA-C parallel port 1 at base+8..base+B.
    if (a >= (uint16_t)(base_ + 0x08) && a <= (uint16_t)(base_ + 0x0B)) return true;

    // PIA-B parallel port 2 at base+C..base+F -- only if a second PIA is populated.
    if (pias_ >= 2 && a >= (uint16_t)(base_ + 0x0C) && a <= (uint16_t)(base_ + 0x0F))
        return true;

    // Fixed switch inputs -- a read-only tri-state (reference §2). A write to F003
    // is not ours.
    if (a == kSense && c.type == Cycle::MemRead) return true;

    // Fixed non-latched output, unless IC A1 is removed for a KCACR at F010/F011.
    if (nlout_ && a >= kNlOutLo && a <= kNlOutHi) return true;

    return false;
}

uint8_t Uio680Board::read(const BusCycle& c) {
    if (c.type != Cycle::MemRead) return 0xFF;
    uint16_t a = c.addr;

    if (a == (uint16_t)(base_ + 6) || a == (uint16_t)(base_ + 7))
        return sio_.read((uint8_t)(a - (base_ + 6)));

    if (a >= (uint16_t)(base_ + 0x08) && a <= (uint16_t)(base_ + 0x0B)) {
        int off = a - (base_ + 0x08);
        return piaC_.read(off >> 1, off & 1);
    }
    if (pias_ >= 2 && a >= (uint16_t)(base_ + 0x0C) && a <= (uint16_t)(base_ + 0x0F)) {
        int off = a - (base_ + 0x0C);
        return piaB_.read(off >> 1, off & 1);
    }

    if (a == kSense) return sense_;

    // A non-latched OUTPUT has no defined read data (the drivers tri-state); hand
    // back the last byte driven so a test or the monitor can observe it.
    if (nlout_ && a >= kNlOutLo && a <= kNlOutHi) return (a & 2) ? drive2_ : drive1_;

    return 0xFF;
}

void Uio680Board::write(const BusCycle& c) {
    if (c.type != Cycle::MemWrite) return;
    uint16_t a = c.addr;

    if (a == (uint16_t)(base_ + 6) || a == (uint16_t)(base_ + 7)) {
        sio_.write((uint8_t)(a - (base_ + 6)), c.data);
        return;
    }

    if (a >= (uint16_t)(base_ + 0x08) && a <= (uint16_t)(base_ + 0x0B)) {
        int off = a - (base_ + 0x08);
        int sec = off >> 1;
        piaC_.write(sec, off & 1, c.data);
        uint8_t out;  // a guest data write drives the lines immediately (like the 4pio)
        if (piaC_.takeOutput(sec, out)) line_[sec].stream->writeByte(out);
        return;
    }
    if (pias_ >= 2 && a >= (uint16_t)(base_ + 0x0C) && a <= (uint16_t)(base_ + 0x0F)) {
        int off = a - (base_ + 0x0C);
        int sec = off >> 1;
        piaB_.write(sec, off & 1, c.data);
        uint8_t out;
        if (piaB_.takeOutput(sec, out)) line_[2 + sec].stream->writeByte(out);
        return;
    }

    // Non-latched output: F011/F013 drive the lines; F010/F012 are the drives'
    // control/status and have no modeled effect. F003 is read-only.
    if (nlout_ && a >= kNlOutLo && a <= kNlOutHi) {
        if (a == (uint16_t)(kNlOutLo + 1)) drive1_ = c.data;        // F011
        else if (a == kNlOutHi) drive2_ = c.data;                  // F013
    }
}

// ---------------------------------------------------------------------------
// Interrupts. The ACIA and both PIAs share the 6800's single IRQ (reference §6).
// ---------------------------------------------------------------------------
bool Uio680Board::assertsInt() const {
    if (sio_.assertsInt()) return true;
    if (piaC_.irq(0) || piaC_.irq(1)) return true;
    if (pias_ >= 2 && (piaB_.irq(0) || piaB_.irq(1))) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Lifecycle.
// ---------------------------------------------------------------------------
void Uio680Board::reset(Reset r) {
    sio_.reset(r);
    piaC_.reset();
    piaB_.reset();
    for (Line& ln : line_) ln.stream->flush();
}

// The door to the outside (DESIGN.md 7.1): drain the serial section, then for
// each populated PIA section pull one received byte into its input latch. Guest
// output already went out at write() time.
void Uio680Board::pump() {
    sio_.pump();
    for (int i = 0; i < pias_ * 2; ++i) {
        Line& ln = line_[i];
        ln.stream->pump();
        ln.stream->flush();
        Pia6820& pia = piaForLine(i);
        int      sec = sectionForLine(i);
        if (!pia.inputFull(sec) && ln.stream->readable())
            pia.deliver(sec, ln.stream->readByte());
    }
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> Uio680Board::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "base";
        x.help  = "S9 window base (F000 + position*0x10); serial at base+6, PIAs base+8..+F";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0xF000;
        x.max   = 0xF0F0;
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string& err) {
            long long b = v.i();
            if ((b & 0xFF00) != 0xF000 || (b & 0x000F) != 0) {
                err = "the 680uio window is F0X0 -- a multiple of 16 in the F000-F0F0 I/O page";
                return false;
            }
            base_ = (uint16_t)b;
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "pias";
        x.help = "6820 PIAs populated: 1 (PIA-C only) or 2 (PIA-C + PIA-B)";
        x.kind = Kind::Int;
        x.min  = 1;
        x.max  = 2;
        x.get  = [this] { return Value::ofInt(pias_); };
        x.set  = [this](const Value& v, std::string&) {
            pias_ = (int)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "sense";
        x.help  = "Switch inputs read at F003 (fixed, read-only tri-state)";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xFF;
        x.get   = [this] { return Value::ofInt(sense_); };
        x.set   = [this](const Value& v, std::string&) {
            sense_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "nlout";
        x.help = "Decode the F010-F013 non-latched output (off = IC A1 removed, KCACR owns F010/F011)";
        x.kind = Kind::Bool;
        x.get  = [this] { return Value::ofBool(nlout_); };
        x.set  = [this](const Value& v, std::string&) {
            nlout_ = v.b();
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<Property> Uio680Board::unitProperties(const std::string& unit) {
    if (lineIndex(unit) >= 0) {
        std::vector<Property> p;
        Property x;
        x.name = "connect";
        x.help = "The endpoint on the other end of this PIA section (CONNECT sets this)";
        x.kind = Kind::Str;
        std::string u = unit;
        x.get  = [this, u] {
            int i = lineIndex(u);
            return Value::ofStr(i < 0 ? "null" : line_[i].spec);
        };
        x.set  = [this, u](const Value& v, std::string& err) { return applyEndpoint(u, v.s(), err); };
        p.push_back(std::move(x));
        return p;
    }
    return sio_.unitProperties(unit);
}

std::vector<UnitDef> Uio680Board::units() const {
    std::vector<UnitDef> u = sio_.units();  // the serial channel `serial`
    for (int i = 0; i < pias_ * 2; ++i)
        u.push_back({kLineNames[i], UnitKind::Serial, line_[i].spec});
    return u;
}

std::vector<MapEntry> Uio680Board::memMap() const {
    std::vector<MapEntry> m;
    m.push_back({(uint32_t)(base_ + 6), (uint32_t)(base_ + 7), "read/write",
                 "6850 ACIA 'serial' -- control/status + Rx/Tx data"});
    m.push_back({(uint32_t)(base_ + 0x08), (uint32_t)(base_ + 0x0B), "read/write",
                 "6820 PIA-C 'p1a/p1b' -- A/B control + data/DDR"});
    if (pias_ >= 2)
        m.push_back({(uint32_t)(base_ + 0x0C), (uint32_t)(base_ + 0x0F), "read/write",
                     "6820 PIA-B 'p2a/p2b' -- A/B control + data/DDR"});
    m.push_back({(uint32_t)kSense, (uint32_t)kSense, "read",
                 "switch inputs (sense) -- fixed, read-only"});
    if (nlout_)
        m.push_back({(uint32_t)kNlOutLo, (uint32_t)kNlOutHi, "write",
                     "non-latched output -- Drive 1 (F010/F011), Drive 2 (F012/F013)"});
    return m;
}

// ---------------------------------------------------------------------------
// Units and connectors.
// ---------------------------------------------------------------------------
int Uio680Board::lineIndex(const std::string& unit) const {
    for (int i = 0; i < pias_ * 2; ++i)
        if (unitEq(unit, kLineNames[i])) return i;
    return -1;
}

bool Uio680Board::applyEndpoint(const std::string& unit, const std::string& endpoint,
                                std::string& err) {
    int i = lineIndex(unit);
    if (i < 0) {
        err = "680uio has no parallel section '" + unit +
              "' -- try p1a/p1b (p2a/p2b when pias=2)";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    // An in:/out: PATH is rebased against the config dir; the ORIGINAL spec is
    // remembered so a relative path does not double-rebase on CONFIG SAVE +
    // reload, and each PATH is collected so a failed open can name the rule.
    std::vector<std::string> paths;
    std::string              spec = rebaseEndpointPaths(endpoint, [&](const std::string& p) {
        paths.push_back(p);
        return resolvePath(p);
    });
    auto s = g_resolver(spec, err);
    if (!s) {
        for (const std::string& p : paths) err += pathNote(p);
        return false;
    }
    line_[i].stream = std::move(s);
    line_[i].spec   = endpoint;
    return true;
}

bool Uio680Board::connect(const std::string& unit, const std::string& endpoint, std::string& err) {
    if (lineIndex(unit) >= 0) return applyEndpoint(unit, endpoint, err);

    // Otherwise it is the serial channel: rebase machine-file paths here (the
    // section has no config dir), then its shared resolver opens the endpoint --
    // mirrors mits-680io.cpp.
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

bool Uio680Board::disconnect(const std::string& unit, std::string& err) {
    int i = lineIndex(unit);
    if (i >= 0) {
        line_[i].stream = std::make_unique<NullStream>();
        line_[i].spec   = "null";
        return true;
    }
    return sio_.disconnect(unit, err);
}

ByteStream* Uio680Board::unitStream(const std::string& unit) {
    int i = lineIndex(unit);
    if (i >= 0) return line_[i].stream.get();
    return sio_.unitStream(unit);
}

std::vector<std::string> Uio680Board::drainLog() {
    std::vector<std::string> out;
    for (auto& s : sio_.drainLog()) out.push_back(id + ":" + s);
    for (auto& s : log_) out.push_back(std::move(s));
    log_.clear();
    return out;
}

// ---------------------------------------------------------------------------
// SNAPSHOT / RESTORE. The straps (base/pias/sense/nlout) are config, re-applied
// from the machine file; the live chip state travels here (DESIGN.md 13). sense_
// is one byte and rides along harmlessly, like the 680io straps.
// ---------------------------------------------------------------------------
void Uio680Board::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u8(sense_);
    w.u8(drive1_);
    w.u8(drive2_);
    sio_.serialize(w);
    piaC_.serialize(w);
    piaB_.serialize(w);
}

void Uio680Board::deserialize(StateReader& r) {
    Board::deserialize(r);
    sense_  = r.u8();
    drive1_ = r.u8();
    drive2_ = r.u8();
    sio_.deserialize(r);
    piaC_.deserialize(r);
    piaB_.deserialize(r);
}

} // namespace altair
