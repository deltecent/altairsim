#include "boards/compupro-interfacer1.h"

#include "core/statefile.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace altair {

namespace {

InterfacerOneBoard::EndpointResolver g_resolver;

// A board CAN be attached to a bare bus with no machine (and so no clock) around it. A UART
// with no clock cannot time a character; it reads as a dead board. (Same idiom as Io4Board.)
Clock& deadCard() {
    static Clock stopped;
    return stopped;
}

// Switch S1 (page 4): four positions per channel select one of sixteen rates from the
// BR1941/2941 baud generator. The property takes the rate itself; 134.5 runs the UART at 134,
// because the chip's clock is a whole number of baud here.
struct BaudRow {
    const char* name;
    long long   rate;
};
const BaudRow kBauds[] = {
    {"50", 50},     {"75", 75},     {"110", 110},   {"134.5", 134}, {"150", 150},
    {"300", 300},   {"600", 600},   {"1200", 1200}, {"1800", 1800}, {"2000", 2000},
    {"2400", 2400}, {"3600", 3600}, {"4800", 4800}, {"7200", 7200}, {"9600", 9600},
    {"19200", 19200},
};

// The power-up strap byte as the factory example and the user's marked-up manual both
// leave it for a terminal: 8 data bits, odd sense, no parity, 1 stop bit, CA and CD
// spacing (asserted), both interrupts disabled.
constexpr uint8_t kDefaultStrap = InterfacerOneBoard::Nbi | InterfacerOneBoard::Np;

} // namespace

void InterfacerOneBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

InterfacerOneBoard::InterfacerOneBoard() {
    a_.base  = 0x00;  // the CompuPro standard: Channel A at 00-01, Channel B at 02-03
    b_.base  = 0x02;
    a_.strap = kDefaultStrap;
    b_.strap = kDefaultStrap;
    for (Channel* ch : {&a_, &b_}) {
        ch->uart.baud = 9600;
        ch->uart.disconnect();  // -> NullStream: a dead line, never a null pointer
        applyLevel(*ch);
    }
}

InterfacerOneBoard::~InterfacerOneBoard() {
    if (clock_) clock_->cancel(wake_);  // a deadline firing into a freed board is a UAF
}

// ---------------------------------------------------------------------------
// Decode
// ---------------------------------------------------------------------------

// Unit names match in any case, as on the io4 (MCP passes the operator's spelling through).
InterfacerOneBoard::Channel* InterfacerOneBoard::channel(const std::string& u) {
    const std::string n = lowerAscii(u);
    return n == "a" ? &a_ : n == "b" ? &b_ : nullptr;
}
const InterfacerOneBoard::Channel* InterfacerOneBoard::channel(const std::string& u) const {
    const std::string n = lowerAscii(u);
    return n == "a" ? &a_ : n == "b" ? &b_ : nullptr;
}

// Each channel compares A7-A1 against its own switch (U19/U20, 25LS2521), and A0 picks the
// port. If both channels are set to the same block, the real card drives the data bus from
// both UARTs at once. The bus sees ONE board here and cannot report that, so channel A
// answers, B is shadowed, and configChanged() says so in the log.
bool InterfacerOneBoard::decodePort(uint8_t port, Channel*& ch, bool& isData) const {
    for (const Channel* c : {&a_, &b_}) {
        if (!c->enabled || (uint8_t)(port & 0xFE) != c->base) continue;
        ch     = const_cast<Channel*>(c);
        isData = ((port & 1) == 0) != swapPorts_;  // factory: data at the even address
        return true;
    }
    return false;
}

bool InterfacerOneBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    Channel* ch     = nullptr;
    bool     isData = false;
    return decodePort(c.port(), ch, isData);
}

// ---------------------------------------------------------------------------
// The status byte and the control latch
// ---------------------------------------------------------------------------

// D0 TBMT, D1 DAV, D2 OPT, D3 PE, D4 OR, D5 FE, D6 CC, D7 CB. U27/U28 (81LS95) do not invert,
// so the UART's own bits read true. CB, CC and CF arrive through a 1489 (U2/U6), which does
// invert: an asserted (ON) line reads 0. EOC, strapped to OPT at J12/J13, comes straight from
// the UART and reads true.
uint8_t InterfacerOneBoard::status(Channel& ch) {
    const Clock&     clk = clock_ ? *clock_ : deadCard();
    const LineStatus ls  = ch.uart.stream().status();
    uint8_t          s   = 0;
    if (ch.uart.txBufferEmpty(clk)) s |= 0x01;
    if (ch.uart.dataAvailable()) s |= 0x02;
    if (ch.optEoc ? ch.uart.txEndOfChar(clk) : !ls.carrier) s |= 0x04;
    if (ch.uart.parityError()) s |= 0x08;
    if (ch.uart.overrun()) s |= 0x10;
    if (ch.uart.framingError()) s |= 0x20;
    if (!ls.dsr) s |= 0x40;  // CC (DB25 pin 6 in master mode)
    if (!ls.cts) s |= 0x80;  // CB (DB25 pin 5 in master mode)
    return s;
}

// The live levels went to the UART's format pins (NB1, EPS, NP, TSB; NB2 is tied high, so
// NB1 picks 7 or 8 bits) and to the 1488 drivers for CA and CD.
void InterfacerOneBoard::applyLevel(Channel& ch) {
    const uint8_t lv = ch.level();
    ch.uart.dataBits = (lv & Nbi) ? 8 : 7;
    ch.uart.stopBits = (lv & Tsb) ? 2 : 1;
    ch.uart.parity   = (lv & Np) ? LineParity::None : (lv & Eps) ? LineParity::Even : LineParity::Odd;
    driveModemLines(ch);
}

// A 1488 inverts: a latch level of 1 drives the line negative, which is MARKING, which on a
// control line is OFF. So the manual's "spacing" is the asserted state.
void InterfacerOneBoard::driveModemLines(Channel& ch) {
    LineControl c;
    c.rts = !(ch.level() & CaOut);
    c.dtr = !(ch.level() & CdOut);
    ch.uart.stream().setControl(c);
}

void InterfacerOneBoard::programChannel(Channel& ch) {
    ch.uart.programLine();
    for (auto& s : ch.uart.drainLog()) log_.push_back(id + ":" + ch.uart.name() + " " + std::move(s));
}

uint8_t InterfacerOneBoard::controlLevel(const std::string& unit) const {
    const Channel* ch = channel(unit);
    return ch ? ch->level() : 0;
}

uint8_t InterfacerOneBoard::read(const BusCycle& c) {
    Channel* ch     = nullptr;
    bool     isData = false;
    if (!decodePort(c.port(), ch, isData)) return 0xFF;
    ch->uart.poll(clock_ ? *clock_ : deadCard());
    // RDAV is tied to RDE: reading the data port clears DAV. The other address is SWE.
    uint8_t v = isData ? ch->uart.readData() : status(*ch);
    refresh();  // a data read can drop RxINT
    return v;
}

void InterfacerOneBoard::write(const BusCycle& c) {
    Channel* ch     = nullptr;
    bool     isData = false;
    if (!decodePort(c.port(), ch, isData)) return;
    if (isData) {
        ch->uart.writeData(c.data, clock_ ? *clock_ : deadCard());
    } else {
        // The control port: latch the byte. A 1 flips a line from its strap, a 0 restores it.
        const uint8_t old = ch->level();
        ch->latch         = c.data;
        if (ch->level() != old) {
            applyLevel(*ch);
            if ((uint8_t)(old ^ ch->level()) & kFormat) programChannel(*ch);
        }
    }
    refresh();  // TBMT fell, or an interrupt enable moved
}

// ---------------------------------------------------------------------------
// Interrupts (U16 AND gates, U29 inverters, J11)
// ---------------------------------------------------------------------------

uint8_t InterfacerOneBoard::assertsVi() const {
    if (!clock_) return 0;
    const Clock& clk = *clock_;
    uint8_t      m   = 0;
    for (const Channel* ch : {&a_, &b_}) {
        if ((ch->level() & RxIntE) && ch->uart.dataAvailable()) m |= viBit(ch->rxIrq);
        if ((ch->level() & TxIntE) && ch->uart.txBufferEmpty(clk)) m |= viBit(ch->txIrq);
    }
    return m;
}

void InterfacerOneBoard::refresh() {
    if (!clock_) return;
    a_.uart.poll(*clock_);
    b_.uart.poll(*clock_);
    intChanged();
    clock_->cancel(wake_);
    wake_ = Clock::kNone;
    if (uint64_t next = nextEdge()) wake_ = clock_->at(next, [this] { refresh(); });
}

// The next T-state at which an enabled, jumpered source could move by itself. Zero = never.
uint64_t InterfacerOneBoard::nextEdge() const {
    const Clock& clk  = clock_ ? *clock_ : deadCard();
    uint64_t     best = 0;
    auto         consider = [&](uint64_t when) {
        if (when <= clk.now()) return;  // strictly future, or the drain loop never stops
        if (!best || when < best) best = when;
    };
    for (const Channel* ch : {&a_, &b_}) {
        if (ch->txIrq != IrqJumper::None && (ch->level() & TxIntE)) consider(ch->uart.txFreeAt());
        if (ch->rxIrq != IrqJumper::None && (ch->level() & RxIntE) && ch->uart.rxWaiting())
            consider(ch->uart.rxNextAt());
    }
    return best;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// POC and pRESET both clear the 74LS175s (U21/U24 reset logic), so the board is back at its
// straps; MR on each UART clears DAV and frees the transmitter. The lines stay connected.
void InterfacerOneBoard::reset(Reset) {
    const Clock& clk = clock_ ? *clock_ : deadCard();
    for (Channel* ch : {&a_, &b_}) {
        const uint8_t old = ch->level();
        ch->latch         = 0;
        ch->uart.masterReset(clk);
        applyLevel(*ch);
        if ((uint8_t)(old ^ ch->level()) & kFormat) programChannel(*ch);
    }
    refresh();
}

void InterfacerOneBoard::power() { reset(Reset::PowerOn); }

void InterfacerOneBoard::pump() {
    for (Channel* ch : {&a_, &b_}) {
        ch->uart.pump();
        if (clock_) ch->uart.poll(*clock_);
    }
    refresh();
}

void InterfacerOneBoard::configChanged() {
    decodeChanged();
    const bool overlap = a_.enabled && b_.enabled && a_.base == b_.base;
    if (overlap && !overlapWarned_) {
        char buf[8];
        std::snprintf(buf, sizeof buf, "%02X", a_.base);
        log_.push_back(id + ": channels a and b are both at port " + buf +
                       " -- the real board would drive the bus twice; here channel a answers "
                       "and b is unreachable");
    }
    overlapWarned_ = overlap;  // say it once when it happens, not after every SET
    for (Channel* ch : {&a_, &b_}) {
        applyLevel(*ch);
        programChannel(*ch);
    }
    refresh();
}

void InterfacerOneBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    for (const Channel* ch : {&a_, &b_}) {
        ch->uart.serialize(w);
        w.u8(ch->latch);  // runtime state; the straps are config and come from the TOML
    }
}

void InterfacerOneBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    for (Channel* ch : {&a_, &b_}) {
        ch->uart.deserialize(r);
        ch->latch = r.u8();
        applyLevel(*ch);
        programChannel(*ch);  // a restored format must reach a real serial port too
    }
    refresh();
}

std::vector<std::string> InterfacerOneBoard::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    for (Channel* ch : {&a_, &b_}) {
        for (auto& s : ch->uart.drainLog()) out.push_back(id + ":" + ch->uart.name() + " " + std::move(s));
        // ...and whatever the far end of the line has to say (a printer that could not spool).
        for (auto& s : ch->uart.stream().drainLog())
            out.push_back(id + ":" + ch->uart.name() + " " + std::move(s));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Reflection
// ---------------------------------------------------------------------------

std::vector<Property> InterfacerOneBoard::properties() {
    std::vector<Property> p;
    Property x;
    x.name = "swap_ports";
    x.help = "J14: move the trace from B-C to A-B, so both channels have status at the "
             "switch address and data at +1. Off (factory) = data first, status second";
    x.kind = Kind::Bool;
    x.get  = [this] { return Value::ofBool(swapPorts_); };
    x.set  = [this](const Value& v, std::string&) {
        swapPorts_ = v.b();
        return true;
    };
    p.push_back(std::move(x));
    return p;
}

std::vector<Property> InterfacerOneBoard::unitProperties(const std::string& unit) {
    if (Channel* ch = channel(unit)) return channelProperties(*ch);
    return {};
}

std::vector<Property> InterfacerOneBoard::channelProperties(Channel& ch) {
    std::vector<Property> p;
    Channel* cp = &ch;  // the channel members never move

    {
        Property x;
        x.name  = "port";
        x.help  = "Switch S2 (Channel A) / S3 (Channel B), positions 1-7: the channel's "
                  "2-port block, A7-A1, so an even address";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xFE;
        x.get   = [cp] { return Value::ofInt(cp->base); };
        x.set   = [cp](const Value& v, std::string& err) {
            if (v.i() & 1) {
                err = "the Interfacer 1 decodes A7-A1 -- a channel's port must be even";
                return false;
            }
            cp->base = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "enabled";
        x.help = "Switch S2/S3 position 8: off = ON = the channel is removed from the I/O space";
        x.kind = Kind::Bool;
        x.get  = [cp] { return Value::ofBool(cp->enabled); };
        x.set  = [cp](const Value& v, std::string&) {
            cp->enabled = v.b();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "baud";
        x.help    = "Switch S1 (positions 1-4 for A, 5-8 for B): the BR1941 rate";
        x.kind    = Kind::Enum;
        for (const BaudRow& b : kBauds) x.choices.push_back(b.name);
        x.get = [cp] {
            for (const BaudRow& b : kBauds)
                if (b.rate == cp->uart.baud) return Value::ofStr(b.name);
            return Value::ofStr("9600");
        };
        x.set = [cp](const Value& v, std::string&) {
            for (const BaudRow& b : kBauds)
                if (v.s() == b.name) cp->uart.baud = b.rate;
            return true;
        };
        p.push_back(std::move(x));
    }

    // The power-up straps: the trace left uncut in J7/J9 (A) or J8/J10 (B) for each line.
    auto strapEnum = [&](const char* name, const char* help, uint8_t bit, const char* zero,
                         const char* one) {
        Property x;
        x.name    = name;
        x.help    = help;
        x.kind    = Kind::Enum;
        x.choices = {zero, one};
        std::string z = zero, o = one;
        x.get = [cp, bit, z, o] { return Value::ofStr((cp->strap & bit) ? o : z); };
        x.set = [cp, bit, o](const Value& v, std::string&) {
            cp->strap = (uint8_t)(v.s() == o ? (cp->strap | bit) : (cp->strap & ~bit));
            return true;
        };
        p.push_back(std::move(x));
    };
    strapEnum("data_bits", "Power-up NBI (J7/J8 1-16 = 7, 2-15 = 8). Control bit D7 flips it",
              Nbi, "7", "8");
    strapEnum("parity_sense", "Power-up EPS (J7/J8 3-14 = odd, 4-13 = even). Control bit D6 "
              "flips it", Eps, "odd", "even");
    strapEnum("parity", "Power-up NP (J7/J8 5-12 = on, 6-11 = off). Control bit D5 flips it",
              Np, "on", "off");
    strapEnum("stop_bits", "Power-up TSB (J7/J8 7-10 = 1, 8-9 = 2). Control bit D4 flips it",
              Tsb, "1", "2");
    strapEnum("ca", "Power-up CA, RS-232 RTS (J9/J10 1-16 = spacing/ON, 2-15 = marking/OFF). "
              "Control bit D3 flips it", CaOut, "spacing", "marking");
    strapEnum("cd", "Power-up CD, RS-232 DTR (J9/J10 3-14 = spacing/ON, 4-13 = marking/OFF). "
              "Control bit D2 flips it", CdOut, "spacing", "marking");
    strapEnum("tx_int_enable", "Power-up TXINTE (J9/J10 5-12 = off, 6-11 = on). Control bit D1 "
              "flips it", TxIntE, "off", "on");
    strapEnum("rx_int_enable", "Power-up RXINTE (J9/J10 7-10 = off, 8-9 = on). Control bit D0 "
              "flips it", RxIntE, "off", "on");

    {
        Property x;
        x.name    = "opt";
        x.help    = "J12 (A) / J13 (B): what status bit D2 reads -- cf = the RS-232 CF input "
                    "(B-A, reads 0 when ON), eoc = the UART's End Of Character (B-C)";
        x.kind    = Kind::Enum;
        x.choices = {"cf", "eoc"};
        x.get     = [cp] { return Value::ofStr(cp->optEoc ? "eoc" : "cf"); };
        x.set     = [cp](const Value& v, std::string&) {
            cp->optEoc = v.s() == "eoc";
            return true;
        };
        p.push_back(std::move(x));
    }

    // J11: VI0-VI7 only. irqJumperProperty() offers `int` too; the board refuses it.
    auto viOnly = [&](const char* name, const char* help, IrqJumper& j) {
        // Dropping `int` from the choices is the whole refusal: validation rejects it before
        // the setter runs, and SHOW, completion and the reference all list VI lines only.
        Property x = irqJumperProperty(name, help, j);
        std::erase(x.choices, std::string("int"));
        p.push_back(std::move(x));
    };
    viOnly("rx_int", "J11: where RxINT (DAV AND RXINTE) is jumpered: none | vi0..vi7",
           cp->rxIrq);
    viOnly("tx_int", "J11: where TxINT (TBMT AND TXINTE) is jumpered: none | vi0..vi7. A LEVEL, "
           "held while the transmitter is idle", cp->txIrq);

    {
        Property x;
        x.name = "control";
        x.help = "What the guest last wrote to the control port (the 74LS175 latches)";
        x.kind = Kind::Int;
        x.radix = 16;
        x.get  = [cp] { return Value::ofInt(cp->latch); };
        p.push_back(std::move(x));  // no setter: read-only
    }
    {
        Property x;
        x.name = "connect";
        x.help = "The endpoint on this channel's line (CONNECT sets this): a file, socket, "
                 "serial port, in:/out: file, null, loopback";
        x.kind = Kind::Str;
        x.get  = [cp] { return Value::ofStr(cp->uart.endpoint()); };
        x.set  = [this, cp](const Value& v, std::string& err) {
            return connect(cp->uart.name(), v.s(), err);
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<UnitDef> InterfacerOneBoard::units() const {
    return {
        {"a", UnitKind::Serial, a_.uart.endpoint()},
        {"b", UnitKind::Serial, b_.uart.endpoint()},
    };
}

std::vector<MapEntry> InterfacerOneBoard::ioMap() const {
    std::vector<MapEntry> m;
    for (const Channel* ch : {&a_, &b_}) {
        if (!ch->enabled) continue;
        const std::string w  = ch == &a_ ? "Channel A" : "Channel B";
        const uint32_t    da = swapPorts_ ? ch->base + 1u : ch->base;
        const uint32_t    st = swapPorts_ ? ch->base : ch->base + 1u;
        m.push_back({da, da, "read/write", w + " -- receive (R) / transmit (W)"});
        m.push_back({st, st, "read/write", w + " -- status (R) / control latch (W)"});
    }
    return m;
}

static std::string noUnit(const std::string& unit) {
    return "interfacer1 has no unit '" + unit + "' -- its channels are 'a' and 'b'";
}

bool InterfacerOneBoard::connect(const std::string& unit, const std::string& ep, std::string& err) {
    Channel* ch = channel(unit);
    if (!ch) {
        err = noUnit(unit);
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
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
    ch->uart.connect(std::move(s));
    driveModemLines(*ch);  // a new line hears the board's CA and CD at once
    refresh();
    return true;
}

bool InterfacerOneBoard::connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                                       std::string& err) {
    Channel* ch = channel(unit);
    if (!ch) {
        err = noUnit(unit);
        return false;
    }
    ch->uart.connect(std::move(s));
    driveModemLines(*ch);
    return true;
}

bool InterfacerOneBoard::disconnect(const std::string& unit, std::string& err) {
    Channel* ch = channel(unit);
    if (!ch) {
        err = noUnit(unit);
        return false;
    }
    ch->uart.disconnect();
    refresh();
    return true;
}

ByteStream* InterfacerOneBoard::unitStream(const std::string& unit) {
    Channel* ch = channel(unit);
    return ch ? &ch->uart.stream() : nullptr;
}

} // namespace altair
