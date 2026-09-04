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

// ---------------------------------------------------------------------------
// THE STATUS-HEADER PROFILES. One entry = one host personality a channel can select by
// name, presetting the whole W1/W2 status strap-up: which data bit each of the six status
// signals lands on, the buffer polarity (74367 positive / 74368 negative), and whether the
// two port addresses are reversed.
//
// These recipes come straight from the IO-4 manual's applications section -- they are the
// strap tables SSM published for imitating specific cards. The names are io4-native
// (deliberately NOT shared with gsio's `sior0`/`sior1`): a profile here is a richer bundle
// (map + polarity + port order) than a gsio strap preset, and naming them for the host each
// imitates keeps the two boards from implying a false equivalence.
//
// `stat[]` is indexed by Io4Board::StatSig -- {Dav, Ror, Rpe, Rfe, Teoc, Tbmt} -- and -1
// means "not jumpered" (that signal drives no data bit). To add a personality, add a row:
// its `name` becomes a `profile` choice and appears in SHOW, tab completion and the
// generated reference with no other edit.
// ---------------------------------------------------------------------------
struct Io4Profile {
    const char* name;
    const char* help;
    int         stat[Io4Board::kNumStat];  // Dav, Ror, Rpe, Rfe, Teoc, Tbmt; -1 = none
    bool        invert;                     // 74368 (negative sense) if true
    bool        pr;                         // S1/S2-PR: reverse status/data addresses
};

const std::vector<Io4Profile>& io4Profiles() {
    static const std::vector<Io4Profile> kP = {
        // Altair 88-SIO Rev 1 -- THE DEFAULT (index 0). DAV -> D0, TBMT -> D7, status
        // inverted (a 74LS368 in the U18/U16 socket). This is exactly the strapping the SSM
        // 8080 System Monitor's console expects -- it spins while D0=1 waiting for a byte and
        // while D7=1 waiting to send -- so a stock io4 boots it. The manual gives this as its
        // Altair-Rev-1 emulation recipe; a Rev 1 88-SIO drives only bits 7 and 0 (inverted),
        // which is what this reproduces.
        {"altair-rev1",
         "Altair 88-SIO Rev 1 / the SSM 8080 monitor console: DAV=D0, TBMT=D7, status inverted",
         {/*Dav*/ 0, /*Ror*/ -1, /*Rpe*/ -1, /*Rfe*/ -1, /*Teoc*/ -1, /*Tbmt*/ 7}, true, false},

        // Altair 88-SIO Rev 0. DAV -> D5, TBMT -> D1, positive sense. The manual imitates a
        // Rev 0 board through its two TRUE-sense status bits (bit 5 = data available, bit 1 =
        // transmit buffer empty); the inverted ready bits at 7/0 a real Rev 0 also carries are
        // a separate pair this recipe does not strap.
        {"altair-rev0", "Altair 88-SIO Rev 0: DAV=D5, TBMT=D1, positive sense",
         {/*Dav*/ 5, /*Ror*/ -1, /*Rpe*/ -1, /*Rfe*/ -1, /*Teoc*/ -1, /*Tbmt*/ 1}, false, false},

        // Intel 8251 (async USART). The full six-signal map: TBMT -> D0, DAV -> D1, TEOC -> D2,
        // RPE -> D3, ROR -> D4, RFE -> D5, positive sense. The three error bits and TEOC are
        // strapped even though this UART reports the errors always-inactive -- a driver that
        // masks them sees exactly the 8251's status layout.
        {"i8251", "Intel 8251 async USART: TBMT=D0, DAV=D1, TEOC=D2, RPE=D3, ROR=D4, RFE=D5",
         {/*Dav*/ 1, /*Ror*/ 4, /*Rpe*/ 3, /*Rfe*/ 5, /*Teoc*/ 2, /*Tbmt*/ 0}, false, false},

        // Processor Technology serial port. DAV -> D6, TBMT -> D7, positive sense.
        {"proctech", "Processor Technology serial: DAV=D6, TBMT=D7, positive sense",
         {/*Dav*/ 6, /*Ror*/ -1, /*Rpe*/ -1, /*Rfe*/ -1, /*Teoc*/ -1, /*Tbmt*/ 7}, false, false},

        // IMSAI serial. DAV -> D1, TBMT -> D0, positive sense, AND the ports reversed
        // (S1/S2-PR ON -- data first, status last), which is what IMSAI software expects.
        {"imsai", "IMSAI serial: DAV=D1, TBMT=D0, positive sense, ports reversed (data first)",
         {/*Dav*/ 1, /*Ror*/ -1, /*Rpe*/ -1, /*Rfe*/ -1, /*Teoc*/ -1, /*Tbmt*/ 0}, false, true},
    };
    return kP;
}

} // namespace

void Io4Board::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

Io4Board::Io4Board() {
    // The board ships strapped for the SSM 8080 monitor console (profile 0, altair-rev1), so a
    // machine that adds a stock io4 and points a console at channel A boots with no config.
    applyProfile(a_, 0);
    applyProfile(b_, 0);

    // -> NullStream on both channels. There is never a null pointer in the stream path: a
    // channel with nothing plugged into it has a DEAD line, not a dangling one.
    a_.uart.disconnect();
    b_.uart.disconnect();
}

Io4Board::~Io4Board() = default;

// ---------------------------------------------------------------------------
// Profiles -- preset the straps, and read back which one they match.
// ---------------------------------------------------------------------------

void Io4Board::applyProfile(SerialChannel& ch, int idx) {
    const Io4Profile& p = io4Profiles()[(size_t)idx];
    for (int i = 0; i < kNumStat; ++i) ch.statBit[i] = p.stat[i];
    ch.invert       = p.invert;
    ch.portReversal = p.pr;
}

std::string Io4Board::profileName(const SerialChannel& ch) const {
    for (const auto& p : io4Profiles()) {
        bool same = p.invert == ch.invert && p.pr == ch.portReversal;
        for (int i = 0; same && i < kNumStat; ++i) same = ch.statBit[i] == p.stat[i];
        if (same) return p.name;
    }
    return "custom";
}

// ---------------------------------------------------------------------------
// Addressing -- switch S3, a 4-port block. A at base+0/+1, B at base+2/+3, each channel's
// two ports optionally reversed by its own PR strap.
// ---------------------------------------------------------------------------

bool Io4Board::decodePort(uint8_t port, SerialChannel*& ch, bool& isData) const {
    uint8_t off = (uint8_t)(port - base_);
    if (off >= 4) return false;
    // const_cast: the two members are non-const; decodePort is used from const decodes()
    // (where the out-params are discarded) and from non-const read/write.
    ch = const_cast<SerialChannel*>(off < 2 ? &a_ : &b_);
    bool odd = (off & 1) != 0;         // default: status/control at the even port, data at odd
    isData   = odd ^ ch->portReversal;  // PR swaps the two addresses within the channel
    return true;
}

bool Io4Board::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    SerialChannel* ch     = nullptr;
    bool           isData = false;
    return decodePort(c.port(), ch, isData);
}

// Compose the status byte from the W1/W2 strap map. Each of the six status signals, if
// jumpered to a data bit, drives that bit -- XORed against the channel's polarity, so a
// 74368 (negative sense) makes an asserted signal read 0 and an inactive one read 1. A bit
// no signal is jumpered to reads 0 (nothing drives it in this model).
uint8_t Io4Board::statusByte(const SerialChannel& ch) const {
    const Clock& clk = clock_ ? *clock_ : deadCard();

    bool sig[kNumStat];
    sig[Dav]  = ch.uart.dataAvailable();
    sig[Ror]  = ch.uart.overrun();        // always false -- no line-noise model (DESIGN.md 0.1)
    sig[Rpe]  = ch.uart.parityError();    // "
    sig[Rfe]  = ch.uart.framingError();   // "
    sig[Teoc] = ch.uart.txEndOfChar(clk);
    sig[Tbmt] = ch.uart.txBufferEmpty(clk);

    uint8_t s = 0;
    for (int i = 0; i < kNumStat; ++i) {
        if (ch.statBit[i] < 0) continue;       // not jumpered -- this signal drives nothing
        if (sig[i] ^ ch.invert) s |= (uint8_t)(1u << ch.statBit[i]);
    }
    return s;
}

uint8_t Io4Board::read(const BusCycle& c) {
    SerialChannel* ch     = nullptr;
    bool           isData = false;
    if (!decodePort(c.port(), ch, isData)) return 0xFF;  // decodes() gates this; be defensive

    // The receiver runs on the UART's own clock, not on ours -- advance it before reading.
    ch->uart.poll(clock_ ? *clock_ : deadCard());

    // The DATA port's read strobe is wired to /RDAR: reading it clears Data Available. The
    // other port is /SWE -- the synthesized status byte.
    return isData ? ch->uart.readData() : statusByte(*ch);
}

void Io4Board::write(const BusCycle& c) {
    SerialChannel* ch     = nullptr;
    bool           isData = false;
    if (!decodePort(c.port(), ch, isData)) return;

    if (isData) {
        // The chip's /TDS strobe: the character goes out, TBMT falls until it has left.
        ch->uart.writeData(c.data, clock_ ? *clock_ : deadCard());
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
    a_.uart.masterReset(clk);
    b_.uart.masterReset(clk);
}

void Io4Board::power() { reset(Reset::PowerOn); }

// THE ONE DOOR THE OUTSIDE WORLD COMES THROUGH (DESIGN.md 7.1): drain the host into each line,
// then advance each receiver so a byte that just arrived is ready for the next status poll.
void Io4Board::pump() {
    a_.uart.pump();
    b_.uart.pump();
    if (clock_) {
        a_.uart.poll(*clock_);
        b_.uart.poll(*clock_);
    }
}

// A strap moved: a port change relocates the block (decodeChanged), and a word-format strap
// (`baud`/`data_bits`/`stop_bits`/`parity`) restraps a real serial port on the far end. The
// status-map straps need no far-end action -- they only shape the byte statusByte() builds.
void Io4Board::configChanged() {
    decodeChanged();
    programChannel(a_);
    programChannel(b_);
}

void Io4Board::programChannel(SerialChannel& ch) {
    ch.uart.programLine();
    for (auto& s : ch.uart.drainLog()) log_.push_back(id + ":" + ch.uart.name() + " " + std::move(s));
}

void Io4Board::serialize(StateWriter& w) const {
    Board::serialize(w);  // enabled_
    a_.uart.serialize(w);
    b_.uart.serialize(w);
    // The straps (status map, polarity, PR, base) are CONFIG -- re-applied from TOML into a
    // machine built from the same file a snapshot restores into -- so they do not travel.
}

void Io4Board::deserialize(StateReader& r) {
    Board::deserialize(r);
    a_.uart.deserialize(r);
    b_.uart.deserialize(r);
}

std::vector<std::string> Io4Board::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    for (SerialChannel* ch : {&a_, &b_})
        for (auto& s : ch->uart.drainLog())
            out.push_back(id + ":" + ch->uart.name() + " " + std::move(s));
    return out;
}

// ---------------------------------------------------------------------------
// Reflection
// ---------------------------------------------------------------------------

Io4Board::SerialChannel* Io4Board::channel(const std::string& u) {
    std::string n = lowerAscii(u);
    if (n == "a") return &a_;
    if (n == "b") return &b_;
    return nullptr;
}
const Io4Board::SerialChannel* Io4Board::channel(const std::string& u) const {
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

// The per-channel straps and the connector. On the real card these are switch S2 (Serial A) /
// S1 (Serial B) for the word format, the W1/W2 header for the status map, the U18/U16 buffer
// for polarity, and the W3 baud header. Here they are the UART's own format pins plus the
// board's status-shaping straps, captured by pointer (the SerialChannel members never move).
std::vector<Property> Io4Board::channelProperties(SerialChannel& ch) {
    std::vector<Property> p;
    SerialChannel* cp = &ch;  // stable: the channel members never move

    // THE PROFILE SELECTOR, FIRST so CONFIG SAVE writes it before the individual straps and a
    // saved `profile=imsai` + a hand-override reload in the right order. Its choices are
    // `custom` plus every built-in personality; selecting one presets the map/polarity/PR,
    // `custom` leaves them as they are. The getter reports whichever profile the live straps
    // match (or `custom`), so a hand-strapped channel that happens to match a card names it.
    {
        Property x;
        x.name = "profile";
        x.help = "Preset the status straps from a host personality: custom, altair-rev1 "
                 "(the default and the SSM 8080 monitor console), altair-rev0, i8251, "
                 "proctech, imsai. Sets stat_*, invert_status and port_reversal (overridable)";
        x.kind = Kind::Enum;
        x.choices = {"custom"};
        for (const auto& pr : io4Profiles()) x.choices.push_back(pr.name);
        x.get = [this, cp] { return Value::ofStr(profileName(*cp)); };
        x.set = [this, cp](const Value& v, std::string&) {
            if (v.s() != "custom") {
                const auto& profs = io4Profiles();
                for (size_t i = 0; i < profs.size(); ++i)
                    if (v.s() == profs[i].name) { applyProfile(*cp, (int)i); break; }
            }
            return true;
        };
        p.push_back(std::move(x));
    }

    // The six W1/W2 status-header straps: each signal to a data bit (0-7) or `none`.
    auto statProp = [cp](const char* name, const char* help, StatSig sig) {
        Property x;
        x.name    = name;
        x.help    = help;
        x.kind    = Kind::Enum;
        x.choices = {"none", "0", "1", "2", "3", "4", "5", "6", "7"};
        x.get     = [cp, sig] {
            int b = cp->statBit[sig];
            return Value::ofStr(b < 0 ? std::string("none") : std::string(1, (char)('0' + b)));
        };
        x.set = [cp, sig](const Value& v, std::string&) {
            cp->statBit[sig] = (v.s() == "none") ? -1 : (int)(v.s()[0] - '0');
            return true;
        };
        return x;
    };
    p.push_back(statProp("stat_dav", "Data-bus bit carrying DAV, data available (0-7 | none)", Dav));
    p.push_back(statProp("stat_tbmt", "Data-bus bit carrying TBMT, transmit buffer empty (0-7 | none)", Tbmt));
    p.push_back(statProp("stat_teoc", "Data-bus bit carrying TEOC, transmitter end of character (0-7 | none)", Teoc));
    p.push_back(statProp("stat_ror", "Data-bus bit carrying ROR, receiver over-run (0-7 | none; always inactive)", Ror));
    p.push_back(statProp("stat_rpe", "Data-bus bit carrying RPE, receiver parity error (0-7 | none; always inactive)", Rpe));
    p.push_back(statProp("stat_rfe", "Data-bus bit carrying RFE, receiver framing error (0-7 | none; always inactive)", Rfe));

    // U16/U18: 74368 (negative sense) vs 74367 (positive). One polarity for the whole byte.
    {
        Property x;
        x.name = "invert_status";
        x.help = "Invert every status bit -- a 74368 buffer (negative sense): an asserted "
                 "signal reads 0, an inactive one reads 1. Off = 74367 (positive sense)";
        x.kind = Kind::Bool;
        x.get  = [cp] { return Value::ofBool(cp->invert); };
        x.set  = [cp](const Value& v, std::string&) {
            cp->invert = v.b();
            return true;
        };
        p.push_back(std::move(x));
    }
    // S1/S2-PR: reverse the channel's two port addresses.
    {
        Property x;
        x.name = "port_reversal";
        x.help = "Reverse the channel's two ports (S1/S2-PR): off = status first, data last "
                 "(MITS, Proc Tech); on = data first, status last (IMSAI)";
        x.kind = Kind::Bool;
        x.get  = [cp] { return Value::ofBool(cp->portReversal); };
        x.set  = [cp](const Value& v, std::string&) {
            cp->portReversal = v.b();
            return true;
        };
        p.push_back(std::move(x));
    }

    // ---- The word format (S2 = Serial A / S1 = Serial B) plus the baud header W3. ----
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
        x.get   = [cp] { return Value::ofInt(cp->uart.baud); };
        x.set   = [cp](const Value& v, std::string&) {
            cp->uart.baud = v.i();
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
        x.get   = [cp] { return Value::ofInt(cp->uart.dataBits); };
        x.set   = [cp](const Value& v, std::string&) {
            cp->uart.dataBits = (int)v.i();
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
        x.get   = [cp] { return Value::ofInt(cp->uart.stopBits); };
        x.set   = [cp](const Value& v, std::string&) {
            cp->uart.stopBits = (int)v.i();
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
        x.get     = [cp] {
            switch (cp->uart.parity) {
            case LineParity::Odd:  return Value::ofStr("odd");
            case LineParity::Even: return Value::ofStr("even");
            default:               return Value::ofStr("none");
            }
        };
        x.set = [cp](const Value& v, std::string&) {
            const std::string& s = v.s();
            cp->uart.parity = (s == "odd") ? LineParity::Odd : (s == "even") ? LineParity::Even
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
        x.get  = [cp] { return Value::ofStr(cp->uart.endpoint()); };
        x.set  = [this, cp](const Value& v, std::string& err) {
            return connect(cp->uart.name(), v.s(), err);
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<Property> Io4Board::unitProperties(const std::string& unit) {
    SerialChannel* ch = channel(unit);
    if (!ch) return {};
    return channelProperties(*ch);
}

std::vector<UnitDef> Io4Board::units() const {
    return {
        {"a", UnitKind::Serial, a_.uart.endpoint()},
        {"b", UnitKind::Serial, b_.uart.endpoint()},
    };
}

std::vector<MapEntry> Io4Board::ioMap() const {
    // Each channel's status and data addresses depend on its PR strap.
    auto rows = [](const SerialChannel& ch, uint8_t p0, const char* who) {
        uint8_t st = ch.portReversal ? (uint8_t)(p0 + 1) : p0;
        uint8_t da = ch.portReversal ? p0 : (uint8_t)(p0 + 1);
        std::string w = who;
        return std::vector<MapEntry>{
            {(uint32_t)st, (uint32_t)st, "read/write", w + " -- status (R) / control, discarded (W)"},
            {(uint32_t)da, (uint32_t)da, "read/write", w + " -- receive (R) / transmit (W)"},
        };
    };
    std::vector<MapEntry> m;
    for (auto& e : rows(a_, base_, "Serial A")) m.push_back(e);
    for (auto& e : rows(b_, (uint8_t)(base_ + 2), "Serial B")) m.push_back(e);
    return m;
}

bool Io4Board::connect(const std::string& unit, const std::string& ep, std::string& err) {
    SerialChannel* ch = channel(unit);
    if (!ch) {
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
    ch->uart.connect(std::move(s));  // the chip owns the line and brings it up to the straps
    return true;
}

bool Io4Board::connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                             std::string& err) {
    SerialChannel* ch = channel(unit);
    if (!ch) {
        err = "io4 has no unit '" + unit + "' -- its serial channels are 'a' and 'b'";
        return false;
    }
    ch->uart.connect(std::move(s));
    return true;
}

bool Io4Board::disconnect(const std::string& unit, std::string& err) {
    SerialChannel* ch = channel(unit);
    if (!ch) {
        err = "io4 has no unit '" + unit + "' -- its serial channels are 'a' and 'b'";
        return false;
    }
    ch->uart.disconnect();
    return true;
}

ByteStream* Io4Board::unitStream(const std::string& unit) {
    SerialChannel* ch = channel(unit);
    return ch ? &ch->uart.stream() : nullptr;
}

} // namespace altair
