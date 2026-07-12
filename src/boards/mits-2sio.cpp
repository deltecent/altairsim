#include "boards/mits-2sio.h"

#include "host/stream.h"

#include <utility>

namespace altair {

// WHERE THE ENDPOINT GRAMMAR STOPS. The program installs this (src/main.cpp);
// the card holds it and hands it down to the chips. Neither the card nor the
// 6850 knows what a socket is, and this is the whole of the arrangement that
// keeps it that way (DESIGN.md 7.7).
namespace {
Sio2Board::EndpointResolver g_resolver;
} // namespace

void Sio2Board::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

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
    Mc6850&   ch  = (off < 2) ? a_ : b_;
    uint8_t v   = (off & 1) ? ch.readData(clk) : ch.readStatus(clk);
    refresh();  // reading the data register CLEARS RDRF -- and with it, the interrupt
    return v;
}

void Sio2Board::write(const BusCycle& c) {
    Clock&  clk = clock_ ? *clock_ : deadCard();
    uint8_t off = (uint8_t)(c.port() - base_);
    Mc6850&   ch  = (off < 2) ? a_ : b_;
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

// ---------------------------------------------------------------------------
// A BUS RESET DOES NOTHING TO THIS CARD, AND THE DATA SHEET SETTLES IT: THERE IS NO PIN.
//
// The MC6850's twenty-four pins are Vss, RxD, RxCLK, TxCLK, RTS, TxD, IRQ, CS0-CS2, RS,
// Vcc, R/W, E, D0-D7, /DCD and /CTS. No RESET. So the S-100 RESET* line reaches this
// card's address decoding and NOTHING ELSE -- there is no wire to run it down, and no
// card circuitry could fake one short of forging a bus write. A 6850 is reset by the
// GUEST, by writing 11 into the divide field, and by nothing else in the world.
//
// This used to reset both chips on BOTH kinds of reset, and the difference is not
// academic: hit RESET on a running machine and the 2SIO would lose its word format, its
// RTS, its interrupt enables and any character sitting in the receive register. On the
// real card it loses NOTHING, and a monitor that reset the machine and then read the
// console port got back a byte that should still have been there. (DESIGN.md 0.1: the
// data sheet wins. It won.)
//
// POWER-ON-CLEAR is different -- the machine was switched ON, and a card coming up must
// be usable at once (DESIGN.md 6.1), so the chips get put in a known good state.
//
// refresh() runs EITHER WAY, because pin 73 is the CARD's: the backplane's interrupt
// wire has to be re-driven whether or not anything happened to the chips.
// ---------------------------------------------------------------------------
void Sio2Board::reset(Reset r) {
    if (!clock_) return;

    if (r == Reset::PowerOn) {
        a_.powerOn(*clock_);
        b_.powerOn(*clock_);
    }

    // ...and refresh() CANCELS the outstanding deadline before re-arming, which is
    // why wake_ must not be cleared here. POWER empties the queue under us, but
    // RESET* does not: a character was going out when the switch was hit, and its
    // alarm is still on the books. Zeroing the handle first would orphan it -- the
    // timer would still fire, into a chip whose transmitter had been reinitialized
    // under it. A leaked deadline is a quiet bug; the cancel is not optional.
    refresh();
}

void Sio2Board::power() { reset(Reset::PowerOn); }

// What the chips have to say -- and today there is exactly one thing either of them
// CAN say: "this cable cannot do that baud rate". The card says it, rather than
// running the wire at a speed nobody chose, in silence. (Board::drainLog() is
// virtual as of 59a175b; before that, a card that was not the memory card had no
// way to speak at all.)
std::vector<std::string> Sio2Board::drainLog() {
    auto out = a_.drainLog();
    for (auto& s : b_.drainLog()) out.push_back(std::move(s));
    for (auto& s : out) s = id + ":" + s;
    return out;
}

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

Mc6850* Sio2Board::channel(const std::string& name) {
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
    if (Mc6850* ch = channel(unit)) return ch->properties(g_resolver);
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
    Mc6850* ch = channel(unit);
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

    Mc6850* ch = channel(which);
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
    Mc6850* ch = channel(unit);
    if (!ch) {
        err = "2sio has no unit '" + unit + "' -- it has 'a' and 'b'";
        return false;
    }
    ch->disconnect();
    refresh();  // the line went dead: no more characters are coming off it
    return true;
}

} // namespace altair
