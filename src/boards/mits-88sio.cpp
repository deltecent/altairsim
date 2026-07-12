#include "boards/mits-88sio.h"

#include "host/stream.h"

#include <utility>

namespace altair {

// ---------------------------------------------------------------------------
// THE STATUS WORD, AND WHY IT SHARES NO CODE WITH THE 2SIO.
//
// The 88-2SIO's 6850 status bits read TRUE: bit set = condition true. The
// 88-SIO's READY bits read INVERTED: bit CLEAR = ready. Both cards can be in the
// same machine, both conventions live at once, and a driver written against one
// silently misbehaves on the other rather than failing loudly. The temptation to
// factor out "a UART" is exactly the mistake that produces a shared helper with a
// bool flipping its polarity, and then a card that gets the flag wrong once.
//
// So they are two files, and this constant block is the reason.
//
// From the manual's Theory of Operation, "Bit Definition", as amended by the
// errata sheet (see SioRev in the header):
//
//                        Rev 0 (as shipped)              Rev 1 (errata at factory)
//   bit 7   LOW      Output device ready             Output device ready (TBMT)
//   bit 6            not used                        not used
//   bit 5   HIGH     Data Available                  not used
//   bit 4   HIGH     Data Overflow                   Data Overflow
//   bit 3   HIGH     Framing Error                   Framing Error
//   bit 2   HIGH     Parity Error                    Parity Error
//   bit 1   HIGH     X-mitter Buffer Empty           not used
//   bit 0   LOW      Input device ready              Input device ready (DAV)
// ---------------------------------------------------------------------------
namespace {

constexpr uint8_t kOutputNotReady = 0x80;  // bit 7 -- INVERTED. Clear = ready.
constexpr uint8_t kInputNotReady  = 0x01;  // bit 0 -- INVERTED. Clear = ready.

// True-sense error bits. They EXIST and are always ZERO, for the same reason the
// 2SIO's are: they report line noise, and there is no line. A ByteStream delivers
// the byte that was sent or it delivers nothing, and synthesizing a framing error
// from it would mean inventing a noise model, which means inventing a probability
// -- the exact kind of number DESIGN.md 0.1 says to ask about rather than make up.
constexpr uint8_t kOverflow     = 0x10;  // bit 4
constexpr uint8_t kFramingError = 0x08;  // bit 3
constexpr uint8_t kParityError  = 0x04;  // bit 2

// Rev 0 ONLY: the UART's own two flags, in TRUE sense, alongside the inverted
// ready bits. The errata modification reroutes TBMT and DAV to bits 7 and 0 and
// leaves these two undriven -- which IS what a Rev 1 board is.
constexpr uint8_t kRev0DataAvailable = 0x20;  // bit 5
constexpr uint8_t kRev0TxBufferEmpty = 0x02;  // bit 1

// ---------------------------------------------------------------------------
// UNDRIVEN BITS READ AS 1 (Patrick, 2026-07-12 -- asked, because the manual says
// "not used" and stops).
//
// Which is also what the hardware says: these bits are presented by an 8T97
// tri-state buffer (parts list, ICs K/L/N) whose input pad is left unconnected,
// and an unconnected TTL input floats HIGH.
//
// The visible consequence is that a Rev 0 driver run against a Rev 1 board sees
// bit 5 stuck high -- "data always available" -- and reads garbage forever. That is
// a real hardware consequence of installing the wrong card, and it is the correct
// outcome. Had the sense been the other way it would hang instead: both are broken,
// but they are broken DIFFERENTLY, which is why it was worth asking rather than
// picking one.
// ---------------------------------------------------------------------------
constexpr uint8_t kUnusedRev0 = 0x40;  // bit 6
constexpr uint8_t kUnusedRev1 = 0x62;  // bits 6, 5, 1

// The control channel, WRITE. This is the whole of it. There is no 6850-style
// control register on this card: word format and baud are soldered pads, and the
// only thing an OUT can change is the pair of interrupt-enable flip-flops (IC B).
//
//   D1  D0    OUTPUT INTERRUPT   INPUT INTERRUPT
//    0   0        disabled           disabled
//    0   1        disabled           ENABLED
//    1   0        ENABLED            disabled
//    1   1        ENABLED            ENABLED
constexpr uint8_t kInIntEnable  = 0x01;  // D0
constexpr uint8_t kOutIntEnable = 0x02;  // D1

SioBoard::EndpointResolver g_resolver;

// A card in a backplane always has a clock, but Bus::attach() is public, so a
// board CAN be wired up without a machine around it. A UART with no clock is a
// chip with no crystal: it cannot receive and it cannot time a character. It reads
// as a dead card rather than dereferencing a null pointer.
Clock& deadCard() {
    static Clock stopped;
    return stopped;
}

} // namespace

void SioBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

// THE ONE PLACE THE LINE IS EVER REPLACED. Four callers (the constructor, CONNECT,
// DISCONNECT, and the `connect` property) each used to rebuild the filter chain by
// hand, which is four chances to forget the FilterStream wrapper and silently lose
// every transform on that endpoint.
void SioBoard::attachStream(std::unique_ptr<ByteStream> s) {
    auto f  = std::make_unique<FilterStream>(std::move(s));
    filter_ = f.get();
    stream_ = std::move(f);
}

SioBoard::SioBoard() {
    // -> NullStream. There is no null pointer in the stream path, ever: a card with
    // nothing plugged into it is a card with a DEAD line, not a dangling one.
    attachStream(std::make_unique<NullStream>());
}

SioBoard::~SioBoard() {
    // The queue is holding a lambda with `this` inside it, and a card can be pulled
    // out of a RUNNING machine (`BOARDS REMOVE sio0`). A deadline that fires into a
    // destroyed board is a use-after-free with a two-week fuse on it.
    if (clock_) clock_->cancel(wake_);
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

// A character on the wire is a start bit, the data bits, maybe a parity bit, and
// the stop bits. On this card every one of those is a SOLDERED PAD (NDB1/NDB2,
// NPB/POE, NSB) -- the guest cannot change them, which is the deepest difference
// between this board and the 2SIO, where the same numbers come out of a control
// register the guest wrote.
int SioBoard::bitsPerChar() const {
    return 1 + dataBits_ + (parity_ == Parity::None ? 0 : 1) + stopBits_;
}

uint64_t SioBoard::charTStates() const {
    if (baud_ <= 0) return 0;
    const Clock& c = clock_ ? *clock_ : deadCard();
    return (uint64_t)(c.hz() * (long long)bitsPerChar() / baud_);
}

bool SioBoard::txBufferEmpty() const {
    const Clock& c = clock_ ? *clock_ : deadCard();
    return c.now() >= txFreeAt_;
}

// ---------------------------------------------------------------------------
// The UART
// ---------------------------------------------------------------------------

// Pull a byte off the line, if the line has had time to deliver one AND the
// receive register is free to take it.
//
// IT DOES NOT SYNTHESIZE AN OVERRUN, and the long version of why is in
// mits-2sio.cpp over Acia::poll(). The short version: a ByteStream is NOT a serial
// line. It is a buffered, flow-controlled source that will hold the byte until we
// take it, and inventing an overrun from it does not reproduce a hardware
// behaviour -- it MANUFACTURES data loss the host transport does not have. The
// first 2SIO did exactly that and lost typed characters immediately.
//
// The honest consequence is that status bit 4 (Data Overflow) is always zero, and
// that is written down in the board's .md as a limitation rather than left for
// someone to discover.
void SioBoard::pollRx() {
    if (dav_) return;                         // register still full: the line waits
    const Clock& c = clock_ ? *clock_ : deadCard();
    if (c.now() < rxNextAt_) return;          // the character has not finished arriving
    if (!stream_->readable()) return;

    uint8_t b = 0;
    if (stream_->read(&b, 1) != 1) return;

    rxData_   = b;
    dav_      = true;
    rxNextAt_ = c.now() + charTStates();
}

uint8_t SioBoard::statusByte() const {
    uint8_t s = (rev_ == SioRev::Rev1) ? kUnusedRev1 : kUnusedRev0;

    // The two INVERTED ready bits. Set means NOT ready -- this is the trap.
    if (!txReady()) s |= kOutputNotReady;
    if (!rxReady()) s |= kInputNotReady;

    // ...and on a Rev 0 board, the same two facts appear AGAIN, at bits 5 and 1,
    // in true sense. That redundancy is not a mistake in the manual: bits 7 and 0
    // came from the device's handshake lines and bits 5 and 1 from the UART, and
    // they were only guaranteed to agree if you had a device with handshake
    // capability on the connector. The errata modification exists precisely for the
    // people who did not, and it is what a Rev 1 board ships with.
    if (rev_ == SioRev::Rev0) {
        if (rxReady()) s |= kRev0DataAvailable;
        if (txReady()) s |= kRev0TxBufferEmpty;
    }

    (void)kOverflow;
    (void)kFramingError;
    (void)kParityError;  // always clear -- see the constant block
    return s;
}

// ---------------------------------------------------------------------------
// The bus interface
// ---------------------------------------------------------------------------

// Two ports: control/status at BASE (even), data at BASE+1 (odd).
bool SioBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    uint8_t p = c.port();
    return p == base_ || p == (uint8_t)(base_ + 1);
}

uint8_t SioBoard::read(const BusCycle& c) {
    pollRx();  // the receiver runs on the UART's clock, not on ours

    uint8_t v;
    if ((c.port() - base_) & 1) {
        // The DATA channel. Reading it clears the RDAV flip-flop -- and clears it
        // whether or not it was set: a read of an empty receive register on a real
        // UART hands you whatever was last in it and does not error.
        v    = rxData_;
        dav_ = false;
    } else {
        v = statusByte();
    }

    refresh();  // reading the data channel cleared DAV -- and with it, the interrupt
    return v;
}

void SioBoard::write(const BusCycle& c) {
    if ((c.port() - base_) & 1) {
        // The DATA channel. The character goes out, and the transmit buffer is BUSY
        // until it has had time to leave. TBMT is a DEADLINE, not a flag.
        uint8_t b = c.data;
        stream_->write(&b, 1);
        stream_->flush();
        const Clock& clk = clock_ ? *clock_ : deadCard();
        txFreeAt_ = clk.now() + charTStates();
    } else {
        // The CONTROL channel, and the only two bits of it that exist.
        inIntEnabled_  = (c.data & kInIntEnable) != 0;
        outIntEnabled_ = (c.data & kOutIntEnable) != 0;
    }

    refresh();  // a character went out (TBMT fell), or the interrupt enables moved
}

// PIN 73, COMBINATIONAL AND PURE (DESIGN.md 4.4.1). No work happens here.
//
// Three things have to line up: the UART is asking, software enabled that
// interrupt, and the corresponding pad is actually soldered to pin 73. An `in_int`
// strapped to `vi3` goes to a vectored interrupt line instead, which nothing
// watches until an 88-VI card exists -- so it correctly does nothing, exactly as a
// wire into an empty slot would.
bool SioBoard::assertsInt() const {
    if (!clock_) return false;  // no crystal: the chip is not running at all
    if (inIntEnabled_ && inIrq_ == IrqJumper::Int && rxReady()) return true;
    if (outIntEnabled_ && outIrq_ == IrqJumper::Int && txReady()) return true;
    return false;
}

// ---------------------------------------------------------------------------
// THE CARD'S OWN CLOCK (DESIGN.md 4.4.1, 7.5).
//
// The receiver is advanced HERE, and that is the load-bearing line. An
// interrupt-driven driver NEVER reads the status port -- being interrupt-driven is
// precisely so that it does not have to -- so a UART that only ingested a character
// when the guest looked at a register would never ingest one, never raise the
// request, and the operator could type forever with nothing happening.
//
// And the transmitter drains on the UART's clock with nobody touching the card at
// all: a guest that enables the output interrupt, sends a character and HALTs is an
// entirely ordinary driver, and the only thing that can wake it is a deadline this
// card set for itself.
// ---------------------------------------------------------------------------
void SioBoard::refresh() {
    if (!clock_) return;

    pollRx();
    intChanged();  // drive pin 73 -- the bus is not going to come and ask

    clock_->cancel(wake_);
    wake_ = Clock::kNone;

    // Usually there is no edge coming and we set no timer at all: a quiet line with
    // an idle transmitter has nothing whatever to do next, and that is the commonest
    // state in the machine.
    if (uint64_t next = nextEdge()) wake_ = clock_->at(next, [this] { refresh(); });
}

// WHEN COULD THE REQUEST MOVE ON ITS OWN? Zero means never.
uint64_t SioBoard::nextEdge() const {
    const Clock& clk = clock_ ? *clock_ : deadCard();

    uint64_t best = 0;
    auto consider = [&](uint64_t when) {
        // STRICTLY FUTURE. A deadline already past is already showing in
        // assertsInt(); arming a timer for now() would fire inside the drain loop
        // that is running us, and arm it again, and never stop.
        if (when <= clk.now()) return;
        if (!best || when < best) best = when;
    };

    // The transmitter drains on its own clock, and the request rises with it.
    if (outIntEnabled_) consider(txFreeAt_);

    // The receiver fills on its own too -- but ONLY if there is actually a character
    // on the line to fill it with. If the host has sent nothing, there is no edge
    // coming and no timer to set: a byte appearing out of nowhere is not a deadline,
    // it is an event in the OUTSIDE WORLD, and pump() is the one door the outside
    // world comes through.
    if (inIntEnabled_ && !dav_ && stream_->readable()) consider(rxNextAt_);

    return best;
}

void SioBoard::reset(Reset) {
    if (!clock_) return;

    dav_      = false;
    rxData_   = 0;
    txFreeAt_ = clock_->now();  // transmitter is immediately ready
    rxNextAt_ = clock_->now();

    // POC* clears the interrupt-enable flip-flops. We do the same on RESET*, which
    // is an ASSUMPTION and is flagged in the .md: the manual documents the flip-flops
    // (IC B) but not their clear line. Every driver enables its own interrupts after
    // a reset, so nothing period-correct should be able to tell.
    inIntEnabled_  = false;
    outIntEnabled_ = false;

    // The endpoint STAYS CONNECTED. A warm reset does not unplug the terminal, and a
    // guest that reset its UART and found the console gone would be a baffling thing
    // to debug.

    // refresh() CANCELS the outstanding deadline before re-arming, which is why
    // wake_ must not be cleared here: POWER empties the queue under us, but RESET*
    // does not. A character was going out when the button was pressed, and its alarm
    // is still on the books. Zeroing the handle first would ORPHAN it.
    refresh();
}

void SioBoard::power() { reset(Reset::PowerOn); }

// THE ONE DOOR THE OUTSIDE WORLD COMES THROUGH (DESIGN.md 7.1).
void SioBoard::pump() {
    stream_->pump();
    refresh();
}

// A pad moved: `port` (which moves the card in the I/O space), `baud` or a word-
// format pad (which changes how long a character takes, so every deadline this card
// has set is now aimed at the wrong T-state), `in_int`/`out_int` (which wire the
// request is soldered to), or `connect` (a new line, possibly with something already
// on it).
void SioBoard::configChanged() {
    decodeChanged();
    refresh();
}

// ---------------------------------------------------------------------------
// Reflection
// ---------------------------------------------------------------------------

std::vector<Property> SioBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Base address -- MUST BE EVEN. Control at BASE, data at BASE+1";
        x.kind  = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xFE;
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string& err) {
            // The board-specific half of the validation, which only the board knows:
            // the address decode ignores A0 and uses it to pick the channel, so an
            // odd base is not a card you could build. The manual says "any even
            // numbered address from 0 to 376 (octal)" and means it.
            if (v.i() & 1) {
                err = "the 88-SIO decodes an even/odd PAIR -- the base must be even";
                return false;
            }
            base_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "rev";
        x.help    = "Board revision. 1 = the errata mod done at the factory (see the .md)";
        x.kind    = Kind::Enum;
        x.choices = {"0", "1"};
        x.get     = [this] { return Value::ofStr(rev_ == SioRev::Rev1 ? "1" : "0"); };
        x.set     = [this](const Value& v, std::string&) {
            rev_ = (v.s() == "1") ? SioRev::Rev1 : SioRev::Rev0;
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "baud";
        x.help  = "Line rate. A JUMPER on the real card -- software cannot change it";
        x.kind  = Kind::Int;
        x.radix = 10;  // never on the wire: decimal (DESIGN.md 10.0.1)
        x.min   = 50;
        x.max   = 25000;  // "The maximum BAUD rate is (400K/16) 25,000 BAUD"
        x.unit  = "baud";
        x.get   = [this] { return Value::ofInt(baud_); };
        x.set   = [this](const Value& v, std::string&) {
            baud_ = v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "data_bits";
        x.help  = "Data bits per character. The NDB1/NDB2 pads";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.min   = 5;
        x.max   = 8;
        x.get   = [this] { return Value::ofInt(dataBits_); };
        x.set   = [this](const Value& v, std::string&) {
            dataBits_ = (int)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "stop_bits";
        x.help  = "Stop bits. The NSB pad: GND = 1, +V = 2";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.min   = 1;
        x.max   = 2;
        x.get   = [this] { return Value::ofInt(stopBits_); };
        x.set   = [this](const Value& v, std::string&) {
            stopBits_ = (int)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "parity";
        x.help    = "The NPB/POE pads: none | odd | even";
        x.kind    = Kind::Enum;
        x.choices = {"none", "odd", "even"};
        x.get     = [this] {
            switch (parity_) {
            case Parity::Odd:  return Value::ofStr("odd");
            case Parity::Even: return Value::ofStr("even");
            default:           return Value::ofStr("none");
            }
        };
        x.set = [this](const Value& v, std::string&) {
            const std::string& s = v.s();
            parity_ = (s == "odd") ? Parity::Odd : (s == "even") ? Parity::Even : Parity::None;
            return true;
        };
        p.push_back(std::move(x));
    }

    // TWO STRAPS, NOT ONE MODE. The assembly manual: "You may connect the 'OUT'
    // (output device) pad to some priority level, and the 'IN' (input device) pad to
    // some priority level; or you may connect the 'BH' (both devices) pad to a
    // desired priority level for both devices."
    //
    // So the input and output devices can sit at DIFFERENT VI priorities, and "BH"
    // is not a third thing -- it is one wire instead of two, for the case where both
    // go to the same place. Modelling it as `source = rx | tx | both` would have been
    // fewer properties and would have made the manual's own example unrepresentable.
    p.push_back(irqJumperProperty(
        "in_int", "Where the IN pad is soldered (RX): none | int | vi0..vi7", inIrq_));
    p.push_back(irqJumperProperty(
        "out_int", "Where the OUT pad is soldered (TX): none | int | vi0..vi7", outIrq_));

    {
        Property x;
        x.name = "connect";
        x.help = "The endpoint on the other end of the line (CONNECT sets this)";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(stream_->describe()); };
        x.set  = [this](const Value& v, std::string& err) {
            if (!g_resolver) {
                err = "no endpoint resolver installed";
                return false;
            }
            auto s = g_resolver(v.s(), err);
            if (!s) return false;
            attachStream(std::move(s));
            return true;
        };
        p.push_back(std::move(x));
    }

    // The transform chain, verbatim -- upper, strip7in, crlf, bsdel and the rest
    // (DESIGN.md 7.2). They are the FILTER's properties, and the board passes them
    // through, which is why `SET sio0 UPPER=ON` works on a socket exactly as it does
    // on the console, with no code here.
    for (Property& f : filter_->properties()) p.push_back(std::move(f));

    return p;
}

// ONE serial unit. The card has one UART and one connector, so unlike the 2SIO
// there is nothing to disambiguate -- every jumper on it is a BOARD property, which
// is exactly what it is on the PCB. The unit exists because CONNECT names one.
std::vector<UnitDef> SioBoard::units() const {
    return {{"tty", UnitKind::Serial, stream_->describe()}};
}

std::vector<MapEntry> SioBoard::ioMap() const {
    return {
        {(uint32_t)base_, (uint32_t)base_, "read/write",
         "COM2502 -- status (read) / interrupt enables (write)"},
        {(uint32_t)base_ + 1, (uint32_t)base_ + 1, "read/write", "COM2502 -- data"},
    };
}

bool SioBoard::connect(const std::string& unit, const std::string& ep, std::string& err) {
    if (unit != "tty") {
        err = "sio has no unit '" + unit + "' -- it has one, and it is called 'tty'";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    auto s = g_resolver(ep, err);
    if (!s) return false;
    attachStream(std::move(s));

    refresh();  // a new line, and it may already have something waiting on it
    return true;
}

bool SioBoard::disconnect(const std::string& unit, std::string& err) {
    if (unit != "tty") {
        err = "sio has no unit '" + unit + "' -- it has one, and it is called 'tty'";
        return false;
    }
    attachStream(std::make_unique<NullStream>());

    refresh();  // the line went dead: no more characters are coming off it
    return true;
}

} // namespace altair
