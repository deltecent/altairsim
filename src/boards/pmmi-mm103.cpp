#include "boards/pmmi-mm103.h"

#include "core/statefile.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <utility>
#include <vector>

namespace altair {
namespace {

PmmiBoard::EndpointResolver g_resolver;

// A card in a backplane always has a clock, but Bus::attach() is public, so a board
// CAN be wired up without a machine around it. A UART with no clock cannot receive or
// time a character; it reads as a dead card rather than dereferencing a null pointer.
Clock& deadCard() {
    static Clock stopped;
    return stopped;
}

// ---------------------------------------------------------------------------
// IN BA+2 -- MODEM STATUS, THE FIXED "READY" STUB (reference §5).
//
// This milestone does not model the 6860 handshake, so the modem-status port returns
// one constant meaning "connected, off-hook, clear to send". The active-low bits are
// the trap here: Dial Tone, Ringing, CTS and Answer Phone all read 0 when ASSERTED.
//
//   bit 0  Dial Tone   = 1  (active low: no dial tone -- we are past dialing)
//   bit 1  Ringing     = 1  (active low: not ringing)
//   bit 2  CTS         = 0  (active low: CLEAR to send)
//   bit 3  Rx Break    = 0  (active high: no break received)
//   bit 4  Answer Phone= 0  (active low: OFF-HOOK, modem holding the line)
//   bit 5  Digital FO  = 0  (diagnostics only)
//   bit 6  Mode        = 1  (active high: originate)
//   bit 7  Timer Pulses= 0
//
// A later milestone replaces this constant with the real SH/RI/DTR + carrier state
// machine. See docs/boards/pmmi-mm103.md.
constexpr uint8_t kModemStatusReady = 0x43;

// The bits we decode back out of that byte for the SHOW `lines` string.
constexpr uint8_t kMsCts = 0x04;  // active low: 0 = clear to send
constexpr uint8_t kMsAp  = 0x10;  // active low: 0 = off-hook

// OUT BA+0 modem-control shadow bits (reference §4).
constexpr uint8_t kSh = 0x01;  // Switch Hook -- 1 = off-hook / originate
constexpr uint8_t kRi = 0x02;  // Ring Indicator -- 1 = answer mode

// OUT BA+3 modem-control shadow bits (reference §4). DTR is unbarred: active high.
constexpr uint8_t kDtr = 0x40;  // 1 = modem enabled
constexpr uint8_t kSt  = 0x10;  // Self Test -- bit 4, ACTIVE LOW (0 = testing)

} // namespace

void PmmiBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

PmmiBoard::PmmiBoard() {
    // -> NullStream. There is no null pointer in the stream path, ever: a card with
    // nothing plugged into it has a DEAD line, not a dangling one.
    u_.disconnect();
}

// ---------------------------------------------------------------------------
// The bus interface -- four consecutive ports, read != write at every one.
// ---------------------------------------------------------------------------

bool PmmiBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    uint8_t p = c.port();
    return p >= base_ && p <= (uint8_t)(base_ + 3);
}

uint8_t PmmiBoard::read(const BusCycle& c) {
    const Clock& clk = clock_ ? *clock_ : deadCard();
    u_.poll(clk);  // the receiver runs on the UART's clock, not on ours

    switch ((c.port() - base_) & 3) {
    case 1:  return u_.readData();   // IN BA+1 -- receive data (clears DAV)
    case 2:  return modemStatus();   // IN BA+2 -- modem status
    case 3:  return 0xFF;            // IN BA+3 -- strobe: nothing drives the bus, floats
                                     // 0xFF (inferred, reference §5). It would latch the
                                     // staged interrupt mask -- inert without interrupts.
    default: return uartStatus();    // IN BA+0 -- UART status
    }
}

void PmmiBoard::write(const BusCycle& c) {
    const Clock& clk = clock_ ? *clock_ : deadCard();
    switch ((c.port() - base_) & 3) {
    case 1:  // OUT BA+1 -- transmit data. The chip's /TDS strobe.
        u_.writeData(c.data, clk);
        break;
    case 2:  // OUT BA+2 -- rate divisor AND interrupt-mask staging (shadowed, inert).
        out2_ = c.data;
        programRate(c.data);
        break;
    case 3:  // OUT BA+3 -- 6860 modem control. Shadowed; the ST bit drives self-test.
        out3_ = c.data;
        updateSelfTest();
        break;
    default: // OUT BA+0 -- UART format / SH,RI / interrupt enable (enable bit inert).
        out0_ = c.data;
        programFrame(c.data);
        break;
    }
}

// ---------------------------------------------------------------------------
// The two status ports.
// ---------------------------------------------------------------------------

// IN BA+0 -- UART status, ALL ACTIVE HIGH (unlike the 88-SIO, which inverts).
uint8_t PmmiBoard::uartStatus() const {
    const Clock& clk = clock_ ? *clock_ : deadCard();
    bool    tbmt = u_.txBufferEmpty(clk);
    uint8_t s    = 0;
    if (tbmt)               s |= 0x01;  // bit 0 TBMT -- transmit buffer empty
    if (u_.dataAvailable()) s |= 0x02;  // bit 1 DAV  -- received char available
    if (tbmt)               s |= 0x04;  // bit 2 TEOC -- serializer done (shares tx deadline)
    // bits 3-5 RPE/OR/FE: the chip hardwires its error flags false -- there is no line to
    // have noise on (uart1602.h) -- so they read 0 here.
    if (u_.parityError())  s |= 0x08;
    if (u_.overrun())      s |= 0x10;
    if (u_.framingError()) s |= 0x20;
    // bits 6-7 Aux In 1/2: the aux-connector inputs, nothing wired -- read 0.
    return s;
}

// IN BA+2 -- modem status. Fixed this milestone (see kModemStatusReady).
uint8_t PmmiBoard::modemStatus() const { return kModemStatusReady; }

// ---------------------------------------------------------------------------
// OUT BA+0 / OUT BA+2 -- the software-programmable frame and baud rate.
// ---------------------------------------------------------------------------

// OUT BA+0 bits 2-6 are the UART word format (reference §4). Push them into the
// chip's straps and reprogram the line, exactly as a real port would be reopened.
void PmmiBoard::programFrame(uint8_t control) {
    u_.dataBits = 5 + ((control >> 2) & 0x03);              // NB1,NB2: 00->5 .. 11->8
    if (control & 0x10)                                     // NP: 1 = no parity
        u_.parity = LineParity::None;
    else
        u_.parity = (control & 0x20) ? LineParity::Even     // EPS: 1 = even
                                     : LineParity::Odd;      //      0 = odd
    u_.stopBits = (control & 0x40) ? 2 : 1;                 // TSB: 1 = 2 stop bits
    u_.programLine();
}

// OUT BA+2 as the rate generator: an 8-bit divisor N. Baud = 250,000 / (16 * N)
// (reference §6). N = 0 is undefined -- the 74193 down-counter wraps to 256 and no
// §8 program writes it -- so we leave the rate unchanged.
void PmmiBoard::programRate(uint8_t divisor) {
    if (divisor == 0) return;
    u_.baud = 250000 / (16LL * divisor);
    u_.programLine();
}

// ---------------------------------------------------------------------------
// SELF TEST -- the 6860 loopback (OUT BA+3 bit 4, ST). See the header.
// ---------------------------------------------------------------------------

// PLUG A LINE INTO THE CONNECTOR. While looped, the live wire on the UART is the
// internal loopback plug and the real line is pocketed -- so a fresh operator line
// replaces the POCKETED one, not the plug, and reappears on the pins when ST clears.
void PmmiBoard::attachStream(std::unique_ptr<ByteStream> s) {
    if (selfTestEngaged())
        savedLine_ = std::move(s);
    else
        u_.connect(std::move(s));
}

// Reconcile the loopback with the shadow. On real hardware ST is active low and only
// means anything with the modem enabled, so the loopback is engaged iff DTR is set and
// ST is asserted. Entering, we pocket the phone line and drop a LoopbackStream on the
// UART's pins (host/stream.h): the guest's TX returns on its RX. The LoopbackStream also
// loops the modem control pins, but that is INERT here -- the PMMI reads a fixed modem
// status stub (modemStatus) and never consults the stream's pins, so self-test on this
// board is a data loopback only. Leaving, we put the phone line back and discard the
// plug. Idempotent: a no-op if already there.
void PmmiBoard::updateSelfTest() {
    bool want = (out3_ & kDtr) && !(out3_ & kSt);
    if (want == selfTestEngaged()) return;
    if (want)
        savedLine_ = u_.swapStream(std::make_unique<LoopbackStream>());
    else
        u_.swapStream(std::move(savedLine_));  // plug returned and dropped; savedLine_ -> null
}

// ---------------------------------------------------------------------------
// Lifecycle.
// ---------------------------------------------------------------------------

void PmmiBoard::reset(Reset) {
    if (!clock_) return;
    u_.masterReset(*clock_);  // MR: clears the UART's receiver/flags, keeps the line

    // The write-only control registers clear on power-on-clear. The endpoint STAYS
    // CONNECTED (masterReset is careful about that) -- a warm reset does not unplug the
    // line.
    out0_ = out2_ = out3_ = 0;

    // DTR is now clear, so any self-test loopback tears down here and the pocketed
    // phone line comes back onto the pins -- the shadow and the live wire stay in step.
    updateSelfTest();
}

void PmmiBoard::power() { reset(Reset::PowerOn); }

// THE ONE DOOR THE OUTSIDE WORLD COMES THROUGH (DESIGN.md 7.1).
void PmmiBoard::pump() { u_.pump(); }

void PmmiBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    u_.serialize(w);
    w.u8(out0_);
    w.u8(out2_);
    w.u8(out3_);
}

void PmmiBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    u_.deserialize(r);
    out0_ = r.u8();
    out2_ = r.u8();
    out3_ = r.u8();

    // Re-derive the loopback from the restored control latch, exactly as the 6860 would
    // come out of restore in the self-test state its ST bit says. RESTORE runs on the
    // already-connected machine, so u_.stream() here is the real line -- the one this
    // pockets. (savedLine_ is a synthesized plug and never travels.)
    updateSelfTest();
}

// ---------------------------------------------------------------------------
// SHOW -- the read-only status strings. Capitals = asserted, the Mc6850 `lines` idiom.
// ---------------------------------------------------------------------------

std::string PmmiBoard::frameString() const {
    char p = (u_.parity == LineParity::Odd)    ? 'O'
             : (u_.parity == LineParity::Even) ? 'E'
                                               : 'N';
    return std::to_string(u_.dataBits) + p + std::to_string(u_.stopBits);
}

std::string PmmiBoard::uartString() const {
    const Clock& clk = clock_ ? *clock_ : deadCard();
    std::string  s;
    s += u_.txBufferEmpty(clk) ? "TBMT " : "tbmt ";
    s += u_.dataAvailable() ? "DAV" : "dav";
    return s;
}

std::string PmmiBoard::linesString() const {
    uint8_t     ms = modemStatus();
    std::string s;
    s += (out0_ & kSh) ? "SH " : "sh ";     // switch hook -- off-hook / originate
    s += (out0_ & kRi) ? "RI " : "ri ";     // ring indicator -- answer mode
    s += (out3_ & kDtr) ? "DTR " : "dtr ";  // data terminal ready -- modem enabled
    s += selfTestEngaged() ? "ST " : "st "; // 6860 self-test -- the line looped on itself
    s += (ms & kMsCts) ? "cts " : "CTS ";   // active low: 0 = clear to send
    s += (ms & kMsAp) ? "ap" : "AP";        // active low: 0 = off-hook
    return s;
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------

std::vector<Property> PmmiBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Base address -- the 6-position DIP. Four ports at BASE..BASE+3; "
                  "MUST be on a 4-port boundary. Default C0 (North Star alternative E0)";
        x.kind  = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xFC;
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string& err) {
            // The board occupies four ports and the decode uses A0-A1 to pick the
            // register, so the base must be a multiple of four -- an off-boundary base
            // is not a card you could strap.
            if (v.i() & 3) {
                err = "the PMMI occupies BASE..BASE+3 -- the base must be a multiple of 4";
                return false;
            }
            base_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "connect";
        x.help = "The endpoint on the phone line (CONNECT sets this). in:/out: a file, ...";
        x.kind = Kind::Str;
        // Report the REAL line even mid-self-test (lineStream), never the internal plug.
        x.get  = [this] { return Value::ofStr(lineStream().describe()); };
        // Route through connect() so a declarative `[pmmi0.unit.line] connect = "out:x"`
        // rebases its PATH the same way the CONNECT command does -- one path, one rule.
        x.set  = [this](const Value& v, std::string& err) { return connect("line", v.s(), err); };
        p.push_back(std::move(x));
    }
    // ---- READ-ONLY STATUS. A property with no setter is a PIN, not a jumper: SHOW
    // renders it `(read-only)`. This is the whole of `SHOW <id>`'s status view. ----
    {
        Property x;
        x.name = "frame";
        x.help = "Live UART frame (read-only), e.g. 8N1. Set by OUT BA+0 bits 2-6";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(frameString()); };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "baud";
        x.help  = "Live line rate (read-only), 250000/(16*N) from the OUT BA+2 divisor";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.unit  = "baud";
        x.get   = [this] { return Value::ofInt(u_.baud); };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "uart";
        x.help = "Live UART status (read-only). CAPITALS = asserted: TBMT DAV";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(uartString()); };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "lines";
        x.help = "Live modem lines (read-only). CAPITALS = asserted: SH RI DTR CTS AP "
                 "(CTS/AP are the fixed stub until the handshake lands)";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(linesString()); };
        p.push_back(std::move(x));
    }
    return p;
}

// One serial unit -- the phone line. The unit exists because CONNECT names one. The
// state is the REAL line's, so a transient self-test loopback does not rewrite what
// SHOW and CONFIG SAVE report the operator connected.
std::vector<UnitDef> PmmiBoard::units() const {
    std::string ep = savedLine_ ? savedLine_->describe() : u_.endpoint();
    return {{"line", UnitKind::Serial, ep}};
}

std::vector<MapEntry> PmmiBoard::ioMap() const {
    return {
        {(uint32_t)base_, (uint32_t)base_, "read/write",
         "PMMI -- UART status (R) / format + SH,RI + int-enable (W)"},
        {(uint32_t)base_ + 1, (uint32_t)base_ + 1, "read/write",
         "PMMI -- receive data (R) / transmit data (W)"},
        {(uint32_t)base_ + 2, (uint32_t)base_ + 2, "read/write",
         "PMMI -- modem status (R) / rate generator + int-mask staging (W)"},
        {(uint32_t)base_ + 3, (uint32_t)base_ + 3, "read/write",
         "PMMI -- strobe, floats 0xFF (R) / 6860 modem control (W)"},
    };
}

bool PmmiBoard::connect(const std::string& unit, const std::string& ep, std::string& err) {
    if (unit != "line") {
        err = "pmmi has no unit '" + unit + "' -- it has one, and it is called 'line'";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    // A machine-file in:/out: PATH is relative to the machine file; rebase the copy the
    // resolver opens. The `connect` unit property routes here too, so both the CONNECT
    // command and a declarative connect are covered.
    std::vector<std::string> paths;
    std::string              spec = rebaseEndpointPaths(ep, [&](const std::string& p) {
        paths.push_back(p);
        return resolvePath(p);
    });
    auto s = g_resolver(spec, err);
    if (!s) {
        for (const std::string& p : paths) err += pathNote(p);
        return false;
    }
    attachStream(std::move(s));
    return true;
}

bool PmmiBoard::disconnect(const std::string& unit, std::string& err) {
    if (unit != "line") {
        err = "pmmi has no unit '" + unit + "' -- it has one, and it is called 'line'";
        return false;
    }
    attachStream(std::make_unique<NullStream>());
    return true;
}

} // namespace altair
