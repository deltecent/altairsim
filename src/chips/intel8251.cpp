#include "chips/intel8251.h"

#include "core/clock.h"
#include "core/statefile.h"

#include <bit>
#include <utility>

namespace altair {

namespace {

// ---- COMMAND word bits (control write, when NOT expecting a mode word) ----
// D0 (TxEN) and D4 (ER) are not named here: TxEN does not gate the status TxRDY bit
// (which means "buffer empty"; see txReady), and ER resets PE/OE/FE, which we never
// set. D7 (EH, sync-only hunt) is likewise not modeled.
constexpr uint8_t kDTR  = 0x02;  // /DTR output (1 = asserted low)
constexpr uint8_t kRxE  = 0x04;  // receive enable
constexpr uint8_t kSBRK = 0x08;  // send break
constexpr uint8_t kRTS  = 0x20;  // /RTS output (1 = asserted low)
constexpr uint8_t kIR   = 0x40;  // internal reset -> back to expecting a mode word

// ---- STATUS byte bits ----
constexpr uint8_t kTxRDY   = 0x01;
constexpr uint8_t kRxRDY   = 0x02;
constexpr uint8_t kTxEMPTY = 0x04;
// D3 PE, D4 OE, D5 FE report line noise. There is no line to have noise on -- a
// ByteStream delivers the byte that was sent or nothing -- so they are always zero,
// the same stance the Mc6850 and Uart1602 take (and for the same reason: inventing a
// framing error means inventing a noise probability, DESIGN.md 0.1). D6 SYNDET is
// sync-only. If a real serial-port endpoint ever lands, those become real events and
// come from the place that knows -- not from here.
constexpr uint8_t kDSR = 0x80;

// The parity bit a real 8251 would append: even parity makes the total number of 1s
// even, odd parity makes it odd. Only reached when the guest programs parity on (the
// SD monitor does not), but modeled correctly so the DSR line is right if it does.
bool parityBit(uint8_t data, int nbits, LineParity p) {
    int ones = std::popcount((unsigned)(data & ((1u << nbits) - 1)));
    bool even = (ones & 1) != 0;         // true if an odd count -> even-parity bit is 1
    return p == LineParity::Even ? even : !even;
}

} // namespace

// ---------------------------------------------------------------------------
// The MODE word, decoded. Async layout [S2 S1 | EP PEN | L2 L1 | B2 B1].
// ---------------------------------------------------------------------------
int Intel8251::dataBits() const { return 5 + ((mode_ >> 2) & 0x03); }

LineParity Intel8251::parity() const {
    if (!(mode_ & 0x10)) return LineParity::None;         // PEN
    return (mode_ & 0x20) ? LineParity::Even : LineParity::Odd;  // EP
}

int Intel8251::stopBits() const {
    // 00 invalid, 01 = 1, 10 = 1.5, 11 = 2. The bit-count rounds 1.5 up to 2; the
    // stop bits are all mark, so this only affects how long a character occupies the
    // line, never the DSR line model.
    switch ((mode_ >> 6) & 0x03) {
    case 0x02:
    case 0x03: return 2;
    default:   return 1;
    }
}

int Intel8251::bitsPerChar() const {
    return 1 + dataBits() + (parity() == LineParity::None ? 0 : 1) + stopBits();
}

LineParams Intel8251::params() const {
    LineParams p;
    p.baud     = baud;
    p.dataBits = dataBits();
    p.stopBits = stopBits();
    p.parity   = parity();
    return p;
}

uint64_t Intel8251::bitTStates(const Clock& clk) const {
    return baud > 0 ? clk.tStatesPer(baud) : 0;
}

uint64_t Intel8251::charTStates(const Clock& clk) const {
    return bitTStates(clk) * (uint64_t)bitsPerChar();
}

// PUSH THE CARD'S STRAP AND THE GUEST'S FRAME AT THE WIRE. Ignored by every endpoint
// but a real serial port. If the host refuses (a cable that cannot do the rate), say
// so once and go on running at the strap -- the strap is what the guest can measure.
void Intel8251::programLine() {
    std::string err;
    if (stream_->setParams(params(), err)) return;
    log_.push_back(name_ + ": " + err);
}

void Intel8251::connect(std::unique_ptr<ByteStream> s) {
    stream_ = std::move(s);   // the line is taken as it is -- no transform chain (the
    programLine();            // console owns that, DESIGN.md 7.2)
    driveControl();
}

void Intel8251::disconnect() { connect(std::make_unique<NullStream>()); }

std::vector<std::string> Intel8251::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    return out;
}

void Intel8251::driveControl() {
    LineControl c;
    c.rts = (command_ & kRTS) != 0;
    c.dtr = (command_ & kDTR) != 0;   // the 8251 HAS a DTR pin, unlike the 6850
    c.brk = (command_ & kSBRK) != 0;
    stream_->setControl(c);
}

// ---------------------------------------------------------------------------
// D7, THE DSR BIT, WHEN THE CARD HAS STRAPPED RxD TO IT.
//
// The receive frame is on the line for one character-time after it starts. During it,
// D7 mirrors the serial line: the 8251's /DSR pin is active-low and the SBC's RxD
// idles marking (high), so a start bit (space, low) ASSERTS /DSR -> D7 = 1, and a mark
// bit -> D7 = 0. The bits are LSB-first, so the monitor timing the first low run after
// a CR (0x0D, bit0 = 1) measures exactly the start bit. Everything here is in emulated
// T-states, so the monitor's IN-loop crosses the bit boundaries correctly even flat
// out. See reference/Intel 8251 USART.md and the board reference.
// ---------------------------------------------------------------------------
bool Intel8251::dsrBit(const Clock& clk) const {
    if (dsrSrc != DsrSource::FollowRxD) return false;
    if (!rxActive_) return false;                     // idle: line marking, /DSR negated
    uint64_t bit = bitTStates(clk);
    if (bit == 0) return false;

    uint64_t i = (clk.now() - rxStart_) / bit;
    if (i == 0) return true;                          // start bit: space -> D7 = 1

    int nd = dataBits();
    if ((int)i <= nd) {                               // data bit i-1, LSB-first
        bool line = ((rxPending_ >> (i - 1)) & 1) != 0;
        return !line;                                 // mark(1)->0, space(0)->1
    }
    if (parity() != LineParity::None && (int)i == nd + 1) {
        return !parityBit(rxPending_, nd, parity());
    }
    return false;                                     // stop bit(s): mark -> D7 = 0
}

// ---------------------------------------------------------------------------
// Advance the receiver. A frame is clocked in over a whole character-time; only when
// it finishes does RxRDY rise and the byte become readable.
//
// It does NOT synthesize overrun. A new frame starts only when the register is free
// (!rxRdy_), so a byte the guest has not read waits in the flow-controlled stream
// rather than being lost -- the same decision, and the same reasoning, as Mc6850::poll.
// ---------------------------------------------------------------------------
void Intel8251::poll(const Clock& clk) {
    uint64_t now = clk.now();

    // A frame that has finished arriving lands in the receive register.
    if (rxActive_ && now >= rxDoneAt_) {
        rxData_   = rxPending_;
        rxRdy_    = true;
        rxActive_ = false;
        rxNextAt_ = rxDoneAt_;   // the next character cannot begin before this one ended
    }

    // ...and if the register is free and a byte is waiting on the line, start its frame.
    if (!rxActive_ && !rxRdy_ && now >= rxNextAt_ && (command_ & kRxE) &&
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

bool Intel8251::rxWaiting() const {
    return !rxActive_ && !rxRdy_ && (command_ & kRxE) && stream_->readable();
}

bool Intel8251::txReady(const Clock& clk) const {
    // Buffer-empty, and the far end has somewhere to put the byte. The status TxRDY is
    // the "holding register free" meaning the monitor's transmit poll (IN 7D / AND 1)
    // waits on; it is not gated by TxEN here.
    return clk.now() >= txFreeAt_ && stream_->writable();
}

bool Intel8251::txEmpty(const Clock& clk) const { return clk.now() >= txFreeAt_; }

uint8_t Intel8251::statusByte(const Clock& clk) const {
    uint8_t s = 0;
    if (txReady(clk)) s |= kTxRDY;
    if (rxRdy_)       s |= kRxRDY;
    if (txEmpty(clk)) s |= kTxEMPTY;
    if (dsrBit(clk))  s |= kDSR;
    return s;
}

uint8_t Intel8251::readStatus(const Clock& clk) {
    poll(clk);
    return statusByte(clk);
}

uint8_t Intel8251::readData(const Clock& clk) {
    poll(clk);
    rxRdy_ = false;   // reading the data register clears RxRDY
    return rxData_;
}

void Intel8251::writeData(uint8_t v, const Clock& clk) {
    stream_->write(&v, 1);
    stream_->flush();
    // The character is on the wire, and the transmit register is BUSY until it has had
    // time to leave -- a deadline, not a flag (the same timing the 6850 uses for TDRE).
    txFreeAt_ = clk.now() + charTStates(clk);
}

// ---------------------------------------------------------------------------
// THE WRITE-TARGET STATE MACHINE. One control address, disambiguated by state:
// the first control write after a reset is the MODE word; every one after that is a
// COMMAND word; a COMMAND with the internal-reset bit rewinds to expecting a MODE.
// ---------------------------------------------------------------------------
void Intel8251::writeControl(uint8_t v, const Clock& clk) {
    (void)clk;
    if (expectMode_) {
        mode_       = v;
        expectMode_ = false;
        programLine();   // the MODE word IS the frame on the wire
        return;
    }

    command_ = v;
    if (v & kIR) expectMode_ = true;  // internal reset: the next control write is a MODE
    // ER (v & kER) clears PE/OE/FE -- we model none, so there is nothing to clear.
    // EH (bit 7) is sync-only. TxEN/RxE latch in command_ and gate tx/rx.
    driveControl();                   // RTS/DTR/BREAK are pins; drive them now
}

// POWER-ON. A known-good state, expecting a mode word, endpoint still connected. The
// 8251's RESET pin is not modeled as a card-driven bus reset (the monitor always
// reprograms the chip) -- see the board's reset().
void Intel8251::powerOn(const Clock& clk) {
    expectMode_ = true;
    mode_       = 0x4E;   // a sane default frame (8N1 x16) until the guest programs one
    command_    = 0;
    rxData_ = rxPending_ = 0;
    rxRdy_ = rxActive_ = false;
    rxStart_ = rxDoneAt_ = 0;
    txFreeAt_ = rxNextAt_ = clk.now();
    driveControl();       // control_ is 0: RTS/DTR negated, no break
    programLine();
}

// ---------------------------------------------------------------------------
// Reflection -- the unit properties the board presents under its serial unit.
// ---------------------------------------------------------------------------
std::vector<Property> Intel8251::properties(const EndpointResolver& resolve) {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "baud";
        x.help  = "Line rate. On the SBC the CTC generates it; here it paces the receive "
                  "line and sizes the auto-baud bit. No free-running setting (min 50)";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.min   = 50;
        x.max   = 76800;
        x.unit  = "baud";
        x.get   = [this] { return Value::ofInt(baud); };
        x.set   = [this](const Value& v, std::string&) {
            baud = v.i();
            programLine();
            return true;
        };
        p.push_back(std::move(x));
    }
    // NOTE: the /DSR source is NOT a property here. On a real card /DSR is either a
    // modem line or -- on the SBC-200 -- a JUMPER strapping RxD to it. That jumper is a
    // fact about the CARD, so the owning board owns the knob (SbcBoard's `rxd2dsr`) and
    // sets dsrSrc; the chip only implements the pin (dsrBit()). This mirrors the 6850's
    // dcd/cts PinStraps, which the board sets and the chip honors.
    p.push_back(irqJumperProperty(
        "interrupt", "Where this port's IRQ is jumpered: none | int | vi0..vi7 "
                     "(decoded; not yet honored)",
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

void Intel8251::serialize(StateWriter& w) const {
    w.boolean(expectMode_);
    w.u8(mode_);
    w.u8(command_);
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

void Intel8251::deserialize(StateReader& r) {
    expectMode_ = r.boolean();
    mode_       = r.u8();
    command_    = r.u8();
    rxData_     = r.u8();
    rxRdy_      = r.boolean();
    rxPending_  = r.u8();
    rxActive_   = r.boolean();
    rxStart_    = r.u64();
    rxDoneAt_   = r.u64();
    rxNextAt_   = r.u64();
    rxCount_    = r.u64();
    txFreeAt_   = r.u64();
}

} // namespace altair
