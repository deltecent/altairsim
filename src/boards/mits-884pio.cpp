#include "boards/mits-884pio.h"

#include "core/statefile.h"

#include <cctype>
#include <cstdio>

namespace altair {
namespace {

// Control/status register bits we act on (reference §3).
constexpr uint8_t kDdrSelect = 0x04;  // bit 2: 0 = DDR at the data address, 1 = data reg
constexpr uint8_t kIrq1Flag  = 0x80;  // bit 7: C1/IRQ1 flag -- HIGH = data available
// bit 6 (IRQ2/C2 flag) is not modeled; the stored control bits are 5..0 (mask 0x3F).
constexpr uint8_t kCtrlStored = 0x3F;

Pio4Board::EndpointResolver g_resolver;

const char* kPortLetters = "jklm";

} // namespace

void Pio4Board::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

Pio4Board::Pio4Board() {
    for (Section& s : sec_) s.stream = std::make_unique<NullStream>();
}

// ---------------------------------------------------------------------------
// Addressing. A board answers a 16-address window (4 per populated port). Within
// it: port = offset/4, and offset%4 selects A/B and control/data.
// ---------------------------------------------------------------------------
bool Pio4Board::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    uint8_t p = c.port();
    return p >= base_ && p < (uint8_t)(base_ + ports_ * 4);
}

uint8_t Pio4Board::read(const BusCycle& c) {
    int off  = (uint8_t)(c.port() - base_);
    int idx  = (off / 4) * 2 + ((off % 4) >> 1);  // section index
    int reg  = off & 1;                           // 0 = control, 1 = data/DDR
    Section& s = sec_[idx];

    if (reg == 0) {
        // Control/status: the stored bits, plus the live IRQ1 flag (data available).
        uint8_t v = s.ctrl & kCtrlStored;
        if (s.inFull) v |= kIrq1Flag;
        return v;
    }
    // Data address.
    if (s.ctrl & kDdrSelect) {
        // Reading the DATA register hands over the input latch and resets the flag
        // (6820: bit 7 and IRQ clear on a data read).
        s.inFull = false;
        return s.inLatch;
    }
    return s.ddr;  // DDR is what the data address reaches when control bit 2 is 0
}

void Pio4Board::write(const BusCycle& c) {
    int off  = (uint8_t)(c.port() - base_);
    int idx  = (off / 4) * 2 + ((off % 4) >> 1);
    int reg  = off & 1;
    Section& s = sec_[idx];

    if (reg == 0) {
        // Control: bits 5..0 are ours; 7 and 6 are read-only status flags.
        s.ctrl = c.data & kCtrlStored;
        return;
    }
    // Data address.
    if (s.ctrl & kDdrSelect) {
        s.outReg = c.data;
        s.stream->writeByte(c.data);  // out on the data lines, verbatim
    } else {
        s.ddr = c.data;  // program the data direction
    }
}

void Pio4Board::reset(Reset) {
    // POC/RESET clears every register: all lines become inputs, all flags clear.
    // The lines STAY CONNECTED.
    for (Section& s : sec_) {
        s.ctrl    = 0;
        s.ddr     = 0;
        s.outReg  = 0;
        s.inLatch = 0;
        s.inFull  = false;
        s.stream->flush();
    }
}

void Pio4Board::serialize(StateWriter& w) const {
    Board::serialize(w);
    for (const Section& s : sec_) {  // fixed kSections, so the layout is deterministic
        w.u8(s.ctrl);
        w.u8(s.ddr);
        w.u8(s.outReg);
        w.u8(s.inLatch);
        w.boolean(s.inFull);
    }
}

void Pio4Board::deserialize(StateReader& r) {
    Board::deserialize(r);
    for (Section& s : sec_) {
        s.ctrl    = r.u8();
        s.ddr     = r.u8();
        s.outReg  = r.u8();
        s.inLatch = r.u8();
        s.inFull  = r.boolean();
    }
}

// The one door to the outside world (DESIGN.md 7.1): drain each section's output
// line and pull one byte into its input latch. The latch is one byte deep and sets
// status bit 7, so a polling driver (poll bit 7, read data) works.
void Pio4Board::pump() {
    for (int i = 0; i < ports_ * 2; ++i) {
        Section& s = sec_[i];
        s.stream->pump();
        s.stream->flush();
        if (!s.inFull && s.stream->readable()) {
            s.inLatch = s.stream->readByte();
            s.inFull  = true;
        }
    }
}

// ---------------------------------------------------------------------------
// Properties, units, and the connectors.
// ---------------------------------------------------------------------------
std::vector<Property> Pio4Board::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Base address -- must be on a 16-address boundary. 16 ports from here";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xF0;  // base + 4 ports * 4 addresses stays within 00..FF
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string& err) {
            if (v.i() & 0x0F) {
                err = "the 88-4PIO answers 16 addresses -- the base must be a multiple of 16";
                return false;
            }
            base_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "ports";
        x.help = "How many 6820 PIAs are populated (1..4 -- J, K, L, M)";
        x.kind = Kind::Int;
        x.min  = 1;
        x.max  = kMaxPorts;
        x.get  = [this] { return Value::ofInt(ports_); };
        x.set  = [this](const Value& v, std::string&) {
            ports_ = (int)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<Property> Pio4Board::unitProperties(const std::string& unit) {
    if (sectionIndex(unit) < 0) return {};
    std::vector<Property> p;
    {
        Property x;
        x.name = "connect";
        x.help = "The endpoint on the other end of this section (CONNECT sets this)";
        x.kind = Kind::Str;
        std::string u = unit;
        x.get  = [this, u] {
            int i = sectionIndex(u);
            return Value::ofStr(i < 0 ? "null" : sec_[i].spec);
        };
        x.set  = [this, u](const Value& v, std::string& err) { return applyEndpoint(u, v.s(), err); };
        p.push_back(std::move(x));
    }
    return p;
}

std::string Pio4Board::unitName(int idx) {
    std::string n;
    n += kPortLetters[idx / 2];
    n += (idx & 1) ? 'b' : 'a';
    return n;
}

// Two units per populated port, named for the 6820 and its section: ja, jb, ka, ...
std::vector<UnitDef> Pio4Board::units() const {
    std::vector<UnitDef> u;
    for (int i = 0; i < ports_ * 2; ++i)
        u.push_back({unitName(i), UnitKind::Serial, sec_[i].spec});
    return u;
}

std::vector<MapEntry> Pio4Board::ioMap() const {
    std::vector<MapEntry> m;
    for (int port = 0; port < ports_; ++port) {
        uint32_t b = (uint32_t)base_ + port * 4;
        char note[64];
        std::snprintf(note, sizeof note, "4PIO %c -- A control/data, B control/data",
                      kPortLetters[port]);
        m.push_back({b, b + 3, "read/write", note});
    }
    return m;
}

int Pio4Board::sectionIndex(const std::string& unit) const {
    if (unit.size() != 2) return -1;
    char c0 = (char)std::tolower((unsigned char)unit[0]);
    char c1 = (char)std::tolower((unsigned char)unit[1]);
    int port = -1;
    for (int i = 0; i < kMaxPorts; ++i)
        if (kPortLetters[i] == c0) port = i;
    if (port < 0 || port >= ports_) return -1;
    int section = (c1 == 'a') ? 0 : (c1 == 'b') ? 1 : -1;
    if (section < 0) return -1;
    return port * 2 + section;
}

ByteStream* Pio4Board::unitStream(const std::string& unit) {
    int i = sectionIndex(unit);
    return i < 0 ? nullptr : sec_[i].stream.get();
}

bool Pio4Board::applyEndpoint(const std::string& unit, const std::string& endpoint,
                              std::string& err) {
    int i = sectionIndex(unit);
    if (i < 0) {
        err = "4pio has no section '" + unit + "' -- try ja/jb (and ka.. for more ports)";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }

    std::string spec = endpoint;
    if (endpoint.rfind("file:", 0) == 0) {
        std::string path = endpoint.substr(5);
        if (!path.empty()) spec = "file:" + resolvePath(path);
    }

    auto s = g_resolver(spec, err);
    if (!s) {
        if (endpoint.rfind("file:", 0) == 0) err += pathNote(endpoint.substr(5));
        return false;
    }
    sec_[i].stream = std::move(s);
    sec_[i].spec   = endpoint;
    return true;
}

bool Pio4Board::connect(const std::string& unit, const std::string& ep, std::string& err) {
    return applyEndpoint(unit, ep, err);
}

bool Pio4Board::disconnect(const std::string& unit, std::string& err) {
    int i = sectionIndex(unit);
    if (i < 0) {
        err = "4pio has no section '" + unit + "' -- try ja/jb (and ka.. for more ports)";
        return false;
    }
    sec_[i].stream = std::make_unique<NullStream>();
    sec_[i].spec   = "null";
    sec_[i].inFull = false;
    return true;
}

} // namespace altair
