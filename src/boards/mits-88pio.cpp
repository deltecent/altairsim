#include "boards/mits-88pio.h"

#include "core/statefile.h"

namespace altair {
namespace {

// The status word (reference §3). Two bits, ACTIVE HIGH: a set bit means the named
// condition IS so. The rest of the byte is not driven by anything on this card.
constexpr uint8_t kOutReady = 0x01;  // DI0: HIGH = the output device will take a byte
constexpr uint8_t kInFull   = 0x02;  // DI1: HIGH = the input device has sent one

// The control word (reference §3): the two interrupt-enable bits. Only stored.
constexpr uint8_t kIntOut = 0x01;  // DO0: enable the output-device interrupt
constexpr uint8_t kIntIn  = 0x02;  // DO1: enable the input-device interrupt

PioBoard::EndpointResolver g_resolver;

} // namespace

void PioBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

PioBoard::PioBoard()
    : out_(std::make_unique<NullStream>()), in_(std::make_unique<NullStream>()) {
    // No null pointer in either stream path, ever: an unconnected device is a DEAD
    // line (a printer switched off, a keyboard unplugged), not a dangling one.
}

// ---------------------------------------------------------------------------
// Status: DI0 from the output line's writable(), DI1 from the input latch.
// ---------------------------------------------------------------------------
uint8_t PioBoard::statusByte() const {
    uint8_t s = 0;
    if (out_->writable()) s |= kOutReady;  // the output device can take the next byte
    if (inFull_) s |= kInFull;             // the input device has one waiting
    return s;
}

// ---------------------------------------------------------------------------
// The bus interface. Two ports: Control/Status at BASE (even), Data at BASE+1 (odd).
// ---------------------------------------------------------------------------
// A0 PICKS THE CHANNEL. Unlike the C700 -- an output-only card where an IN at the
// odd data port drives nothing and must be left to float (issue #26) -- the 88-PIO
// has a real INPUT LATCH (8212 IC H) that puts its byte on the bus on `IN <odd>`.
// So BOTH ports answer BOTH directions here, and claiming the odd read is correct:
// the card genuinely drives it. (When nothing is connected the latch simply holds
// 0 -- a real input port with no device reads whatever is on its floating lines.)
bool PioBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    uint8_t p = c.port();
    return p == base_ || p == (uint8_t)(base_ + 1);
}

uint8_t PioBoard::read(const BusCycle& c) {
    if ((c.port() - base_) & 1) {
        // DATA -- hand over the latched input byte and mark the latch empty.
        inFull_ = false;
        return inLatch_;
    }
    return statusByte();  // CONTROL/STATUS channel
}

void PioBoard::write(const BusCycle& c) {
    if ((c.port() - base_) & 1) {
        // DATA -- the byte goes out on the output line, verbatim.
        out_->writeByte(c.data);
        return;
    }
    // CONTROL -- the interrupt-enable bits (DO0/DO1). Stored so the status/snapshot
    // carry them; the request itself is not raised in the polled card.
    ctrl_ = c.data & (kIntOut | kIntIn);
}

void PioBoard::reset(Reset) {
    // POC/RESET clears the interrupt structure and empties the input latch. The
    // lines STAY CONNECTED -- a warm reset does not unplug a device.
    ctrl_    = 0;
    inFull_  = false;
    inLatch_ = 0;
    out_->flush();
}

void PioBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u8(inLatch_);
    w.boolean(inFull_);
    w.u8(ctrl_);
}

void PioBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    inLatch_ = r.u8();
    inFull_  = r.boolean();
    ctrl_    = r.u8();
}

// The one door to the outside world (DESIGN.md 7.1): drain the output line, and
// pull one byte off the input line into the latch if it is empty. Both halves are
// no-ops on a NullStream. The input latch models the 8212 H -- one byte deep, set
// by the device's strobe -- so DI1 stays true until the guest reads the data port.
void PioBoard::pump() {
    out_->pump();
    out_->flush();
    if (!inFull_ && in_->readable()) {
        inLatch_ = in_->readByte();
        inFull_  = true;
    }
    in_->pump();
}

// ---------------------------------------------------------------------------
// Properties, units, and the connectors.
// ---------------------------------------------------------------------------
std::vector<Property> PioBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Base address -- MUST BE EVEN. Control/status at BASE, data at BASE+1";
        x.kind  = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xFE;
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string& err) {
            // A0 is not decoded -- it picks the channel -- so an odd base is not a
            // card you could build.
            if (v.i() & 1) {
                err = "the 88-PIO decodes an even/odd PAIR -- the base must be even";
                return false;
            }
            base_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<Property> PioBoard::unitProperties(const std::string& unit) {
    if (unit != "out" && unit != "in") return {};
    std::vector<Property> p;
    {
        // Each device is a line you CONNECT. `connect` is the endpoint on the far
        // end; reading it round-trips through CONFIG SAVE.
        Property x;
        x.name = "connect";
        x.help = "The endpoint on the other end of this line (CONNECT sets this)";
        x.kind = Kind::Str;
        std::string u = unit;  // by value -- the lambda outlives this call
        x.get  = [this, u] { return Value::ofStr(specOf(u)); };
        x.set  = [this, u](const Value& v, std::string& err) { return applyEndpoint(u, v.s(), err); };
        p.push_back(std::move(x));
    }
    return p;
}

// TWO serial units, named for their direction: the output device and the input
// device. CONNECT names them.
std::vector<UnitDef> PioBoard::units() const {
    return {
        {"out", UnitKind::Serial, outSpec_},
        {"in",  UnitKind::Serial, inSpec_},
    };
}

std::vector<MapEntry> PioBoard::ioMap() const {
    return {
        {(uint32_t)base_, (uint32_t)base_, "read/write",
         "PIO -- status: DI0 out-ready, DI1 in-full (read) / control: interrupt enable (write)"},
        {(uint32_t)base_ + 1, (uint32_t)base_ + 1, "read/write",
         "PIO -- data: input latch (read) / to the output device (write)"},
    };
}

const std::string& PioBoard::specOf(const std::string& unit) const {
    return unit == "in" ? inSpec_ : outSpec_;
}

ByteStream* PioBoard::unitStream(const std::string& unit) {
    if (unit == "out") return out_.get();
    if (unit == "in") return in_.get();
    return nullptr;
}

bool PioBoard::applyEndpoint(const std::string& unit, const std::string& endpoint,
                             std::string& err) {
    if (unit != "out" && unit != "in") {
        err = "pio has no unit '" + unit + "' -- it has 'out' and 'in'";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }

    // A file: PATH is rebased relative to the config dir (the board is the only
    // thing that knows it), and the ORIGINAL spec is remembered so a relative path
    // does not double-rebase on CONFIG SAVE + reload. Same rule the C700 follows.
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

    if (unit == "in") {
        in_     = std::move(s);
        inSpec_ = endpoint;  // as written -- what SHOW and CONFIG SAVE echo
    } else {
        out_     = std::move(s);
        outSpec_ = endpoint;
    }
    return true;
}

bool PioBoard::connect(const std::string& unit, const std::string& ep, std::string& err) {
    return applyEndpoint(unit, ep, err);
}

bool PioBoard::disconnect(const std::string& unit, std::string& err) {
    if (unit == "in") {
        in_     = std::make_unique<NullStream>();
        inSpec_ = "null";
        inFull_ = false;  // the input device is gone -- nothing waiting
        return true;
    }
    if (unit == "out") {
        out_     = std::make_unique<NullStream>();
        outSpec_ = "null";
        return true;
    }
    err = "pio has no unit '" + unit + "' -- it has 'out' and 'in'";
    return false;
}

} // namespace altair
