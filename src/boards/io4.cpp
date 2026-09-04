#include "boards/io4.h"

#include "core/statefile.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <utility>

namespace altair {

namespace {

Io4Board::EndpointResolver g_resolver;

// A card on a backplane always has a clock, but Bus::attach() is public, so a board CAN be
// wired up without a machine around it. A UART with no clock cannot time a character; it reads
// as a dead card rather than dereferencing a null pointer. (Same idiom as SioBoard.)
Clock& deadCard() {
    static Clock stopped;
    return stopped;
}

} // namespace

void Io4Board::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

Io4Board::Io4Board() {
    // -> NullStream on both channels. There is never a null pointer in the stream path: a
    // channel with nothing plugged into it has a DEAD line, not a dangling one.
    a_.disconnect();
    b_.disconnect();
}

Io4Board::~Io4Board() = default;

// ---------------------------------------------------------------------------
// Addressing -- switch S3, a 4-port block. A at base+0/+1, B at base+2/+3.
// ---------------------------------------------------------------------------

bool Io4Board::decodePort(uint8_t port, Uart1602*& u, bool& isData) const {
    uint8_t off = (uint8_t)(port - base_);
    if (off >= 4) return false;
    // const_cast: the two members are non-const; decodePort is used from const decodes()
    // (where the out-param is discarded) and from non-const read/write.
    u      = const_cast<Uart1602*>(off < 2 ? &a_ : &b_);
    isData = (off & 1) != 0;  // status/control at the even port, data at the odd
    return true;
}

bool Io4Board::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    Uart1602* u = nullptr;
    bool      isData = false;
    return decodePort(c.port(), u, isData);
}

// PHASE 1 status map: MITS SIO Rev 0 (sior0), active low. DAV -> bit 0, TBMT -> bit 7, both
// inverted (asserted reads 0). This is exactly what the SSM 8080 System Monitor spins on.
uint8_t Io4Board::statusByte(const Uart1602& u) const {
    const Clock& clk = clock_ ? *clock_ : deadCard();
    uint8_t      s   = 0;
    if (!u.dataAvailable())    s |= 0x01;  // bit 0: 1 = nothing to receive (inverted DAV)
    if (!u.txBufferEmpty(clk)) s |= 0x80;  // bit 7: 1 = transmitter busy (inverted TBMT)
    return s;
}

uint8_t Io4Board::read(const BusCycle& c) {
    Uart1602* u = nullptr;
    bool      isData = false;
    if (!decodePort(c.port(), u, isData)) return 0xFF;  // decodes() gates this; be defensive

    // The receiver runs on the UART's own clock, not on ours -- advance it before reading.
    u->poll(clock_ ? *clock_ : deadCard());

    // The DATA port's read strobe is wired to /RDAR: reading it clears Data Available. The
    // other port is /SWE -- the synthesized status byte.
    return isData ? u->readData() : statusByte(*u);
}

void Io4Board::write(const BusCycle& c) {
    Uart1602* u = nullptr;
    bool      isData = false;
    if (!decodePort(c.port(), u, isData)) return;

    if (isData) {
        // The chip's /TDS strobe: the character goes out, TBMT falls until it has left.
        u->writeData(c.data, clock_ ? *clock_ : deadCard());
        return;
    }
    // OUT to the status/control port: ACCEPTED AND DISCARDED. The 1602 UART has no control
    // register -- word format is soldered pins (S1/S2), not a byte the guest can write.
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// MR (pin 21) on each UART: the data sheet's "sets TSO/TEOC/TBMT high, clears RDA/RPE/RFE/ROR".
// A warm reset does NOT unplug the terminal -- Uart1602::masterReset() keeps the endpoint.
void Io4Board::reset(Reset) {
    const Clock& clk = clock_ ? *clock_ : deadCard();
    a_.masterReset(clk);
    b_.masterReset(clk);
}

void Io4Board::power() { reset(Reset::PowerOn); }

// THE ONE DOOR THE OUTSIDE WORLD COMES THROUGH (DESIGN.md 7.1): drain the host into each line,
// then advance each receiver so a byte that just arrived is ready for the next status poll.
void Io4Board::pump() {
    a_.pump();
    b_.pump();
    if (clock_) {
        a_.poll(*clock_);
        b_.poll(*clock_);
    }
}

// A strap moved: a port change relocates the block (decodeChanged), and a word-format strap
// (`baud`/`data_bits`/`stop_bits`/`parity`) restraps a real serial port on the far end.
void Io4Board::configChanged() {
    decodeChanged();
    programChannel(a_);
    programChannel(b_);
}

void Io4Board::programChannel(Uart1602& u) {
    u.programLine();
    for (auto& s : u.drainLog()) log_.push_back(id + ":" + u.name() + " " + std::move(s));
}

void Io4Board::serialize(StateWriter& w) const {
    Board::serialize(w);  // enabled_
    a_.serialize(w);
    b_.serialize(w);
}

void Io4Board::deserialize(StateReader& r) {
    Board::deserialize(r);
    a_.deserialize(r);
    b_.deserialize(r);
}

std::vector<std::string> Io4Board::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    for (Uart1602* u : {&a_, &b_})
        for (auto& s : u->drainLog()) out.push_back(id + ":" + u->name() + " " + std::move(s));
    return out;
}

// ---------------------------------------------------------------------------
// Reflection
// ---------------------------------------------------------------------------

Uart1602*       Io4Board::channel(const std::string& u) {
    std::string n = lowerAscii(u);
    if (n == "a") return &a_;
    if (n == "b") return &b_;
    return nullptr;
}
const Uart1602* Io4Board::channel(const std::string& u) const {
    return const_cast<Io4Board*>(this)->channel(u);
}

std::vector<Property> Io4Board::properties() {
    std::vector<Property> p;
    // Switch S3: the 4-port block base. One switch for the whole serial section, so this is a
    // BOARD property (both channels move with it), not a per-channel one.
    {
        Property x;
        x.name  = "port";
        x.help  = "Serial base address (switch S3) -- a 4-PORT BLOCK, so a multiple of 4. "
                  "Serial A at BASE+0/+1, Serial B at BASE+2/+3";
        x.kind  = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xFC;
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string& err) {
            if (v.i() & 3) {
                err = "the IO-4 serial section decodes a 4-port block -- the base must be a "
                      "multiple of 4";
                return false;
            }
            base_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

// The per-channel word-format straps and the connector. On the real card these are switch S2
// (Serial A) / S1 (Serial B) plus the baud header W3; here they are the UART's own format pins.
std::vector<Property> Io4Board::channelProperties(Uart1602& u) {
    std::vector<Property> p;
    Uart1602* up = &u;  // stable: the UART members never move
    {
        Property x;
        x.name  = "baud";
        x.help  = "Line rate (header W3). RX and TX share one rate here -- a real host serial "
                  "port cannot be split. Canonical IO-4 rates: 55-9600";
        x.kind  = Kind::Int;
        x.radix = 10;  // never on the wire: decimal (DESIGN.md 10.0.1)
        x.min   = 50;
        x.max   = 25000;
        x.unit  = "baud";
        x.get   = [up] { return Value::ofInt(up->baud); };
        x.set   = [up](const Value& v, std::string&) {
            up->baud = v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "data_bits";
        x.help  = "Data bits per character (S1/S2 NDB1+NDB2)";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.min   = 5;
        x.max   = 8;
        x.get   = [up] { return Value::ofInt(up->dataBits); };
        x.set   = [up](const Value& v, std::string&) {
            up->dataBits = (int)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "stop_bits";
        x.help  = "Stop bits (S1/S2 NSB): 1 or 2";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.min   = 1;
        x.max   = 2;
        x.get   = [up] { return Value::ofInt(up->stopBits); };
        x.set   = [up](const Value& v, std::string&) {
            up->stopBits = (int)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "parity";
        x.help    = "Parity (S1/S2 NPB/POE): none | odd | even";
        x.kind    = Kind::Enum;
        x.choices = {"none", "odd", "even"};
        x.get     = [up] {
            switch (up->parity) {
            case LineParity::Odd:  return Value::ofStr("odd");
            case LineParity::Even: return Value::ofStr("even");
            default:               return Value::ofStr("none");
            }
        };
        x.set = [up](const Value& v, std::string&) {
            const std::string& s = v.s();
            up->parity = (s == "odd") ? LineParity::Odd : (s == "even") ? LineParity::Even
                                                                        : LineParity::None;
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "connect";
        x.help = "The endpoint on this channel's line (CONNECT sets this): a file, socket, "
                 "serial port, in:/out: file, null, loopback";
        x.kind = Kind::Str;
        x.get  = [up] { return Value::ofStr(up->endpoint()); };
        x.set  = [this, up](const Value& v, std::string& err) {
            return connect(up->name(), v.s(), err);
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<Property> Io4Board::unitProperties(const std::string& unit) {
    Uart1602* u = channel(unit);
    if (!u) return {};
    return channelProperties(*u);
}

std::vector<UnitDef> Io4Board::units() const {
    return {
        {"a", UnitKind::Serial, a_.endpoint()},
        {"b", UnitKind::Serial, b_.endpoint()},
    };
}

std::vector<MapEntry> Io4Board::ioMap() const {
    return {
        {(uint32_t)base_,       (uint32_t)base_,       "read/write", "Serial A -- status (R) / control, discarded (W)"},
        {(uint32_t)(base_ + 1), (uint32_t)(base_ + 1), "read/write", "Serial A -- receive (R) / transmit (W)"},
        {(uint32_t)(base_ + 2), (uint32_t)(base_ + 2), "read/write", "Serial B -- status (R) / control, discarded (W)"},
        {(uint32_t)(base_ + 3), (uint32_t)(base_ + 3), "read/write", "Serial B -- receive (R) / transmit (W)"},
    };
}

bool Io4Board::connect(const std::string& unit, const std::string& ep, std::string& err) {
    Uart1602* u = channel(unit);
    if (!u) {
        err = "io4 has no unit '" + unit + "' -- its serial channels are 'a' and 'b'";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    // A machine-file in:/out: PATH is relative to the machine file; rebase the copy the
    // resolver opens. describe() still echoes the operator's original spec.
    std::vector<std::string> paths;
    std::string              spec = rebaseEndpointPaths(ep, [&](const std::string& pth) {
        paths.push_back(pth);
        return resolvePath(pth);
    });
    auto s = g_resolver(spec, err);
    if (!s) {
        for (const std::string& pth : paths) err += pathNote(pth);
        return false;
    }
    u->connect(std::move(s));  // the chip owns the line and brings it up to the straps
    return true;
}

bool Io4Board::connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                             std::string& err) {
    Uart1602* u = channel(unit);
    if (!u) {
        err = "io4 has no unit '" + unit + "' -- its serial channels are 'a' and 'b'";
        return false;
    }
    u->connect(std::move(s));
    return true;
}

bool Io4Board::disconnect(const std::string& unit, std::string& err) {
    Uart1602* u = channel(unit);
    if (!u) {
        err = "io4 has no unit '" + unit + "' -- its serial channels are 'a' and 'b'";
        return false;
    }
    u->disconnect();
    return true;
}

ByteStream* Io4Board::unitStream(const std::string& unit) {
    Uart1602* u = channel(unit);
    return u ? &u->stream() : nullptr;
}

} // namespace altair
