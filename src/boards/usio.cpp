#include "boards/usio.h"

#include "core/statefile.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <utility>
#include <vector>

namespace altair {

// WHERE THE ENDPOINT GRAMMAR STOPS. main.cpp installs this; the card holds it and
// hands a spec to it. The card never learns what a socket is (DESIGN.md 7.7).
namespace {
UsioBoard::EndpointResolver g_resolver;
} // namespace

void UsioBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

// ---------------------------------------------------------------------------
// THE BUILT-IN PROFILES. One entry = one card the operator can select by name.
//
// A profile is JUST the strap bundle -- where the two ports sit and which status
// bits carry RDR/TDRE -- extracted from each card's manual (reference/Cromemco
// TU-ART.md, reference/IMSAI SIO-2.md). Neither card has a factory-default base;
// the base here is the conventional one and the operator overrides `status_port` /
// `data_port` freely.
//
// TO ADD A CARD: add a struct to this vector. Its `name` becomes a `profile` choice,
// appears in SHOW, tab-completion and the generated reference, and needs no other edit.
// ---------------------------------------------------------------------------
const std::vector<UsioBuiltin>& usioBuiltins() {
    static const std::vector<UsioBuiltin> kBuiltins = {
        // Cromemco TU-ART (TMS 5501): status read and data at consecutive ports; RDR
        // is status bit 6 (RBL, receive buffer loaded), TDRE is bit 7 (SBE, send buffer
        // empty), both active high. The TMS 5501's command register is a separate
        // write-only port -- and USIO discards control writes -- so it collapses cleanly
        // to the status/data pair.
        {"tuart", "Cromemco TU-ART (TMS 5501): status/data at BASE+0/BASE+1, "
                  "RDR=bit6 TDRE=bit7, both active high",
         UsioProfile{/*status*/ 0x00, /*data*/ 0x01, /*rdr*/ 6, /*tdre*/ 7, false, false}},

        // IMSAI SIO-2, channel A (Intel 8251): data below status -- data at BASE+0,
        // status at BASE+1 (here the conventional 0x02/0x03). The 8251 status register
        // reads RxRDY in bit 1 and TxRDY in bit 0, both active high.
        {"imsai-sio2", "IMSAI SIO-2 ch A (Intel 8251): data/status at BASE+2/BASE+3, "
                       "RDR=bit1 TDRE=bit0, both active high",
         UsioProfile{/*status*/ 0x03, /*data*/ 0x02, /*rdr*/ 1, /*tdre*/ 0, false, false}},

        // CompuPro Interfacer II (1602/1863 UART): data at BASE+0, status at BASE+1.
        // The status register reads TBMT (transmitter buffer empty) in bit 0 and DAV
        // (data available) in bit 1, both active high. Baud and framing are hardware
        // straps on the real board; the control-port write is discarded here as on any
        // profile. Conventional base here is 0x00/0x01 (the manual's worked example).
        {"compupro-if2", "CompuPro Interfacer II (1602/1863 UART): data/status at "
                         "BASE+0/BASE+1, RDR=bit1 TDRE=bit0, both active high",
         UsioProfile{/*status*/ 0x01, /*data*/ 0x00, /*rdr*/ 1, /*tdre*/ 0, false, false}},

        // CompuPro System Support 1 (Signetics 2651 USART): the 2651 is a four-port chip
        // (data/status/mode/command); USIO models only the data+status pair, so its mode
        // and command ports fall to unclaimed I/O -- harmless for a polled console, since
        // those are init-only writes. Status reads TxRDY in bit 0 and RxRDY in bit 1,
        // both active high -- the same logical shape as the Interfacer II. Default base is
        // SS-1's documented standard block, data at 0x5C and status at 0x5D.
        {"compupro-ss1", "CompuPro System Support 1 (2651 USART): data/status at "
                         "BASE+0/BASE+1 (default 5C/5D), RDR=bit1 TDRE=bit0, both active "
                         "high; the 2651 mode/command ports are not modeled",
         UsioProfile{/*status*/ 0x5D, /*data*/ 0x5C, /*rdr*/ 1, /*tdre*/ 0, false, false}},
    };
    return kBuiltins;
}

// ---------------------------------------------------------------------------
// UsioBoard
// ---------------------------------------------------------------------------

UsioBoard::UsioBoard() {
    // -> NullStream. There is no null pointer in the stream path, ever: a card with
    // nothing plugged into it has a DEAD line (TDRE set, RDR clear), not a dangling one.
    stream_ = std::make_unique<NullStream>();
}

// Two ports, and only two: the status/control port and the data port. Everything
// else on the bus is somebody else's.
bool UsioBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    uint8_t p = c.port();
    return p == straps_.statusPort || p == straps_.dataPort;
}

// SYNTHESIZE THE STATUS BYTE. Start at 0, set the RDR bit from readable() and the
// TDRE bit from writable(), then invert either bit whose strap says active-low.
//
// "true = asserted" is the stream's contract (host/stream.h); active-low is a fact
// about THIS card's pins, applied here and nowhere else. A bit reads 1 when its
// signal is asserted UNLESS it is active-low, in which case asserted reads 0.
uint8_t UsioBoard::statusByte() const {
    uint8_t s = 0;
    if (stream_->readable() != straps_.rdrActiveLow)  s |= (uint8_t)(1u << straps_.rdrBit);
    if (stream_->writable() != straps_.tdreActiveLow) s |= (uint8_t)(1u << straps_.tdreBit);
    return s;
}

uint8_t UsioBoard::read(const BusCycle& c) {
    if (c.port() == straps_.dataPort) {
        // Take a byte off the line. A quiet line reads 0 and is NOT counted -- rxBytes()
        // is bytes actually delivered to the guest, the fact the run loop sums to know a
        // transfer is arriving somewhere (board.h).
        uint8_t b = 0;
        if (stream_->read(&b, 1) == 1) ++rxBytes_;
        return b;
    }
    // The status/control port. A read is the synthesized status byte; the data port took
    // the branch above, so anything reaching here is the status port.
    return statusByte();
}

void UsioBoard::write(const BusCycle& c) {
    if (c.port() == straps_.dataPort) {
        stream_->writeByte(c.data);  // transmit is immediate -- no baud gate on the emulated line
        return;
    }
    // OUT to the status/control port: ACCEPTED AND DISCARDED. There is no chip and so
    // no control register; the port exists only so a guest that writes it is not an
    // unhandled bus cycle.
}

// Push the strap + a fixed 8N1 frame at the wire. Only a real serial port honors it
// (and only it can refuse); null/socket/file/loopback ignore it. The refusal is a fact
// about the world -- an FTDI cable that cannot do the rate -- so it is said out loud
// rather than run at the wrong speed in silence (the 6850's discipline, mc6850.cpp).
void UsioBoard::programLine() {
    LineParams p;
    p.baud     = baud_;
    p.dataBits = 8;
    p.stopBits = 1;
    p.parity   = LineParity::None;
    std::string err;
    if (!stream_->setParams(p, err)) log_.push_back(id + ": " + err);
}

void UsioBoard::applyProfile(const UsioProfile& p) { straps_ = p; }

// A jumper moved: `status_port`/`data_port` may have moved the card in the I/O space,
// and `baud` may have restrapped a real serial line. No timers to re-aim -- the card is
// polled and its transmit is immediate.
void UsioBoard::configChanged() {
    decodeChanged();
    programLine();
}

std::vector<std::string> UsioBoard::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    for (auto& s : stream_->drainLog()) out.push_back(id + ": " + std::move(s));
    return out;
}

void UsioBoard::serialize(StateWriter& w) const {
    Board::serialize(w);  // enabled_
    w.u64(rxBytes_);
}

void UsioBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    rxBytes_ = r.u64();
    // The straps are config (re-applied from TOML) and the stream is a host handle
    // (re-resolved from the `connect` property). Neither travels -- nothing else to do.
}

// ---------------------------------------------------------------------------
// Reflection -- the straps, the profile selector, and the endpoint.
// ---------------------------------------------------------------------------

std::vector<Property> UsioBoard::properties() {
    std::vector<Property> p;

    // THE PROFILE SELECTOR. Its choices are `custom` plus every built-in name, so
    // `SHOW usio0` and tab-completion list what an operator can select. Choosing a
    // built-in copies its straps into the live fields; choosing `custom` leaves them
    // as they are. The individual straps below are still settable AFTER a profile is
    // chosen -- CONFIG SAVE writes `profile` first (it is first here), so a saved
    // `profile=tuart` + an overridden `status_port` reload in the right order.
    {
        Property x;
        x.name    = "profile";
        x.help    = "Built-in card to preset the straps from: custom, or a named board. "
                    "Selecting one sets status_port/data_port/bits/polarity (still overridable)";
        x.kind    = Kind::Enum;
        x.choices = {"custom"};
        for (const auto& b : usioBuiltins()) x.choices.push_back(b.name);
        x.get     = [this] { return Value::ofStr(profile_); };
        x.set     = [this](const Value& v, std::string&) {
            profile_ = v.s();
            for (const auto& b : usioBuiltins())
                if (b.name == profile_) { applyProfile(b.profile); break; }
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
        x.get   = [this] { return Value::ofInt(straps_.statusPort); };
        x.set   = [this](const Value& v, std::string&) {
            straps_.statusPort = (uint8_t)v.i();
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
        x.get   = [this] { return Value::ofInt(straps_.dataPort); };
        x.set   = [this](const Value& v, std::string&) {
            straps_.dataPort = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    // The two status-bit positions (0-7) and their polarities.
    {
        Property x;
        x.name = "rdr_bit";
        x.help = "Status bit (0-7) that signals receive data ready";
        x.kind = Kind::Int;
        x.min  = 0;
        x.max  = 7;
        x.get  = [this] { return Value::ofInt(straps_.rdrBit); };
        x.set  = [this](const Value& v, std::string&) {
            straps_.rdrBit = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "tdre_bit";
        x.help = "Status bit (0-7) that signals transmit data empty";
        x.kind = Kind::Int;
        x.min  = 0;
        x.max  = 7;
        x.get  = [this] { return Value::ofInt(straps_.tdreBit); };
        x.set  = [this](const Value& v, std::string&) {
            straps_.tdreBit = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "rdr_active_low";
        x.help = "Invert the receive-data-ready bit (asserted reads 0)";
        x.kind = Kind::Bool;
        x.get  = [this] { return Value::ofBool(straps_.rdrActiveLow); };
        x.set  = [this](const Value& v, std::string&) {
            straps_.rdrActiveLow = v.b();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "tdre_active_low";
        x.help = "Invert the transmit-data-empty bit (asserted reads 0)";
        x.kind = Kind::Bool;
        x.get  = [this] { return Value::ofBool(straps_.tdreActiveLow); };
        x.set  = [this](const Value& v, std::string&) {
            straps_.tdreActiveLow = v.b();
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
        x.get   = [this] { return Value::ofInt(baud_); };
        x.set   = [this](const Value& v, std::string&) {
            baud_ = v.i();
            programLine();  // restrap a connected real port at the new rate
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
        x.get  = [this] { return Value::ofStr(stream_->describe()); };
        x.set  = [this](const Value& v, std::string& err) { return connect("serial", v.s(), err); };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<UnitDef> UsioBoard::units() const {
    return {{"serial", UnitKind::Serial, stream_->describe()}};
}

std::vector<MapEntry> UsioBoard::ioMap() const {
    std::vector<MapEntry> m;
    m.push_back({(uint32_t)straps_.statusPort, (uint32_t)straps_.statusPort, "read/write",
                 "USIO -- status (R) / control, discarded (W)"});
    m.push_back({(uint32_t)straps_.dataPort, (uint32_t)straps_.dataPort, "read/write",
                 "USIO -- receive data (R) / transmit data (W)"});
    return m;
}

bool UsioBoard::connect(const std::string& unit, const std::string& endpoint, std::string& err) {
    if (unit != "serial") {
        err = "usio has no unit '" + unit + "' -- it has one, and it is called 'serial'";
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
    stream_ = std::move(s);
    programLine();  // a fresh line comes up at the card's current strap (baud/8N1)
    return true;
}

bool UsioBoard::disconnect(const std::string& unit, std::string& err) {
    if (unit != "serial") {
        err = "usio has no unit '" + unit + "' -- it has one, and it is called 'serial'";
        return false;
    }
    stream_ = std::make_unique<NullStream>();
    return true;
}

} // namespace altair
