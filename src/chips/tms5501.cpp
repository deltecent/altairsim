#include "chips/tms5501.h"

#include "core/statefile.h"
#include "host/stream.h"

#include <utility>

namespace altair {

// ---------------------------------------------------------------------------
// TMS 5501 register bits. STATUS IS ALL ACTIVE-HIGH (reference/Cromemco TU-ART.md
// 3, confirmed by the manual's own polled drivers: char-out spins while D7=0, char-in
// spins while D6=0). TBE = D7, RDA = D6.
// ---------------------------------------------------------------------------
namespace {
// Status register (port 00 IN), all active-high (1 = asserted).
constexpr uint8_t kTbe = 0x80;  // transmitter buffer empty
constexpr uint8_t kRda = 0x40;  // receiver data available
constexpr uint8_t kIpg = 0x20;  // interrupt pending (mirrors the INT pin)
constexpr uint8_t kSbd = 0x10;  // start-bit detect (diagnostic)
constexpr uint8_t kFbd = 0x08;  // full-bit detect (diagnostic)
constexpr uint8_t kSrv = 0x04;  // serial receive: the live line level (high = mark/idle)
constexpr uint8_t kOre = 0x02;  // overrun error
constexpr uint8_t kFme = 0x01;  // frame error

// Command register (port 02 OUT).
constexpr uint8_t kRes = 0x01;  // D0: reset (a strobe, not latched)
constexpr uint8_t kBrk = 0x02;  // D1: transmit break
constexpr uint8_t kRs7 = 0x04;  // D2: RST7-select routing (inert, Phase 1)
constexpr uint8_t kIne = 0x08;  // D3: interrupt-acknowledge enable (inert, Phase 1)
constexpr uint8_t kHbd = 0x10;  // D4: high baud (octuple the rate)
constexpr uint8_t kTb5 = 0x20;  // D5: test bit (keep low)

// Baud register (port 00 OUT): one-hot in the low seven bits; D7 = stop-bit count.
// If several bits are set the HIGHEST rate wins; if none, the channel is disabled.
// (reference/Cromemco TU-ART.md 4.)
constexpr long long kBaudRates[7] = {110, 150, 300, 1200, 2400, 4800, 9600};  // D0..D6

// Interrupt-mask / interrupt-address bit order, highest priority first, per
// reference/Cromemco TU-ART.md 7: [T5/PI7 | T4 | TBE | RDA | T3 | SENS | T2 | T1] from
// D7..D0. So bit 0 (=Timer 1) is the highest priority and vectors to RST 0 (0xC7); bit i
// vectors to RST i (0xC7 + 8*i). The five interval timers land on these bits:
constexpr int kTimerBit[5] = {0, 1, 3, 6, 7};  // Timer 1..5 -> mask/priority bit

// The 5501's interval timers tick at 64 us/count (reference chip clock ~2 MHz, /128);
// the command register's HBD (D4) octuples that to 8 us. Expressed as a rate so the
// deadline is derived in WALL time via Clock::tStatesPer -- independent of CPU speed.
constexpr long long kTimerTickHz    = 15625;   // 1 / 64 us
constexpr long long kTimerTickHzHbd = 125000;  // 1 / 8 us  (HBD)

// The reference receive cadence at rate=full: ~9600 baud 8N1 (9600 / 10 bits). Short and
// baud-independent, so a fast programmed rate is unchanged while a slow one (a 300-baud
// console) is not dragged down to its own character time on receive. See rxGapTStates.
constexpr long long kFullReceiveHz  = 960;
} // namespace

// ---------------------------------------------------------------------------
uint64_t Tms5501::charTStates(const Clock& clk) const {
    if (!paceReal_) return 0;  // rate=full: the line does not pace -- TBE/RDA come back at once
    if (baud_ <= 0) return 0;
    return (uint64_t)(clk.hz() * (long long)bitsPerChar() / baud_);
}

// The gap the RECEIVER holds between two delivered characters. Any line that does not clock
// its own wire (ByteStream::pacedReceive, true for everything but a real serial port) keeps
// an emulated receive cadence, so a paste does not arrive as one instantaneous burst -- a
// guest that re-samples the line between characters (CDOS's console read-ahead does, ~3k
// T-states after taking a byte) would otherwise find the next byte already sitting in the
// receiver and lose it. At rate=full the gap is SHORT and baud-independent (kFullReceiveHz),
// measured in guest T-states so it costs no wall-clock time under Clock::free(): far faster
// than a 300-baud console yet longer than the read-ahead window. At rate=real it is the
// authentic programmed-baud character time. Transmit and a real serial port are untouched.
uint64_t Tms5501::rxGapTStates(const Clock& clk) const {
    if (paceReal_) return charTStates(clk);       // rate=real: at the programmed baud
    if (!stream_->pacedReceive()) return 0;       // a real serial port clocks its own wire
    return clk.tStatesPer(kFullReceiveHz);        // rate=full: a short, fixed receive gap
}

LineParams Tms5501::params() const {
    LineParams p;
    p.baud     = baud_ > 0 ? baud_ : 9600;
    p.dataBits = 8;                 // fixed by the chip
    p.parity   = LineParity::None;  // the 5501 has no parity
    p.stopBits = stopBits_;
    return p;
}

void Tms5501::programLine() {
    std::string err;
    if (stream_->setParams(params(), err)) return;
    log_.push_back(name_ + ": " + err);
}

void Tms5501::connect(std::unique_ptr<ByteStream> s) {
    stream_ = std::move(s);
    programLine();
    driveControl();
    ctsPin_ = ctsNow();
    txRoom_ = stream_->writable();
}

void Tms5501::disconnect() { connect(std::make_unique<NullStream>()); }

std::vector<std::string> Tms5501::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    return out;
}

// ---------------------------------------------------------------------------
// Pins, strap applied. `ground` means the pin is tied to its asserted state on the
// card and the far end is not asked -- which is how a console channel is wired.
// ---------------------------------------------------------------------------
bool Tms5501::carrier() const {
    if (dcdStrap == PinStrap::Ground) return true;
    return stream_->status().carrier;
}

bool Tms5501::clearToSend() const { return ctsPin_; }  // the sample, not the pin

bool Tms5501::ctsNow() const {
    if (ctsStrap == PinStrap::Ground) return true;
    return stream_->status().cts;
}

// TBE and the two things that inhibit it: /CTS negated, and the endpoint having
// nowhere to put the byte (a full TCP send buffer is the same situation as a modem
// holding CTS low). The 5501 has no "held in reset" latch, unlike the 6850.
bool Tms5501::tbe(const Clock& clk) const {
    if (clk.now() < txFreeAt_) return false;  // the character has not finished leaving
    if (!ctsPin_) return false;
    if (!txRoom_) return false;
    return true;
}

// The command register's BRK (D1) is the only control pin a console channel drives
// here; RTS/DTR are not wired on the FDC's single channel. BRK is stubbed at the
// wire (setControl carries it) and logged when armed -- see writeCommand.
void Tms5501::driveControl() {
    LineControl c;
    c.rts = true;                        // asserted: a grounded console channel
    c.dtr = false;
    c.brk = (command_ & kBrk) != 0;
    stream_->setControl(c);
}

// Pull a byte off the line, if it has had time to deliver one AND the receive
// register is free. Does NOT synthesize overruns from a flow-controlled ByteStream
// -- see the long note in Mc6850::poll for why that manufactures data loss the host
// transport does not have.
void Tms5501::poll(const Clock& clk) {
    ctsPin_ = ctsNow();
    txRoom_ = stream_->writable();

    if (!carrier()) return;             // no carrier: the receiver is dead
    if (rdrf_) return;                  // register still full: the line waits
    // The emulated receive gate, unless the line paces itself: a tape or a ?cps= paper-tape
    // reader carries its own clock, and gating it too would double-pace it (see Uart1602::poll).
    if (!stream_->pacesItself() && clk.now() < rxNextAt_) return;
    if (!stream_->readable()) return;

    uint8_t b = 0;
    if (stream_->read(&b, 1) != 1) return;

    rxData_   = b;
    rdrf_     = true;
    ++rxCount_;
    rxNextAt_ = clk.now() + rxGapTStates(clk);
}

uint8_t Tms5501::readStatus(const Clock& clk) {
    poll(clk);

    uint8_t s = kSrv;  // the line idles at mark; no framing/noise model (DESIGN.md 0.1)
    if (rdrf_)     s |= kRda;
    if (tbe(clk))  s |= kTbe;
    if (ovrn_)     s |= kOre;
    if (pendingSources(clk) & mask_) s |= kIpg;  // INT pending: an unmasked timer fired
    // kSbd / kFbd / kFme stay 0: a ByteStream delivers a whole byte or none -- there is
    // no partial-frame state and no line noise to report (DESIGN.md 0.1).
    (void)kSbd; (void)kFbd; (void)kFme;
    return s;
}

uint8_t Tms5501::readData(const Clock& clk) {
    poll(clk);
    rdrf_ = false;  // reading the data register clears RDA (and ORE)
    ovrn_ = false;
    return rxData_;
}

// OUT 00: the baud-rate register. One-hot rate in D6..D0 (highest wins), D7 = stop
// bits. Sets the effective line rate (through the HBD multiplier) and reprograms the
// wire. RDOS's "Initialize Baud Rate" command lands here at boot.
void Tms5501::writeBaud(uint8_t v) {
    long long r = 0;
    for (int bit = 6; bit >= 0; --bit) {  // highest set bit wins
        if (v & (1u << bit)) { r = kBaudRates[bit]; break; }
    }
    baudBase_ = r;                                   // 0 => serial disabled
    stopBits_ = (v & 0x80) ? 1 : 2;                  // D7: one vs two stop bits
    baud_     = baudBase_ * (hbd_ ? 8 : 1);
    programLine();
}

void Tms5501::writeData(uint8_t v, const Clock& clk) {
    stream_->write(&v, 1);
    stream_->flush();
    txFreeAt_ = clk.now() + charTStates(clk);  // busy until the character gets out
}

// OUT 02: the command register. HBD (D4) octuples the rate; RES (D0) resets the chip
// (a strobe, not latched -- it does not stay in reset the way a 6850's divide-11
// does). BRK (D1) and TB5 (D5) are stubbed and logged when armed.
void Tms5501::writeCommand(uint8_t v, const Clock& clk) {
    command_ = v;

    bool newHbd = (v & kHbd) != 0;
    if (newHbd != hbd_) {
        hbd_  = newHbd;
        baud_ = baudBase_ * (hbd_ ? 8 : 1);
        programLine();
    }

    if (v & kBrk) log_.push_back(name_ + ": BRK (transmit break) not modeled");
    if (v & kTb5) log_.push_back(name_ + ": TB5 (test mode) not modeled");
    (void)kRs7; (void)kIne;  // routing bits: stored in command_, inert in Phase 1

    driveControl();
    if (v & kRes) resetAction(clk);
}

// OUT 05-09: arm interval timer `idx` (0 = Timer 1). One-shot: it fires `v` counts from
// now, one count = 64 us of WALL time (8 us with HBD) -- the chip's own oscillator, so
// the interval is the same real duration whatever the CPU speed. v == 0 fires at once
// (datasheet: a zero load times out immediately). Re-arming restarts the count.
void Tms5501::writeTimer(int idx, uint8_t v, const Clock& clk) {
    if (idx < 0 || idx >= 5) return;
    long long tickHz  = hbd_ ? kTimerTickHzHbd : kTimerTickHz;
    timerFireAt_[idx] = clk.now() + (uint64_t)v * clk.tStatesPer(tickHz);
    timerArmed_[idx]  = true;
}

// The eight interrupt sources currently pending, before masking, in the priority bit
// order (bit 0 = Timer 1). Only the interval timers latch in Phase 1: a timer is pending
// once armed and now() has reached its deadline. The non-timer bits (RDA/TBE serial,
// SENS/disk) stay 0 -- those sources are not wired to the interrupt controller yet.
uint8_t Tms5501::pendingSources(const Clock& clk) const {
    uint8_t p = 0;
    for (int i = 0; i < 5; ++i)
        if (timerArmed_[i] && clk.now() >= timerFireAt_[i]) p |= (uint8_t)(1u << kTimerBit[i]);
    return p;
}

// IN 03: the interrupt-address register. Returns the RST opcode of the highest-priority
// source that is both pending and unmasked (bit 0 = Timer 1 = 0xC7, highest; bit 7 =
// 0xFF, lowest), or 0xFF when none is pending. Reading it clears that source's request
// latch -- for a one-shot interval timer, that disarms it. RDOS 3.12's disk-read timeout
// guard arms Timer 1, unmasks it (OUT 03 = 01), and polls here for 0xC7.
uint8_t Tms5501::readIntAddr(const Clock& clk) {
    uint8_t active = (uint8_t)(pendingSources(clk) & mask_);
    if (active == 0) return 0xFF;                 // no unmasked source pending
    int bit = 0;
    while (!(active & (1u << bit))) ++bit;         // lowest set bit = highest priority
    for (int i = 0; i < 5; ++i)                    // clear the reported source's latch
        if (kTimerBit[i] == bit) timerArmed_[i] = false;
    return (uint8_t)(0xC7 + 8 * bit);              // C7,CF,D7,DF,E7,EF,F7,FF
}

// The INT pin: any unmasked source pending. Only the interval timers feed it in Phase 1,
// and the FDC board does not route it to the bus (assertsInt() is false).
bool Tms5501::irq(const Clock& clk) const {
    return (pendingSources(clk) & mask_) != 0;
}

// RES / RESET* / power-on: receiver to search mode, TX to mark, RDA/ORE cleared,
// TBE set (the transmitter is ready), timers cleared. Does NOT touch the baud strap
// or the command byte -- those are separate from the RES strobe.
void Tms5501::resetAction(const Clock& clk) {
    rdrf_     = false;
    ovrn_     = false;
    rxData_   = 0;
    txFreeAt_ = clk.now();
    rxNextAt_ = clk.now();
    for (bool& a : timerArmed_) a = false;  // one-shot timers cleared; latches drop

    ctsPin_ = ctsNow();
    txRoom_ = stream_->writable();
}

void Tms5501::powerOn(const Clock& clk) {
    command_ = 0;
    hbd_     = false;
    baud_    = baudBase_;
    resetAction(clk);

    // The endpoint STAYS CONNECTED -- switching the machine on does not unplug the
    // terminal. But control_ is now clear, so RTS/BREAK reach the wire.
    driveControl();
    programLine();
}

// ---------------------------------------------------------------------------
// SNAPSHOT/RESTORE. The live chip state; NOT the straps or the stream (a host
// handle, re-CONNECTed). baudBase_/stopBits_/hbd_ travel because the guest programs
// them at runtime (they are not config).
// ---------------------------------------------------------------------------
void Tms5501::serialize(StateWriter& w) const {
    w.u32((uint32_t)baud_);
    w.u32((uint32_t)baudBase_);
    w.u8((uint8_t)stopBits_);
    w.boolean(hbd_);
    w.u8(command_);
    w.u8(mask_);
    w.u8(parallelOut_);
    for (uint64_t t : timerFireAt_) w.u64(t);
    for (bool a : timerArmed_)      w.boolean(a);
    w.u8(rxData_);
    w.u64(rxCount_);
    w.boolean(rdrf_);
    w.boolean(ovrn_);
    w.boolean(ctsPin_);
    w.boolean(txRoom_);
    w.u64(txFreeAt_);
    w.u64(rxNextAt_);
}

void Tms5501::deserialize(StateReader& r) {
    baud_        = (long long)r.u32();
    baudBase_    = (long long)r.u32();
    stopBits_    = (int)r.u8();
    hbd_         = r.boolean();
    command_     = r.u8();
    mask_        = r.u8();
    parallelOut_ = r.u8();
    for (auto& t : timerFireAt_) t = r.u64();
    for (auto& a : timerArmed_)  a = r.boolean();
    rxData_      = r.u8();
    rxCount_     = r.u64();
    rdrf_        = r.boolean();
    ovrn_        = r.boolean();
    ctsPin_      = r.boolean();
    txRoom_      = r.boolean();
    txFreeAt_    = r.u64();
    rxNextAt_    = r.u64();
}

// ---------------------------------------------------------------------------
std::vector<Property> Tms5501::properties(const EndpointResolver& resolve) {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "baud";
        x.help  = "Line rate. The guest sets it by writing the baud register; this seeds "
                  "it and shows the effective rate (0 = the register selected no rate)";
        x.kind  = Kind::Int;
        x.radix = 10;  // never on the wire: decimal (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 76800;
        x.unit  = "baud";
        x.get   = [this] { return Value::ofInt(baud_); };
        x.set   = [this](const Value& v, std::string&) {
            baudBase_ = v.i();
            baud_     = baudBase_ * (hbd_ ? 8 : 1);
            programLine();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        // How fast the emulated console line runs -- the same full|real policy the tape decks
        // carry (mits-88acr, proctech-sol). full (default): the line does not pace, so the
        // console keeps up with the guest whatever baud it programmed; a 16FDC strapped for
        // fixed 300-baud modem mode (its switch 5) is then no slower than a fast terminal.
        // real: pace in wall time at the programmed baud, for the authentic feel. Either way a
        // real serial ENDPOINT still clocks its own wire -- this governs only the internal pacing.
        Property x;
        x.name    = "rate";
        x.help    = "Console speed: full (as fast as the guest reads) | real (wall-clock baud)";
        x.kind    = Kind::Str;
        x.choices = {"full", "real"};
        x.get     = [this] { return Value::ofStr(paceReal_ ? "real" : "full"); };
        x.set     = [this](const Value& v, std::string& err) {
            if (v.s() == "full")      paceReal_ = false;
            else if (v.s() == "real") paceReal_ = true;
            else { err = "rate must be \"full\" or \"real\""; return false; }
            return true;
        };
        p.push_back(std::move(x));
    }
    p.push_back(pinStrapProperty(
        "dcd", "/DCD pin: grounded on the card, or wired to the connector", dcdStrap));
    p.push_back(pinStrapProperty(
        "cts", "/CTS pin: grounded on the card, or wired -- and then it gates the transmitter",
        ctsStrap));
    {
        Property x;
        x.name = "lines";
        x.help = "Live pin state (read-only). CAPITALS = asserted. in: DCD CTS";
        x.kind = Kind::Str;
        x.get  = [this] {
            std::string s;
            s += carrier()     ? "DCD " : "dcd ";
            s += clearToSend() ? "CTS"  : "cts";
            return Value::ofStr(s);
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "connect";
        x.help = "The endpoint on the other end of the line (CONNECT sets this)";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(stream_->describe()); };
        x.set  = [this, resolve](const Value& v, std::string& err) {
            if (!resolve) { err = "no endpoint resolver installed"; return false; }
            auto s = resolve(v.s(), err);
            if (!s) return false;
            connect(std::move(s));
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

} // namespace altair
