#include "boards/vector-bitstreamer.h"

#include "core/bus.h"
#include "core/statefile.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <utility>

namespace altair {

namespace {

EndpointResolver g_resolver;

// The same idiom as Io4Board: a board with no machine around it has a chip with no time.
Clock& deadCard() {
    static Clock stopped;
    return stopped;
}

std::string noUnit(const std::string& unit) {
    return "bitstreamer has no unit '" + unit + "' -- the serial channels are 'a', 'b' and 'c'";
}

} // namespace

void BitstreamerBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

// A channel with nothing plugged in has a dead line, never a null pointer.
BitstreamerBoard::BitstreamerBoard() {
    for (Channel* ch : {&a_, &b_, &c_}) ch->uart.disconnect();
}

BitstreamerBoard::~BitstreamerBoard() {
    // BOARDS REMOVE can pull this from a running machine, with a deadline still queued.
    if (clock_) {
        clock_->cancel(wake_);
        clockTick_.stop(clock_);
    }
}

// ---------------------------------------------------------------------------
// The ports.
// ---------------------------------------------------------------------------
bool BitstreamerBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    return (uint8_t)(c.port() - port_) < 8;
}

// An open input reads 1. U4 pin 5 is the Q of the latch, and the mask does not reach it.
uint8_t BitstreamerBoard::j4() const {
    if (j4d0_ == J4D0::ClockLatch && !latch_) return 0xFE;
    return 0xFF;
}

uint8_t BitstreamerBoard::read(const BusCycle& c) {
    const Clock&  clk = clock_ ? *clock_ : deadCard();
    const uint8_t off = (uint8_t)(c.port() - port_);
    uint8_t       v   = 0xFF;
    if (off < 6) {
        Channel& ch = off < 2 ? a_ : off < 4 ? b_ : c_;
        v           = (off & 1) ? ch.uart.readStatus(clk) : ch.uart.readData(clk);
    } else if (off == 6) {
        v = j4();
    } else {
        latch_ = false;  // U17-11 pulls CLR low on any IN from x9. J5 is what the guest reads.
    }
    refresh();
    return v;
}

void BitstreamerBoard::write(const BusCycle& c) {
    const Clock&  clk = clock_ ? *clock_ : deadCard();
    const uint8_t off = (uint8_t)(c.port() - port_);
    if (off < 6) {
        Channel& ch = off < 2 ? a_ : off < 4 ? b_ : c_;
        if (off & 1) ch.uart.writeControl(c.data, clk);
        else         ch.uart.writeData(c.data, clk);
    } else if (off == 6) {
        mask_ = c.data;
    } else {
        outB_ = c.data;
    }
    refresh();
}

// ---------------------------------------------------------------------------
// Interrupts.
// ---------------------------------------------------------------------------
bool BitstreamerBoard::rxLine(const Channel& ch) const {
    return (mask_ & ch.rxBit) && ch.uart.rxReady();
}

// The TxRDY pin needs TxEN, which the chip's status bit does not (reference, the 8251 file).
// CTS is at ground on this board, so only TxEN gates it.
bool BitstreamerBoard::txLine(const Channel& ch) const {
    return (mask_ & ch.txBit) && clock_ && ch.uart.txEnabled() && ch.uart.txReady(*clock_);
}

bool BitstreamerBoard::assertsInt() const {
    if (clockIrq_ == IrqJumper::Int && clockLine()) return true;
    for (const Channel* ch : {&a_, &b_, &c_}) {
        if (ch->rxIrq == IrqJumper::Int && rxLine(*ch)) return true;
        if (ch->txIrq == IrqJumper::Int && txLine(*ch)) return true;
    }
    return false;
}

uint8_t BitstreamerBoard::assertsVi() const {
    uint8_t m = clockLine() ? viBit(clockIrq_) : 0;
    for (const Channel* ch : {&a_, &b_, &c_}) {
        if (rxLine(*ch)) m |= viBit(ch->rxIrq);
        if (txLine(*ch)) m |= viBit(ch->txIrq);
    }
    return m;
}

// Nothing polls a chip for a guest that waits on interrupts, so the receivers advance here, the
// wires are driven, and the alarm is set for the next time a source moves with no bus cycle.
void BitstreamerBoard::refresh() {
    if (!clock_) return;
    for (Channel* ch : {&a_, &b_, &c_}) ch->uart.poll(*clock_);
    intChanged();

    clock_->cancel(wake_);
    wake_ = Clock::kNone;
    if (uint64_t next = nextEdge()) wake_ = clock_->at(next, [this] { refresh(); });
}

// Zero means never. Only a source that passes its mask and has a jumper can leave a deadline, so
// an idle board lets the machine stand down.
uint64_t BitstreamerBoard::nextEdge() const {
    const uint64_t now  = clock_->now();
    uint64_t       best = 0;
    auto consider = [&](uint64_t when) {
        if (when > now && (!best || when < best)) best = when;
    };
    for (const Channel* ch : {&a_, &b_, &c_}) {
        if (ch->txIrq != IrqJumper::None && (mask_ & ch->txBit) && ch->uart.txEnabled())
            consider(ch->uart.txFreeAt());
        if (ch->rxIrq != IrqJumper::None && (mask_ & ch->rxBit)) {
            if (ch->uart.rxFrameActive()) consider(ch->uart.rxDoneAt());
            else if (ch->uart.rxWaiting()) consider(ch->uart.rxNextAt());
        }
    }
    return best;
}

void BitstreamerBoard::armClock() {
    if (!clock_) return;
    if (clockIrq_ == IrqJumper::None && j4d0_ == J4D0::Open) {
        clockTick_.stop(clock_);
        return;
    }
    // Through bus_ at each call: a machine file is built on a scratch bus that goes away.
    clockTick_.setHostNs([this] { return bus_ ? bus_->busClock().hostNs() : 0; });
    clockTick_.start(clock_, kClockTickHz, kClockChain, [this] {
        latch_ = true;
        intChanged();
    });
}

// ---------------------------------------------------------------------------
// Lifecycle.
// ---------------------------------------------------------------------------

// POC* is the reset wire here, and it drives the 8251 RESET pins through U21. The manual gives no
// power-on state for the LS75 latches or U4, so they come up clear: a mask of 0 is the state the
// manual tells software not to rely on anyway. RESET* reaches nothing.
void BitstreamerBoard::reset(Reset r) {
    if (r != Reset::PowerOn) return;
    const Clock& clk = clock_ ? *clock_ : deadCard();
    for (Channel* ch : {&a_, &b_, &c_}) ch->uart.powerOn(clk);
    mask_  = 0;
    outB_  = 0;
    latch_ = false;
    armClock();
    refresh();
}

void BitstreamerBoard::power() { reset(Reset::PowerOn); }

void BitstreamerBoard::pump() {
    clockTick_.pump(clock_);
    for (Channel* ch : {&a_, &b_, &c_}) ch->uart.pump();
    refresh();
}

void BitstreamerBoard::configChanged() {
    decodeChanged();
    for (Channel* ch : {&a_, &b_, &c_}) ch->uart.programLine();
    armClock();
    refresh();
}

// The ticker's schedule is host time, which does not travel, so a restore starts it again.
void BitstreamerBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    for (const Channel* ch : {&a_, &b_, &c_}) ch->uart.serialize(w);
    w.u8(mask_);
    w.u8(outB_);
    w.boolean(latch_);
}

void BitstreamerBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    for (Channel* ch : {&a_, &b_, &c_}) ch->uart.deserialize(r);
    mask_  = r.u8();
    outB_  = r.u8();
    latch_ = r.boolean();
    armClock();
    refresh();
}

uint64_t BitstreamerBoard::rxBytes() const {
    return a_.uart.rxBytes() + b_.uart.rxBytes() + c_.uart.rxBytes();
}

std::vector<std::string> BitstreamerBoard::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    for (Channel* ch : {&a_, &b_, &c_})
        for (auto& s : ch->uart.drainLog()) out.push_back(id + ":" + std::move(s));
    return out;
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
BitstreamerBoard::Channel* BitstreamerBoard::channel(const std::string& unit) {
    std::string n = lowerAscii(unit);
    if (n == "a") return &a_;
    if (n == "b") return &b_;
    if (n == "c") return &c_;
    return nullptr;
}

std::vector<Property> BitstreamerBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Base address (area W): 02, 12, 22, 32, 42, 52, 62 or 72. The board decodes "
                  "eight ports from there";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0x02;
        x.max   = 0x72;
        x.get   = [this] { return Value::ofInt(port_); };
        x.set   = [this](const Value& v, std::string& err) {
            if ((v.i() & 0x0F) != 2) {
                err = "area W sets only the upper digit, so the base must end in 2";
                return false;
            }
            port_ = (uint16_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x = irqJumperProperty(
            "clock_int",
            "Where area A jumpers the clock latch, through mask bit 6: none | int | vi0..vi7",
            clockIrq_);
        x.set = [this, set = x.set](const Value& v, std::string& err) {
            if (!set(v, err)) return false;
            armClock();  // the jumper decides whether a deadline runs
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "j4_d0";
        x.help    = "What drives pin 11 of J4, which is bit 0 of IN x8: open, or clock-latch "
                    "(U4 pin 5). OASIS needs clock-latch. No Vector Graphic document shows it";
        x.kind    = Kind::Enum;
        x.choices = {"open", "clock-latch"};
        x.get     = [this] {
            return Value::ofStr(j4d0_ == J4D0::ClockLatch ? "clock-latch" : "open");
        };
        x.set     = [this](const Value& v, std::string&) {
            j4d0_ = v.s() == "clock-latch" ? J4D0::ClockLatch : J4D0::Open;
            armClock();
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<Property> BitstreamerBoard::channelProperties(Channel& ch) {
    std::vector<Property> p;
    Channel* cp = &ch;  // the channels are members, and they never move
    {
        Property x;
        x.name    = "baud";
        x.help    = "The channel's eight-position switch, for a guest that selects a clock "
                    "factor of 16";
        x.kind    = Kind::Enum;
        x.choices = {"110", "150", "300", "600", "1200", "2400", "4800", "9600"};
        x.get     = [cp] { return Value::ofStr(std::to_string(cp->uart.baud)); };
        x.set     = [cp](const Value& v, std::string&) {
            cp->uart.baud = std::stoll(v.s());
            return true;
        };
        p.push_back(std::move(x));
    }
    p.push_back(irqJumperProperty(
        "rx_int", "Where area A jumpers RxRDY, through its mask bit: none | int | vi0..vi7",
        cp->rxIrq));
    p.push_back(irqJumperProperty(
        "tx_int", "Where area A jumpers TxRDY, through its mask bit: none | int | vi0..vi7. "
                  "It is a level, held while the transmitter is ready",
        cp->txIrq));
    {
        Property x;
        x.name = "connect";
        x.help = "The endpoint on this channel's line (CONNECT sets this)";
        x.kind = Kind::Str;
        x.get  = [cp] { return Value::ofStr(cp->uart.endpoint()); };
        x.set  = [this, cp](const Value& v, std::string& err) {
            return connect(cp->uart.name(), v.s(), err);
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<Property> BitstreamerBoard::unitProperties(const std::string& unit) {
    if (Channel* ch = channel(unit)) return channelProperties(*ch);
    return {};
}

std::vector<UnitDef> BitstreamerBoard::units() const {
    return {{"a", UnitKind::Serial, a_.uart.endpoint()},
            {"b", UnitKind::Serial, b_.uart.endpoint()},
            {"c", UnitKind::Serial, c_.uart.endpoint()}};
}

std::vector<MapEntry> BitstreamerBoard::ioMap() const {
    const uint32_t b = port_;
    return {
        {b, b, "read/write", "Serial A -- data"},
        {b + 1, b + 1, "read/write", "Serial A -- status (R) / mode, command (W)"},
        {b + 2, b + 2, "read/write", "Serial B -- data"},
        {b + 3, b + 3, "read/write", "Serial B -- status (R) / mode, command (W)"},
        {b + 4, b + 4, "read/write", "Serial C -- data"},
        {b + 5, b + 5, "read/write", "Serial C -- status (R) / mode, command (W)"},
        {b + 6, b + 6, "read/write", "J4 lines (R) / parallel A latch, interrupt mask (W)"},
        {b + 7, b + 7, "read/write", "J5 lines, clears the clock latch (R) / parallel B latch (W)"},
    };
}

bool BitstreamerBoard::connect(const std::string& unit, const std::string& ep, std::string& err) {
    Channel* ch = channel(unit);
    if (!ch) {
        err = noUnit(unit);
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    // A path in a machine file is relative to that file. The chip echoes the spec as typed.
    std::vector<std::string> paths;
    std::string spec = rebaseEndpointPaths(ep, [&](const std::string& pth) {
        paths.push_back(pth);
        return resolvePath(pth);
    });
    auto s = g_resolver(spec, err);
    if (!s) {
        for (const std::string& pth : paths) err += pathNote(pth);
        return false;
    }
    ch->uart.connect(std::move(s));
    refresh();
    return true;
}

bool BitstreamerBoard::connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                                     std::string& err) {
    Channel* ch = channel(unit);
    if (!ch) {
        err = noUnit(unit);
        return false;
    }
    ch->uart.connect(std::move(s));
    refresh();
    return true;
}

bool BitstreamerBoard::disconnect(const std::string& unit, std::string& err) {
    Channel* ch = channel(unit);
    if (!ch) {
        err = noUnit(unit);
        return false;
    }
    ch->uart.disconnect();
    refresh();
    return true;
}

ByteStream* BitstreamerBoard::unitStream(const std::string& unit) {
    Channel* ch = channel(unit);
    return ch ? &ch->uart.stream() : nullptr;
}

} // namespace altair
