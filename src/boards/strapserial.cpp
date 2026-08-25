#include "boards/strapserial.h"

#include "core/statefile.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <utility>
#include <vector>

namespace altair {

// WHERE THE ENDPOINT GRAMMAR STOPS. main.cpp installs this; the card holds it and
// hands a spec to it. The card never learns what a socket is (DESIGN.md 7.7). Io4Board
// and PropIoBoard inherit the static and share this one resolver.
namespace {
StrapSerialBoard::EndpointResolver g_resolver;
} // namespace

void StrapSerialBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

// ---------------------------------------------------------------------------
// THE BUILT-IN PROFILES. One entry = one card a channel can select by name.
//
// A profile is JUST the strap bundle -- where the two ports sit, which status bits
// carry DAV/TBMT, and whether the inverter gate is engaged. A strap-serial channel's
// jumpers let it imitate each of these; the first, `sior0`, is the MITS-SIO-Rev-0
// personality and the default.
//
// TO ADD A CARD: add a struct to this vector. Its `name` becomes a `profile` choice,
// appears in SHOW, tab-completion and the generated reference, and needs no other edit.
// ---------------------------------------------------------------------------
const std::vector<SerialBuiltin>& serialBuiltins() {
    static const std::vector<SerialBuiltin> kBuiltins = {
        // MITS SIO Rev 0 (AY-5-1013) -- the DEFAULT. Status/data at BASE+0/BASE+1; DAV is
        // status bit 0, TBMT is bit 7, both routed through the inverter gate (asserted reads
        // 0, i.e. active low). This is exactly what the SSM 8080 System Monitor expects on
        // its console (roms/SSM-8080MON/SSM_8080MonV10.asm: spins while D0=1 waiting for a
        // byte, spins while D7=1 waiting to send).
        {"sior0", "MITS SIO Rev 0 (AY-5-1013): status/data at BASE+0/BASE+1, "
                  "DAV=bit0 TBMT=bit7, inverter gate on (active low). The default and the "
                  "SSM 8080 monitor console",
         SerialStraps{/*status*/ 0x00, /*data*/ 0x01, /*dav*/ 0, /*tbmt*/ 7, /*inverterGate*/ true}},

        // Cromemco TU-ART (TMS 5501): status read and data at consecutive ports; DAV
        // is status bit 6 (RBL, receive buffer loaded), TBMT is bit 7 (SBE, send buffer
        // empty), both active high (inverter gate off). The TMS 5501's command register
        // is a separate write-only port -- and a strap-serial channel discards control
        // writes -- so it collapses cleanly to the status/data pair.
        {"tuart", "Cromemco TU-ART (TMS 5501): status/data at BASE+0/BASE+1, "
                  "DAV=bit6 TBMT=bit7, inverter gate off (active high)",
         SerialStraps{/*status*/ 0x00, /*data*/ 0x01, /*dav*/ 6, /*tbmt*/ 7, /*inverterGate*/ false}},

        // IMSAI SIO-2, channel A (Intel 8251): data below status -- data at BASE+0,
        // status at BASE+1 (here the conventional 0x02/0x03). The 8251 status register
        // reads RxRDY in bit 1 and TxRDY in bit 0, both active high.
        {"imsai-sio2", "IMSAI SIO-2 ch A (Intel 8251): data/status at BASE+2/BASE+3, "
                       "DAV=bit1 TBMT=bit0, inverter gate off (active high)",
         SerialStraps{/*status*/ 0x03, /*data*/ 0x02, /*dav*/ 1, /*tbmt*/ 0, /*inverterGate*/ false}},

        // CompuPro Interfacer II (1602/1863 UART): data at BASE+0, status at BASE+1.
        // The status register reads TBMT (transmitter buffer empty) in bit 0 and DAV
        // (data available) in bit 1, both active high. Baud and framing are hardware
        // straps on the real board; the control-port write is discarded here as on any
        // profile. Conventional base here is 0x00/0x01 (the manual's worked example).
        {"compupro-if2", "CompuPro Interfacer II (1602/1863 UART): data/status at "
                         "BASE+0/BASE+1, DAV=bit1 TBMT=bit0, inverter gate off (active high)",
         SerialStraps{/*status*/ 0x01, /*data*/ 0x00, /*dav*/ 1, /*tbmt*/ 0, /*inverterGate*/ false}},

        // CompuPro System Support 1 (Signetics 2651 USART): the 2651 is a four-port chip
        // (data/status/mode/command); a strap-serial channel models only the data+status
        // pair, so its mode and command ports fall to unclaimed I/O -- harmless for a polled
        // console, since those are init-only writes. Status reads TxRDY in bit 0 and RxRDY in
        // bit 1, both active high -- the same logical shape as the Interfacer II. Default base
        // is SS-1's documented standard block, data at 0x5C and status at 0x5D.
        {"compupro-ss1", "CompuPro System Support 1 (2651 USART): data/status at "
                         "BASE+0/BASE+1 (default 5C/5D), DAV=bit1 TBMT=bit0, inverter gate "
                         "off (active high); the 2651 mode/command ports are not modeled",
         SerialStraps{/*status*/ 0x5D, /*data*/ 0x5C, /*dav*/ 1, /*tbmt*/ 0, /*inverterGate*/ false}},
    };
    return kBuiltins;
}

// ---------------------------------------------------------------------------
// StrapSerialBoard
// ---------------------------------------------------------------------------

void StrapSerialBoard::addChannel(const std::string& name, const SerialStraps& straps,
                                  const std::string& profileName) {
    StrapSerialChannel ch;
    ch.name    = name;
    ch.straps  = straps;
    ch.profile = profileName;
    // -> NullStream. There is no null pointer in the stream path, ever: a channel with
    // nothing plugged into it has a DEAD line (TBMT set, DAV clear), not a dangling one.
    ch.stream = std::make_unique<NullStream>();
    chans_.push_back(std::move(ch));
}

void StrapSerialBoard::applyProfile(StrapSerialChannel& ch, const SerialStraps& p) {
    ch.straps = p;
}

int StrapSerialBoard::channelIndex(const std::string& name) const {
    for (size_t i = 0; i < chans_.size(); ++i)
        if (chans_[i].name == name) return (int)i;
    return -1;
}

// The first channel whose status or data port matches. Deterministic on a (mis-strapped)
// overlap -- the earlier channel wins -- rather than double-decoding or fighting the bus.
int StrapSerialBoard::channelForPort(uint8_t port) const {
    for (size_t i = 0; i < chans_.size(); ++i)
        if (chans_[i].straps.statusPort == port || chans_[i].straps.dataPort == port)
            return (int)i;
    return -1;
}

// Each channel owns two ports, and only two: its status/control port and its data port.
// Everything else on the bus is somebody else's.
bool StrapSerialBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    return channelForPort(c.port()) >= 0;
}

// SYNTHESIZE A CHANNEL'S STATUS BYTE. Start at 0, set the DAV bit from readable() and the
// TBMT bit from writable(), each XORed against the inverter gate.
//
// "true = asserted" is the stream's contract (host/stream.h); the inverter gate is a fact
// about the card's pins (both status bits share one inverting buffer), applied here and
// nowhere else. A bit reads 1 when its signal is asserted UNLESS the gate is engaged, in
// which case asserted reads 0.
uint8_t StrapSerialBoard::statusByte(const StrapSerialChannel& ch) const {
    uint8_t s = 0;
    if (ch.stream->readable() != ch.straps.inverterGate) s |= (uint8_t)(1u << ch.straps.davBit);
    if (ch.stream->writable() != ch.straps.inverterGate) s |= (uint8_t)(1u << ch.straps.tbmtBit);
    return s;
}

uint8_t StrapSerialBoard::read(const BusCycle& c) {
    int idx = channelForPort(c.port());
    if (idx < 0) return 0xFF;  // not ours (decodes() gates this, but be defensive)
    StrapSerialChannel& ch = chans_[(size_t)idx];
    if (c.port() == ch.straps.dataPort) {
        // Take a byte off the line. A quiet line reads 0 and is NOT counted -- rxBytes()
        // is bytes actually delivered to the guest, the fact the run loop sums to know a
        // transfer is arriving somewhere (board.h).
        uint8_t b = 0;
        if (ch.stream->read(&b, 1) == 1) ++ch.rxBytes;
        return b;
    }
    // The status/control port. A read is the synthesized status byte; the data port took
    // the branch above, so anything reaching here is the status port.
    return statusByte(ch);
}

void StrapSerialBoard::write(const BusCycle& c) {
    int idx = channelForPort(c.port());
    if (idx < 0) return;
    StrapSerialChannel& ch = chans_[(size_t)idx];
    if (c.port() == ch.straps.dataPort) {
        ch.stream->writeByte(c.data);  // transmit is immediate -- no baud gate on the emulated line
        return;
    }
    // OUT to the status/control port: ACCEPTED AND DISCARDED. There is no chip and so
    // no control register; the port exists only so a guest that writes it is not an
    // unhandled bus cycle.
}

void StrapSerialBoard::pump() {
    for (auto& ch : chans_) ch.stream->pump();
}

// Push a channel's strap + a fixed 8N1 frame at the wire. Only a real serial port honors
// it (and only it can refuse); null/socket/file/loopback ignore it. The refusal is a fact
// about the world -- an FTDI cable that cannot do the rate -- so it is said out loud rather
// than run at the wrong speed in silence (the 6850's discipline, mc6850.cpp).
void StrapSerialBoard::programLine(StrapSerialChannel& ch) {
    LineParams p;
    p.baud     = ch.baud;
    p.dataBits = 8;
    p.stopBits = 1;
    p.parity   = LineParity::None;
    std::string err;
    if (!ch.stream->setParams(p, err)) log_.push_back(id + ":" + ch.name + " " + err);
}

// A jumper moved: a channel's status/data port may have moved it in the I/O space, and
// `baud` may have restrapped a real serial line. No timers to re-aim -- the cards are
// polled and transmit is immediate. Also flag a mis-strap that lands two channels on the
// same port (the earlier channel would shadow the later one -- see channelForPort()).
void StrapSerialBoard::configChanged() {
    decodeChanged();
    for (auto& ch : chans_) programLine(ch);
    for (size_t i = 0; i < chans_.size(); ++i)
        for (size_t j = i + 1; j < chans_.size(); ++j) {
            const auto& a = chans_[i].straps;
            const auto& b = chans_[j].straps;
            bool clash = a.statusPort == b.statusPort || a.statusPort == b.dataPort ||
                         a.dataPort == b.statusPort || a.dataPort == b.dataPort;
            if (clash)
                log_.push_back(id + ": channels '" + chans_[i].name + "' and '" +
                               chans_[j].name + "' overlap in the I/O map -- '" +
                               chans_[j].name + "' is shadowed");
        }
}

std::vector<std::string> StrapSerialBoard::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    for (auto& ch : chans_)
        for (auto& s : ch.stream->drainLog()) out.push_back(id + ":" + ch.name + " " + std::move(s));
    return out;
}

uint64_t StrapSerialBoard::rxBytes() const {
    uint64_t n = 0;
    for (const auto& ch : chans_) n += ch.rxBytes;
    return n;
}

void StrapSerialBoard::serialize(StateWriter& w) const {
    Board::serialize(w);  // enabled_
    for (const auto& ch : chans_) w.u64(ch.rxBytes);
}

void StrapSerialBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    for (auto& ch : chans_) ch.rxBytes = r.u64();
    // The straps are config (re-applied from TOML) and the streams are host handles
    // (re-resolved from the `connect` property). Neither travels -- nothing else to do.
}

// ---------------------------------------------------------------------------
// Reflection -- the straps, the profile selector, and the endpoint, PER CHANNEL.
// ---------------------------------------------------------------------------

// The properties for ONE channel. Emitted board-level (single channel, propio) or per-unit
// (multi channel, io4); the names are identical either way because the unit namespaces them.
std::vector<Property> StrapSerialBoard::channelProperties(size_t idx) {
    std::vector<Property> p;

    // THE PROFILE SELECTOR. Its choices are `custom` plus every built-in name. Choosing a
    // built-in copies its straps into the live fields; choosing `custom` leaves them as they
    // are. The individual straps below are still settable AFTER a profile is chosen -- CONFIG
    // SAVE writes `profile` first (it is first here), so a saved `profile=sior0` + an
    // overridden `status_port` reload in the right order.
    {
        Property x;
        x.name    = "profile";
        x.help    = "Built-in card to preset the straps from: custom, or a named board. "
                    "Selecting one sets status_port/data_port/bits/inverter_gate (still overridable)";
        x.kind    = Kind::Enum;
        x.choices = {"custom"};
        for (const auto& b : serialBuiltins()) x.choices.push_back(b.name);
        x.get     = [this, idx] { return Value::ofStr(chans_[idx].profile); };
        x.set     = [this, idx](const Value& v, std::string&) {
            chans_[idx].profile = v.s();
            for (const auto& b : serialBuiltins())
                if (b.name == chans_[idx].profile) { applyProfile(chans_[idx], b.profile); break; }
            return true;
        };
        p.push_back(std::move(x));
    }
    // The status/control port. Read = status byte; write = discarded.
    {
        Property x;
        x.name  = "status_port";
        x.help  = "Status(read)/control(write) port. Control writes are discarded";
        x.kind  = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xFF;
        x.get   = [this, idx] { return Value::ofInt(chans_[idx].straps.statusPort); };
        x.set   = [this, idx](const Value& v, std::string&) {
            chans_[idx].straps.statusPort = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    // The data port. Read = receive byte; write = transmit byte.
    {
        Property x;
        x.name  = "data_port";
        x.help  = "Data port: receive(read)/transmit(write)";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xFF;
        x.get   = [this, idx] { return Value::ofInt(chans_[idx].straps.dataPort); };
        x.set   = [this, idx](const Value& v, std::string&) {
            chans_[idx].straps.dataPort = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    // The two status-bit positions (0-7), named for the UART's signals.
    {
        Property x;
        x.name = "dav";
        x.help = "Status bit (0-7) carrying DAV, data available (a byte to receive)";
        x.kind = Kind::Int;
        x.min  = 0;
        x.max  = 7;
        x.get  = [this, idx] { return Value::ofInt(chans_[idx].straps.davBit); };
        x.set  = [this, idx](const Value& v, std::string&) {
            chans_[idx].straps.davBit = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "tbmt";
        x.help = "Status bit (0-7) carrying TBMT, transmit buffer empty (ready to send)";
        x.kind = Kind::Int;
        x.min  = 0;
        x.max  = 7;
        x.get  = [this, idx] { return Value::ofInt(chans_[idx].straps.tbmtBit); };
        x.set  = [this, idx](const Value& v, std::string&) {
            chans_[idx].straps.tbmtBit = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    // The inverter gate -- ONE knob for both status bits. They share a single inverting
    // buffer, so there is no way to invert one without the other.
    {
        Property x;
        x.name = "inverter_gate";
        x.help = "Route both status bits through the inverter gate -- asserted DAV/TBMT "
                 "read 0 (active low)";
        x.kind = Kind::Bool;
        x.get  = [this, idx] { return Value::ofBool(chans_[idx].straps.inverterGate); };
        x.set  = [this, idx](const Value& v, std::string&) {
            chans_[idx].straps.inverterGate = v.b();
            return true;
        };
        p.push_back(std::move(x));
    }
    // The line rate PROGRAMMED ONTO A REAL SERIAL PORT. The line is always 8N1. It does
    // NOT pace the emulated transmitter (which is immediate) -- see stream.h LineParams:
    // a second, independent baud rate could only ever configure a mismatch. On a socket
    // or a file it is inert.
    {
        Property x;
        x.name  = "baud";
        x.help  = "Line rate programmed onto a CONNECTed real serial port (8N1). "
                  "Inert on a socket/file; does not pace the emulated line";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.unit  = "baud";
        x.min   = 0;
        x.max   = 0;  // unbounded -- the port itself refuses a rate it cannot do
        x.get   = [this, idx] { return Value::ofInt(chans_[idx].baud); };
        x.set   = [this, idx](const Value& v, std::string&) {
            chans_[idx].baud = v.i();
            programLine(chans_[idx]);  // restrap a connected real port at the new rate
            return true;
        };
        p.push_back(std::move(x));
    }
    // THE ENDPOINT. CONNECT sets this; SHOW/CONFIG SAVE read it back. Routing a
    // declarative `connect = "out:x"` through connect() rebases its PATH exactly as the
    // CONNECT command does -- one path, one rule.
    {
        Property x;
        x.name = "connect";
        x.help = "The endpoint on the serial line (CONNECT sets this): a file, socket, "
                 "serial port, in:/out: file, null, loopback";
        x.kind = Kind::Str;
        x.get  = [this, idx] { return Value::ofStr(chans_[idx].stream->describe()); };
        x.set  = [this, idx](const Value& v, std::string& err) {
            return connect(chans_[idx].name, v.s(), err);
        };
        p.push_back(std::move(x));
    }
    return p;
}

// Single-channel boards (propio) surface the straps at BOARD level; multi-channel (io4)
// surface them per-unit (unitProperties), so here properties() is empty for them.
std::vector<Property> StrapSerialBoard::properties() {
    if (boardLevelProps_ && !chans_.empty()) return channelProperties(0);
    return {};
}

std::vector<Property> StrapSerialBoard::unitProperties(const std::string& unit) {
    if (boardLevelProps_) return {};
    int idx = channelIndex(unit);
    if (idx < 0) return {};
    return channelProperties((size_t)idx);
}

std::vector<UnitDef> StrapSerialBoard::units() const {
    std::vector<UnitDef> u;
    for (const auto& ch : chans_)
        u.push_back({ch.name, UnitKind::Serial, ch.stream->describe()});
    return u;
}

std::vector<MapEntry> StrapSerialBoard::ioMap() const {
    std::vector<MapEntry> m;
    for (const auto& ch : chans_) {
        std::string tag = chans_.size() > 1 ? (" " + ch.name) : "";
        m.push_back({(uint32_t)ch.straps.statusPort, (uint32_t)ch.straps.statusPort, "read/write",
                     "serial" + tag + " -- status (R) / control, discarded (W)"});
        m.push_back({(uint32_t)ch.straps.dataPort, (uint32_t)ch.straps.dataPort, "read/write",
                     "serial" + tag + " -- receive data (R) / transmit data (W)"});
    }
    return m;
}

bool StrapSerialBoard::connect(const std::string& unit, const std::string& endpoint,
                               std::string& err) {
    int idx = channelIndex(unit);
    if (idx < 0) {
        err = type() + " has no unit '" + unit + "'";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    // A machine-file in:/out: PATH is relative to the machine file; rebase the copy the
    // resolver opens (rebaseEndpointPaths knows the grammar). describe() still echoes the
    // operator's original spec, so we rebase only the resolver's copy.
    std::vector<std::string> paths;
    std::string              spec = rebaseEndpointPaths(endpoint, [&](const std::string& pth) {
        paths.push_back(pth);
        return resolvePath(pth);
    });
    auto s = g_resolver(spec, err);
    if (!s) {
        for (const std::string& pth : paths) err += pathNote(pth);
        return false;
    }
    chans_[(size_t)idx].stream = std::move(s);
    programLine(chans_[(size_t)idx]);  // a fresh line comes up at the channel's current strap
    return true;
}

// Install an ALREADY-BUILT stream (the MCP console's filtered scripted line), bypassing the
// resolver -- the caller resolved and wrapped it. Otherwise identical to connect(): hand it
// to the named channel and re-strap the line at the channel's current baud.
bool StrapSerialBoard::connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                                     std::string& err) {
    int idx = channelIndex(unit);
    if (idx < 0) {
        err = type() + " has no unit '" + unit + "'";
        return false;
    }
    chans_[(size_t)idx].stream = std::move(s);
    programLine(chans_[(size_t)idx]);
    return true;
}

bool StrapSerialBoard::disconnect(const std::string& unit, std::string& err) {
    int idx = channelIndex(unit);
    if (idx < 0) {
        err = type() + " has no unit '" + unit + "'";
        return false;
    }
    chans_[(size_t)idx].stream = std::make_unique<NullStream>();
    return true;
}

ByteStream* StrapSerialBoard::unitStream(const std::string& unit) {
    int idx = channelIndex(unit);
    return idx < 0 ? nullptr : chans_[(size_t)idx].stream.get();
}

} // namespace altair
