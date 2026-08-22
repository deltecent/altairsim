#include "chips/sig2651.h"

#include "core/clock.h"
#include "core/statefile.h"

#include <utility>

namespace altair {

namespace {

// ---- COMMAND word bits (Base+15, §2.5) ----
// D0 (TxEN) is not named: like the 8251, the transmitter is not gated on it (a guest
// polls TxRDY before enabling it, and TxRDY means "holding register free"). D6/D7 are the
// operating-mode field on a general 2651; the SS-1 uses only normal mode, so they stay 0.
// Reset-error (D4) clears PE/OE/FE, which we never set, so there is nothing to clear.
constexpr uint8_t kDTR  = 0x02;  // /DTR output (1 = asserted low)
constexpr uint8_t kRxEN = 0x04;  // receive enable
constexpr uint8_t kBRK  = 0x08;  // force break
constexpr uint8_t kRTS  = 0x20;  // /RTS output (1 = asserted low)

// ---- STATUS byte bits (Base+13, §2.2) ----
constexpr uint8_t kTxRDY = 0x01;
constexpr uint8_t kRxRDY = 0x02;
constexpr uint8_t kTxEMT = 0x04;
// D3 PE, D4 OE, D5 FE report line noise. A ByteStream delivers the byte that was sent or
// nothing, so there is no noise to report -- they stay 0, the same stance the Intel8251
// and Mc6850 take (inventing a framing error means inventing a noise probability,
// DESIGN.md 0.1). If a real serial-port endpoint ever lands, those become real events
// and come from the place that knows -- not from here.
constexpr uint8_t kDCD = 0x40;  // D6: high = DCD line asserted (active-low)
constexpr uint8_t kDSR = 0x80;  // D7: high = DSR line asserted (active-low)

// Mode Register 2's low nibble selects the baud rate from the on-chip generator (§2.4).
// 134.5 baud is stored as 134 (integer T-state pacing; the half-baud is cosmetic).
constexpr long long kBaudTable[16] = {
    50,   75,   110,  134,  150,  300,  600,  1200,
    1800, 2000, 2400, 3600, 4800, 7200, 9600, 19200,
};

}  // namespace

// ---------------------------------------------------------------------------
// Mode Register 1 decoded (§2.3). Layout [S2 S1 | EP PEN | L2 L1 | B2 B1].
// ---------------------------------------------------------------------------
int Sig2651::dataBits() const { return 5 + ((mode1_ >> 2) & 0x03); }

LineParity Sig2651::parity() const {
    if (!(mode1_ & 0x10)) return LineParity::None;                  // PEN (bit 4)
    return (mode1_ & 0x20) ? LineParity::Even : LineParity::Odd;    // EP (bit 5)
}

int Sig2651::stopBits() const {
    // 00 invalid, 01 = 1, 10 = 1.5, 11 = 2. The bit-count rounds 1.5 up to 2; the stop
    // bits are all mark, so this only sizes how long a character occupies the line.
    switch ((mode1_ >> 6) & 0x03) {
    case 0x02:
    case 0x03: return 2;
    default:   return 1;
    }
}

int Sig2651::bitsPerChar() const {
    return 1 + dataBits() + (parity() == LineParity::None ? 0 : 1) + stopBits();
}

LineParams Sig2651::params() const {
    LineParams p;
    p.baud     = baud;
    p.dataBits = dataBits();
    p.stopBits = stopBits();
    p.parity   = parity();
    return p;
}

uint64_t Sig2651::bitTStates(const Clock& clk) const {
    return baud > 0 ? clk.tStatesPer(baud) : 0;
}

uint64_t Sig2651::charTStates(const Clock& clk) const {
    return bitTStates(clk) * (uint64_t)bitsPerChar();
}

// MR2's low nibble is the baud rate; the high nibble is a board-fixed 0111 with no
// software effect. The programmed rate overwrites the seed `baud` (the 2651 owns its
// baud generator -- unlike the SBC's 8251, which had an external CTC).
void Sig2651::applyMr2(uint8_t v) {
    mode2_ = v;
    baud   = kBaudTable[v & 0x0F];
}

void Sig2651::programLine() {
    std::string err;
    if (stream_->setParams(params(), err)) return;
    log_.push_back(name_ + ": " + err);
}

void Sig2651::connect(std::unique_ptr<ByteStream> s) {
    stream_ = std::move(s);   // the line is taken as it is -- no transform chain (the
    programLine();            // console owns that, DESIGN.md 7.2)
    driveControl();
}

void Sig2651::disconnect() { connect(std::make_unique<NullStream>()); }

std::vector<std::string> Sig2651::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    return out;
}

void Sig2651::driveControl() {
    LineControl c;
    c.rts = (command_ & kRTS) != 0;   // command bit high -> /RTS driven low (asserted)
    c.dtr = (command_ & kDTR) != 0;   // command bit high -> /DTR driven low (asserted)
    c.brk = (command_ & kBRK) != 0;
    stream_->setControl(c);
}

// ---------------------------------------------------------------------------
// Advance the receiver. A frame is clocked in over a whole character-time; only when it
// finishes does RxRDY rise and the byte become readable. It does NOT synthesize overrun:
// a new frame starts only when the register is free, so a byte the guest has not read
// waits in the flow-controlled stream -- the same decision as Intel8251::poll.
// ---------------------------------------------------------------------------
void Sig2651::poll(const Clock& clk) {
    uint64_t now = clk.now();

    if (rxActive_ && now >= rxDoneAt_) {
        rxData_   = rxPending_;
        rxRdy_    = true;
        rxActive_ = false;
        rxNextAt_ = rxDoneAt_;   // the next character cannot begin before this one ended
    }

    if (!rxActive_ && !rxRdy_ && now >= rxNextAt_ && (command_ & kRxEN) &&
        stream_->readable()) {
        uint8_t b = 0;
        if (stream_->read(&b, 1) == 1) {
            rxPending_ = b;
            rxStart_   = now;
            rxDoneAt_  = now + charTStates(clk);
            rxActive_  = true;
            ++rxCount_;  // a byte crossed the wire -- the run loop's proof this line is live
        }
    }
}

bool Sig2651::rxWaiting() const {
    return !rxActive_ && !rxRdy_ && (command_ & kRxEN) && stream_->readable();
}

bool Sig2651::txReady(const Clock& clk) const {
    // Buffer-empty, and the far end has somewhere to put the byte. TxRDY means the
    // holding register is free; it is not gated by TxEN (a guest polls it before enabling
    // the transmitter). Same meaning as the 8251's TxRDY.
    return clk.now() >= txFreeAt_ && stream_->writable();
}

bool Sig2651::txEmpty(const Clock& clk) const { return clk.now() >= txFreeAt_; }

uint8_t Sig2651::statusByte(const Clock& clk) const {
    uint8_t s = 0;
    if (txReady(clk)) s |= kTxRDY;
    if (rxRdy_)       s |= kRxRDY;
    if (txEmpty(clk)) s |= kTxEMT;
    s |= kDCD | kDSR;   // both modem-status bits read asserted on a byte-clean transport
    return s;
}

uint8_t Sig2651::readStatus(const Clock& clk) {
    poll(clk);
    return statusByte(clk);
}

uint8_t Sig2651::readData(const Clock& clk) {
    poll(clk);
    rxRdy_ = false;   // reading the data register clears RxRDY
    return rxData_;
}

uint8_t Sig2651::readMode() {
    uint8_t v = (modePtr_ == 0) ? mode1_ : mode2_;
    modePtr_ ^= 1;    // a mode-address access moves the pointer (MR1<->MR2)
    return v;
}

void Sig2651::writeData(uint8_t v, const Clock& clk) {
    stream_->write(&v, 1);
    stream_->flush();
    // The character is on the wire; the transmit register is BUSY until it has had time
    // to leave -- a deadline, not a flag (the same timing the 8251 and 6850 use).
    txFreeAt_ = clk.now() + charTStates(clk);
}

// ---------------------------------------------------------------------------
// THE ONE PIECE OF INTERNAL SEQUENCING: the mode-register pointer. MR1 and MR2 share the
// mode address; the pointer routes the first mode access after reset to MR1, the next to
// MR2, then wraps. MR1 must always be written before MR2 (§2.1). The pointer is reset to
// MR1 only by chip reset/power-on -- the command register has no mode-reset bit on this
// board, so a command write leaves the pointer alone (§2.5, §2.6).
// ---------------------------------------------------------------------------
void Sig2651::writeMode(uint8_t v) {
    if (modePtr_ == 0) mode1_ = v;   // Mode Register 1: the frame
    else               applyMr2(v);  // Mode Register 2: the baud rate
    modePtr_ ^= 1;
    programLine();                   // MR1 changed the frame, MR2 the baud -- push both
}

void Sig2651::writeCommand(uint8_t v, const Clock& clk) {
    (void)clk;
    command_ = v;
    // TxEN/RxEN latch in command_ and gate tx/rx; reset-error (D4) clears PE/OE/FE, which
    // we never set. RTS/DTR/BREAK are pins -- drive them now.
    driveControl();
}

// POWER-ON. A known-good state: pointer at MR1, transmitter idle, endpoint still
// connected. The 2651's RESET pin is not modeled as a card-driven bus reset (the monitor
// always reprograms the chip) -- see the board's reset().
void Sig2651::powerOn(const Clock& clk) {
    mode1_   = 0x4E;   // 8 data bits, 1 stop, no parity, 16x async -- a sane default frame
    mode2_   = 0x7E;   // board-convention 0111 hi nibble, 9600 baud
    command_ = 0;
    modePtr_ = 0;
    baud     = kBaudTable[mode2_ & 0x0F];
    rxData_ = rxPending_ = 0;
    rxRdy_ = rxActive_ = false;
    rxStart_ = rxDoneAt_ = 0;
    txFreeAt_ = rxNextAt_ = clk.now();
    driveControl();    // command_ is 0: RTS/DTR negated, no break
    programLine();
}

// ---------------------------------------------------------------------------
// Reflection -- the unit properties the board presents under its serial unit.
// ---------------------------------------------------------------------------
std::vector<Property> Sig2651::properties(const EndpointResolver& resolve) {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "baud";
        x.help  = "Line rate. The 2651 generates it from Mode Register 2, so the guest's "
                  "MR2 write overwrites this; it seeds the line before then (min 50)";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.min   = 50;
        x.max   = 19200;   // the on-chip baud generator's ceiling (kBaudTable)
        x.unit  = "baud";
        x.get   = [this] { return Value::ofInt(baud); };
        x.set   = [this](const Value& v, std::string&) {
            baud = v.i();
            programLine();
            return true;
        };
        p.push_back(std::move(x));
    }
    p.push_back(irqJumperProperty(
        "interrupt",
        "Where this port's IRQ is jumpered: none | int | vi0..vi7. The chip raises it on "
        "RxRDY; the standard board routes it through the 8259A instead",
        jumper));
    {
        Property x;
        x.name = "connect";
        x.help = "The endpoint on the other end of the line (CONNECT sets this)";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(stream_->describe()); };
        x.set  = [this, resolve](const Value& v, std::string& err) {
            if (!resolve) {
                err = "no endpoint resolver installed";
                return false;
            }
            auto s = resolve(v.s(), err);
            if (!s) return false;
            connect(std::move(s));
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

void Sig2651::serialize(StateWriter& w) const {
    w.u8(mode1_);
    w.u8(mode2_);
    w.u8(command_);
    w.u32((uint32_t)modePtr_);
    w.u8(rxData_);
    w.boolean(rxRdy_);
    w.u8(rxPending_);
    w.boolean(rxActive_);
    w.u64(rxStart_);
    w.u64(rxDoneAt_);
    w.u64(rxNextAt_);
    w.u64(rxCount_);
    w.u64(txFreeAt_);
}

void Sig2651::deserialize(StateReader& r) {
    mode1_    = r.u8();
    mode2_    = r.u8();
    command_  = r.u8();
    modePtr_  = (int)r.u32();
    rxData_   = r.u8();
    rxRdy_    = r.boolean();
    rxPending_ = r.u8();
    rxActive_ = r.boolean();
    rxStart_  = r.u64();
    rxDoneAt_ = r.u64();
    rxNextAt_ = r.u64();
    rxCount_  = r.u64();
    txFreeAt_ = r.u64();
    baud      = kBaudTable[mode2_ & 0x0F];  // derived from MR2, not stored separately
}

}  // namespace altair
