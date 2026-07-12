#include "boards/mits-2sio.h"

#include "host/stream.h"

#include <utility>

namespace altair {

// ---------------------------------------------------------------------------
// 6850 status register -- TRUE SENSE.
//
// TRAP, and it is written down in the board's .md because it has bitten people
// for forty years: the 88-SIO's status bits are INVERTED and the 88-2SIO's are
// not. A machine can have both cards in it, both conventions live at once. Do
// not share code between them without thinking. (We don't: they will be two
// files.)
// ---------------------------------------------------------------------------
namespace {
constexpr uint8_t kRdrf = 0x01;  // receive data register full
constexpr uint8_t kTdre = 0x02;  // transmit data register empty
constexpr uint8_t kDcd  = 0x04;  // set = carrier LOST (the pin is /DCD)
constexpr uint8_t kCts  = 0x08;  // set = NOT clear to send (the pin is /CTS)
constexpr uint8_t kOvrn = 0x20;
constexpr uint8_t kIrq  = 0x80;

// Bit 4 (FE, framing error) and bit 6 (PE, parity error) EXIST and are always
// ZERO, deliberately. They report line noise, and there is no line: a ByteStream
// delivers the byte that was sent or it delivers nothing. Synthesizing them would
// mean inventing a noise model, which would mean inventing a probability, which
// is exactly the kind of number DESIGN.md 0.1 says to ask about rather than make
// up. If a guest ever needs to SEE a framing error, that is a feature request
// with a real use case attached, not a gap to quietly paper over now.

// Control register (write only).
constexpr uint8_t kDivideMask = 0x03;
constexpr uint8_t kMasterReset = 0x03;  // divide field == 11
constexpr uint8_t kWordMask   = 0x1C;   // bits 2-4
constexpr uint8_t kTxCtlMask  = 0x60;   // bits 5-6
constexpr uint8_t kRie        = 0x80;   // bit 7: receive interrupt enable

// Transmit control field (bits 5-6), from the datasheet:
//   00  RTS low,  transmit interrupt DISABLED
//   01  RTS low,  transmit interrupt ENABLED
//   10  RTS high, transmit interrupt disabled
//   11  RTS low,  transmit a BREAK
constexpr uint8_t kTxIrqEnabled = 0x20;  // the 01 case

Sio2Board::EndpointResolver g_resolver;
} // namespace

void Sio2Board::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

// ---------------------------------------------------------------------------
// Acia
// ---------------------------------------------------------------------------

// A character on the wire is a start bit, the data bits, maybe a parity bit, and
// the stop bits. The guest told us which when it wrote the control register, and
// getting this from the register rather than assuming 8N1 is what makes the baud
// timing come out right for a Teletype configured 7E2.
int Acia::bitsPerChar() const {
    switch ((control_ & kWordMask) >> 2) {
    case 0: return 1 + 7 + 1 + 2;  // 7 data, even parity, 2 stop
    case 1: return 1 + 7 + 1 + 2;  // 7 data, odd  parity, 2 stop
    case 2: return 1 + 7 + 1 + 1;  // 7 data, even parity, 1 stop
    case 3: return 1 + 7 + 1 + 1;  // 7 data, odd  parity, 1 stop
    case 4: return 1 + 8 + 0 + 2;  // 8 data, no parity,   2 stop  <- ALTMON writes this
    case 5: return 1 + 8 + 0 + 1;  // 8 data, no parity,   1 stop
    case 6: return 1 + 8 + 1 + 1;  // 8 data, even parity, 1 stop
    default: return 1 + 8 + 1 + 1; // 8 data, odd  parity, 1 stop
    }
}

uint64_t Acia::charTStates(const Clock& clk) const {
    if (baud_ <= 0) return 0;
    return (uint64_t)(clk.hz() * (long long)bitsPerChar() / baud_);
}

void Acia::connect(std::unique_ptr<ByteStream> s) {
    // EVERY endpoint gets the transform chain, whatever it is (DESIGN.md 7.2).
    // The filter is not a console feature that a socket has to do without.
    auto f   = std::make_unique<FilterStream>(std::move(s));
    filter_  = f.get();
    stream_  = std::move(f);
}

void Acia::disconnect() { connect(std::make_unique<NullStream>()); }

// Pull a byte off the line, if the line has had time to deliver one AND the
// receive register is free to take it.
//
// ---------------------------------------------------------------------------
// WHY THIS DOES NOT SYNTHESIZE OVRN, having tried to.
//
// The first version pulled a byte every character-time whether or not the guest
// had read the last one, and raised OVRN and DISCARDED the byte when it hadn't.
// That is what a real 6850 does, and it is wrong here -- it lost data
// immediately and visibly. ALTMON echoes the full command name as you type
// ("D" -> "DUMP "), and while it was busy transmitting those five characters it
// was not reading the receiver, so the address you typed after the D was thrown
// on the floor. The dump command silently did nothing.
//
// The bug was not the pacing. It was believing a ByteStream is a serial LINE.
//
// A real line is free-running: the sender shoves bits down the wire on its own
// clock and the receiver keeps up or loses them, which is what an overrun IS. A
// ByteStream is nothing like that. It is a buffered, flow-controlled source -- a
// pipe, a socket, an OS keyboard queue -- and it is perfectly happy to hold the
// byte until we take it. Inventing an overrun from it does not reproduce a
// hardware behaviour; it MANUFACTURES data loss that the host transport does not
// have, and it breaks transfers that would have worked.
//
// So: pace the arrivals at the baud rate (that part is real, it is what stops
// the guest reading faster than the line allows, and it is the same clock TDRE
// is timed against) -- but only ever take a byte when the register is free.
//
// The honest consequence is that status bit 5 is always zero, and that is
// written down in docs/boards/mits-2sio.md as a limitation rather than left for
// someone to discover. If a HOST SERIAL PORT endpoint ever lands, an overrun
// there is a genuine hardware event and the stream can report it -- from the
// place that actually knows, which is not here.
// ---------------------------------------------------------------------------
void Acia::poll(const Clock& clk) {
    if (rdrf_) return;                  // the register is still full: the line waits
    if (clk.now() < rxNextAt_) return;  // the character has not finished arriving
    if (!stream_->readable()) return;

    uint8_t b = 0;
    if (stream_->read(&b, 1) != 1) return;

    rxData_   = b;
    rdrf_     = true;
    rxNextAt_ = clk.now() + charTStates(clk);
}

uint8_t Acia::readStatus(const Clock& clk) {
    poll(clk);

    uint8_t s = 0;
    if (rdrf_) s |= kRdrf;
    if (clk.now() >= txFreeAt_) s |= kTdre;
    if (ovrn_) s |= kOvrn;

    // /DCD and /CTS are INPUTS, and the status bit is SET when the line is
    // negated. A ByteStream that has a human or a file on the end of it has no
    // modem control in any real sense, so it asserts both -- which is exactly
    // what strapping the pins to ground on the connector does, and what period
    // installers did constantly.
    LineStatus ls = stream_->status();
    if (!ls.carrier) s |= kDcd;
    if (!ls.cts) s |= kCts;

    if (irq(clk)) s |= kIrq;
    return s;
}

uint8_t Acia::readData(const Clock& clk) {
    poll(clk);
    // Reading the data register clears RDRF and OVRN. Note it clears them EVEN IF
    // RDRF was not set: a read of an empty receive register on a real 6850 gives
    // you whatever was last there, and does not error.
    rdrf_ = false;
    ovrn_ = false;
    return rxData_;
}

void Acia::writeControl(uint8_t v, const Clock& clk) {
    if ((v & kDivideMask) == kMasterReset) {
        // The guest asked for a master reset. Do it -- the Python prototype
        // ignored the control register entirely, and this is the write that
        // matters most: ALTMON's very first instruction is `mvi a,3 / out 10h`.
        masterReset(clk);
        // A master reset does NOT latch the rest of the byte; the guest follows
        // up with a second write to set the word format. ALTMON does exactly that.
        return;
    }
    control_ = v;
}

void Acia::writeData(uint8_t v, const Clock& clk) {
    stream_->write(&v, 1);
    stream_->flush();

    // The character is now on the wire, and the transmit register is BUSY until
    // it has had time to get out. This is the line the Mike Douglas BIOS is
    // timing.
    txFreeAt_ = clk.now() + charTStates(clk);
}

void Acia::masterReset(const Clock& clk) {
    control_  = 0;
    rdrf_     = false;
    ovrn_     = false;
    rxData_   = 0;
    txFreeAt_ = clk.now();   // transmitter is immediately ready
    rxNextAt_ = clk.now();
    // The endpoint STAYS CONNECTED. A warm reset does not unplug the terminal --
    // and a guest that reset its UART and found the console gone would be a
    // baffling thing to debug.
}

// The chip's IRQ pin. Note this is asked WITHOUT reference to the jumper: the
// 6850 raises IRQ because of what is happening inside it, and whether that goes
// anywhere is a question about the WIRE, which is the board's business, not the
// chip's. Keeping the two separate is why the status register's IRQ bit reads
// correctly even on a channel whose interrupt is not jumpered at all.
bool Acia::irq(const Clock& clk) const {
    if ((control_ & kRie) && rdrf_) return true;
    if ((control_ & kTxCtlMask) == kTxIrqEnabled && clk.now() >= txFreeAt_) return true;
    return false;
}

// WHEN COULD THIS PIN MOVE ON ITS OWN? Zero means never.
uint64_t Acia::nextEdge(const Clock& clk) const {
    uint64_t best = 0;
    auto consider = [&](uint64_t when) {
        if (when <= clk.now()) return;  // already past: irq() is showing it already
        if (!best || when < best) best = when;
    };

    // The transmitter drains on its own clock. TDRE goes true at txFreeAt_, and
    // with the transmit interrupt jumpered on, IRQ rises with it -- WITH NOBODY
    // TOUCHING THE CHIP. That is the case the old poll-every-instruction model was
    // quietly covering for, and it is exactly the case a guest sitting in a HLT is
    // waiting on.
    if ((control_ & kTxCtlMask) == kTxIrqEnabled) consider(txFreeAt_);

    // The receiver fills on its own too -- but only if there is actually a
    // character on the line to fill it with. If the host has sent nothing, there is
    // no edge coming and no timer to set: a byte appearing out of nowhere is not a
    // deadline, it is an event in the OUTSIDE WORLD, and pump() is the one door the
    // outside world comes through (DESIGN.md 7.1).
    //
    // This is the asymmetry that makes both mechanisms necessary, and it is why the
    // answer to "event queue or periodic timer?" is "both, and you already had the
    // second one."
    if ((control_ & kRie) && !rdrf_ && stream_->readable()) consider(rxNextAt_);

    return best;
}

std::vector<Property> Acia::properties() {
    std::vector<Property> p;

    {
        Property x;
        x.name    = "baud";
        x.help    = "Line rate. A JUMPER on the real card -- software cannot change it";
        x.kind    = Kind::Int;
        x.radix   = 10;  // never on the wire: decimal (DESIGN.md 10.0.1)
        x.min     = 50;
        x.max     = 76800;
        x.unit    = "baud";
        x.get     = [this] { return Value::ofInt(baud_); };
        x.set     = [this](const Value& v, std::string&) {
            baud_ = v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "interrupt";
        x.help    = "Where this channel's IRQ is jumpered: none | int | vi0..vi7";
        x.kind    = Kind::Enum;
        x.choices = {"none", "int", "vi0", "vi1", "vi2", "vi3", "vi4", "vi5", "vi6", "vi7"};
        x.get     = [this] {
            switch (jumper) {
            case IrqJumper::None: return Value::ofStr("none");
            case IrqJumper::Int:  return Value::ofStr("int");
            default:
                return Value::ofStr("vi" + std::to_string((int)jumper - (int)IrqJumper::Vi0));
            }
        };
        x.set = [this](const Value& v, std::string&) {
            const std::string& s = v.s();
            if (s == "none") jumper = IrqJumper::None;
            else if (s == "int") jumper = IrqJumper::Int;
            else jumper = (IrqJumper)((int)IrqJumper::Vi0 + (s[2] - '0'));
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "connect";
        x.help    = "The endpoint on the other end of the line (CONNECT sets this)";
        x.kind    = Kind::Str;
        x.get     = [this] { return Value::ofStr(stream_->describe()); };
        x.set     = [this](const Value& v, std::string& err) {
            if (!g_resolver) {
                err = "no endpoint resolver installed";
                return false;
            }
            auto s = g_resolver(v.s(), err);
            if (!s) return false;
            connect(std::move(s));
            return true;
        };
        p.push_back(std::move(x));
    }

    // The transform chain, verbatim -- upper, strip7in, crlf, bsdel and the rest
    // (DESIGN.md 7.2). They are the FILTER's properties, not the board's, and the
    // board simply passes them through. Which is why `SET sio2a:b UPPER=ON` works
    // on a socket exactly as it does on the console, with no code here.
    for (Property& f : filter_->properties()) p.push_back(std::move(f));

    return p;
}

// ---------------------------------------------------------------------------
// Sio2Board
// ---------------------------------------------------------------------------

Sio2Board::Sio2Board() {
    a_.disconnect();  // -> NullStream. There is no null pointer in the stream path.
    b_.disconnect();
}

Sio2Board::~Sio2Board() {
    // The queue is holding a lambda with `this` inside it. A card can be pulled out
    // of a RUNNING machine (`BOARDS REMOVE sio0`), and a deadline that fires into a
    // destroyed board is a use-after-free with a two-week fuse on it.
    if (clock_) clock_->cancel(wake_);
}

// Four ports: BA+0..BA+3. Channel A is BA+0 (control/status) and BA+1 (data);
// channel B is BA+2 and BA+3.
bool Sio2Board::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    uint8_t p = c.port();
    return p >= base_ && p < (uint8_t)(base_ + 4);
}

// A card in a backplane always has a clock -- Machine::add() attaches one before
// the board is ever on the bus. But Bus::attach() is public, so a board CAN be
// wired up without a machine around it, and a UART with no clock would dereference
// a null pointer rather than say anything useful. An unclocked 6850 is a chip with
// no crystal: it cannot receive and it cannot time a character, so it reads as a
// dead card rather than taking the process down.
static Clock& deadCard() {
    static Clock stopped;
    return stopped;
}

uint8_t Sio2Board::read(const BusCycle& c) {
    Clock&  clk = clock_ ? *clock_ : deadCard();
    uint8_t off = (uint8_t)(c.port() - base_);
    Acia&   ch  = (off < 2) ? a_ : b_;
    uint8_t v   = (off & 1) ? ch.readData(clk) : ch.readStatus(clk);
    refresh();  // reading the data register CLEARS RDRF -- and with it, the interrupt
    return v;
}

void Sio2Board::write(const BusCycle& c) {
    Clock&  clk = clock_ ? *clock_ : deadCard();
    uint8_t off = (uint8_t)(c.port() - base_);
    Acia&   ch  = (off < 2) ? a_ : b_;
    if (off & 1) ch.writeData(c.data, clk);
    else ch.writeControl(c.data, clk);
    refresh();  // a character went out (TDRE falls), or the interrupt enables moved
}

// PIN 73, COMBINATIONAL AND PURE. This card pulls it if either chip is asking AND
// that chip's jumper actually goes to pin 73. A `vi*` jumper goes to a vectored
// interrupt line instead, which nothing watches until an 88-VI card exists -- so it
// correctly does nothing, exactly as a wire into an empty slot would.
//
// No work happens here. It reads two chips' pins and ORs them. The receiver used to
// be advanced in this function -- see refresh(), which is where that went, and
// board.h, which explains why it had to move.
bool Sio2Board::assertsInt() const {
    if (!clock_) return false;  // no crystal: the chips are not running at all
    if (a_.jumper == IrqJumper::Int && a_.irq(*clock_)) return true;
    if (b_.jumper == IrqJumper::Int && b_.irq(*clock_)) return true;
    return false;
}

// ---------------------------------------------------------------------------
// THE CARD'S OWN CLOCK. This is the heart of the board now.
//
// THE RECEIVERS ARE ADVANCED HERE, and that is the load-bearing line. An
// interrupt-driven driver NEVER reads the status port -- being interrupt-driven is
// precisely so that it does not have to -- so a 6850 that only ingested a character
// when the guest looked at a register would never ingest one, never raise IRQ, and
// the operator could type forever with nothing happening. The receive shift
// register fills on the 6850's own clock and owes the CPU nothing.
//
// That used to happen inside assertsInt(), which the bus called once per
// instruction. It worked, and it was backwards: no backplane interrogates a card
// for its interrupt status. So the card now runs on its own -- on a deadline it
// sets for itself, and on pump(), which is where the host's keystrokes get in --
// and the bus simply reads the wire the card is pulling.
//
// Re-arming from scratch every time, rather than tracking which deadline changed,
// is deliberate: there is exactly one outstanding timer per card, it is always the
// earliest edge of the two chips, and a scheme that cannot leak a timer is worth
// more than one that saves a heap push per character.
// ---------------------------------------------------------------------------
void Sio2Board::refresh() {
    if (!clock_) return;

    a_.poll(*clock_);
    b_.poll(*clock_);

    intChanged();  // drive pin 73 -- the bus is not going to come and ask

    clock_->cancel(wake_);
    wake_ = Clock::kNone;

    uint64_t ea   = a_.nextEdge(*clock_);
    uint64_t eb   = b_.nextEdge(*clock_);
    uint64_t next = !ea ? eb : (!eb ? ea : (ea < eb ? ea : eb));

    // Usually there is no edge coming and we set no timer at all: a quiet line with
    // the transmitter idle has nothing whatever to do next. The old model paid the
    // full price of a poll for precisely this, the commonest case in the machine.
    if (next) wake_ = clock_->at(next, [this] { refresh(); });
}

void Sio2Board::reset(Reset) {
    // Both resets do a 6850 master reset on both chips, and both KEEP THE
    // ENDPOINTS CONNECTED (docs/boards/mits-2sio.md).
    if (!clock_) return;
    a_.masterReset(*clock_);
    b_.masterReset(*clock_);

    // ...and refresh() CANCELS the outstanding deadline before re-arming, which is
    // why wake_ must not be cleared here. POWER empties the queue under us, but
    // RESET* does not: a character was going out when the button was pressed, and
    // its alarm is still on the books. Zeroing the handle first would orphan it --
    // the timer would still fire, into a chip that has been master-reset out from
    // under it. A leaked deadline is a quiet bug; the cancel is not optional.
    refresh();
}

void Sio2Board::power() { reset(Reset::PowerOn); }

// THE ONE DOOR THE OUTSIDE WORLD COMES THROUGH (DESIGN.md 7.1). A character
// arriving from the host is not a deadline -- nothing in emulated time predicted
// it -- so no timer could have been set for it. This is where it gets in, and it is
// why the answer to "event queue, or periodic timer?" turned out to be both.
void Sio2Board::pump() {
    a_.pump();
    b_.pump();
    refresh();
}

// A jumper moved: `interrupt` (which wire the IRQ is soldered to), `baud` (how long
// a character takes, so every deadline we have set is now aimed at the wrong
// T-state), or `connect` (a new line, which may already have something on it).
void Sio2Board::configChanged() {
    decodeChanged();  // `port` may have moved the card in the I/O space
    refresh();        // ...and refresh() re-drives the pin and re-aims the timer
}

Acia* Sio2Board::channel(const std::string& name) {
    if (name == "a") return &a_;
    if (name == "b") return &b_;
    return nullptr;
}

std::vector<Property> Sio2Board::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name    = "port";
        x.help    = "Base address. The card decodes four ports: BASE+0 .. BASE+3";
        x.kind    = Kind::Int;
        x.radix   = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min     = 0;
        x.max     = 0xFC;
        x.get     = [this] { return Value::ofInt(base_); };
        x.set     = [this](const Value& v, std::string&) {
            base_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<Property> Sio2Board::unitProperties(const std::string& unit) {
    if (Acia* ch = channel(unit)) return ch->properties();
    return {};
}

std::vector<UnitDef> Sio2Board::units() const {
    return {
        {"a", UnitKind::Serial, a_.endpoint()},
        {"b", UnitKind::Serial, b_.endpoint()},
    };
}

std::vector<MapEntry> Sio2Board::ioMap() const {
    return {
        {(uint32_t)base_, (uint32_t)base_ + 1, "read/write", "6850 'a' -- status/control, data"},
        {(uint32_t)base_ + 2, (uint32_t)base_ + 3, "read/write", "6850 'b' -- status/control, data"},
    };
}

bool Sio2Board::connect(const std::string& unit, const std::string& endpoint, std::string& err) {
    Acia* ch = channel(unit);
    if (!ch) {
        err = "2sio has no unit '" + unit + "' -- it has 'a' and 'b'";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    auto s = g_resolver(endpoint, err);
    if (!s) return false;
    ch->connect(std::move(s));
    refresh();  // a new line, and it may already have something waiting on it
    return true;
}

bool Sio2Board::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
    if (table != "unit") {
        err = "2sio has no [[board." + table + "]] table";
        return false;
    }

    // Which channel? The loader put it here from the table's dotted name.
    std::string which;
    for (const auto& [k, v] : kv)
        if (k == "unit") which = v;

    Acia* ch = channel(which);
    if (!ch) {
        err = which.empty() ? "which channel? use [board.unit.a] or [board.unit.b]"
                            : "2sio has no channel '" + which + "' -- it has 'a' and 'b'";
        return false;
    }

    // Everything else is a property, set through the ONE property path -- same
    // parser, same radix rule, same validation as `SET sio0:a BAUD=9600` types at
    // the monitor. A config file cannot set something the monitor would refuse.
    for (const auto& [k, v] : kv) {
        if (k == "unit") continue;
        if (!setUnitProperty(*this, which, k, v, err)) return false;
    }
    return true;
}

bool Sio2Board::disconnect(const std::string& unit, std::string& err) {
    Acia* ch = channel(unit);
    if (!ch) {
        err = "2sio has no unit '" + unit + "' -- it has 'a' and 'b'";
        return false;
    }
    ch->disconnect();
    refresh();  // the line went dead: no more characters are coming off it
    return true;
}

} // namespace altair
