#include "boards/sd-sbc.h"

#include "core/clock.h"
#include "core/statefile.h"
#include "host/stream.h"

#include <utility>

namespace altair {

namespace {

EndpointResolver g_resolver;

// A card in a backplane always has a clock, but Bus::attach() is public, so a board
// CAN be wired up without a machine around it. A USART with no clock is a chip with no
// crystal: it reads as a dead card rather than dereferencing a null pointer. (Same
// idiom as SioBoard::deadCard.)
Clock& deadCard() {
    static Clock stopped;
    return stopped;
}

} // namespace

void SbcBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

SbcBoard::SbcBoard() {
    u_.disconnect();  // -> NullStream: a card with nothing plugged in has a DEAD line,
                      // never a dangling one.
    // THE DEFINING STRAP: RxD is soldered to /DSR, so the monitor's auto-baud can watch
    // the serial line in status bit 7. This is the etch default; the `rxd2dsr` board
    // property flips it. See reference/SD Systems SBC-100 & SBC-200.md.
    u_.dsrSrc = DsrSource::FollowRxD;
}

SbcBoard::~SbcBoard() {
    // A card can be pulled from a RUNNING machine; a deadline that fires into a
    // destroyed board is a use-after-free with a two-week fuse on it.
    if (clock_) clock_->cancel(wake_);
}

// ---------------------------------------------------------------------------
// The bus: two ports. Data at base_, status(read)/command(write) at base_+1. This is
// the 8251 orientation -- data LOW, status HIGH -- and it is the REVERSE of the 6850
// section (Sio2Port), which is exactly why this card does not reuse it.
// ---------------------------------------------------------------------------
bool SbcBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    uint8_t p = c.port();
    return p == base_ || p == (uint8_t)(base_ + 1);
}

uint8_t SbcBoard::read(const BusCycle& c) {
    const Clock& clk = clock_ ? *clock_ : deadCard();
    uint8_t v = (c.port() == base_) ? u_.readData(clk) : u_.readStatus(clk);
    refresh();  // reading data cleared RxRDY; either read advanced the receiver
    return v;
}

void SbcBoard::write(const BusCycle& c) {
    const Clock& clk = clock_ ? *clock_ : deadCard();
    if (c.port() == base_) {
        u_.writeData(c.data, clk);      // the DATA port -> transmit
    } else {
        u_.writeControl(c.data, clk);   // the CONTROL port -> mode, then commands
    }
    refresh();
}

// ---------------------------------------------------------------------------
// THE CARD'S OWN CLOCK (DESIGN.md 7.5). The receiver is advanced here, and one alarm
// is armed for the next moment the chip changes with nobody touching it -- a receive
// frame completing (RxRDY rises), or the transmitter draining. No interrupt wire is
// driven in Phase 1, so there is no intChanged() yet.
// ---------------------------------------------------------------------------
void SbcBoard::refresh() {
    if (!clock_) return;

    u_.poll(*clock_);

    clock_->cancel(wake_);
    wake_ = Clock::kNone;
    if (uint64_t next = nextEdge()) wake_ = clock_->at(next, [this] { refresh(); });
}

uint64_t SbcBoard::nextEdge() const {
    const Clock& clk = clock_ ? *clock_ : deadCard();

    uint64_t best = 0;
    auto consider = [&](uint64_t when) {
        if (when <= clk.now()) return;  // already past: it is already showing in status
        if (!best || when < best) best = when;
    };

    if (u_.rxFrameActive()) {
        consider(u_.rxDoneAt());      // the frame will finish and raise RxRDY on its own
    } else if (u_.rxWaiting()) {
        consider(u_.rxNextAt());      // a queued byte will start its frame at this T-state
    }
    consider(u_.txFreeAt());          // the transmitter drains on its own clock

    return best;
}

// ---------------------------------------------------------------------------
// RESET. The 8251's RESET pin is not driven from the backplane here: the monitor
// always software-programs the chip (mode then command) out of reset, so nothing
// period-correct can tell. A bus reset just re-polls and re-arms; power-on puts the
// chip in its known-good state. (Same stance, and same reasoning, as SioBoard.)
// ---------------------------------------------------------------------------
void SbcBoard::reset(Reset r) {
    if (!clock_) return;
    if (r == Reset::PowerOn) u_.powerOn(*clock_);
    refresh();  // do NOT clear wake_ first: a bus reset does not empty the queue, and a
                // character still leaving has an alarm on the books.
}

void SbcBoard::power() { reset(Reset::PowerOn); }

// THE ONE DOOR THE OUTSIDE WORLD COMES THROUGH (DESIGN.md 7.1).
void SbcBoard::pump() {
    u_.pump();
    refresh();
}

// A strap moved: `port` (moves the card in I/O space) or a unit property (`baud`
// changes how long a character takes, so every deadline is now aimed at the wrong
// T-state; `connect` is a new line, possibly with something already on it).
void SbcBoard::configChanged() {
    decodeChanged();
    u_.programLine();
    refresh();
}

// ---------------------------------------------------------------------------
// Snapshot: the chip's live state travels; the straps (variant/base) are config.
// ---------------------------------------------------------------------------
void SbcBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    u_.serialize(w);
}

void SbcBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    u_.deserialize(r);
    refresh();  // re-arm the deadline from the restored state
}

// ---------------------------------------------------------------------------
// Reflection
// ---------------------------------------------------------------------------
uint8_t SbcBoard::statusByte() const {
    return u_.statusByte(clock_ ? *clock_ : deadCard());
}

std::vector<Property> SbcBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name    = "variant";
        x.help    = "Which board: sbc100 (2.4576 MHz) or sbc200 (4 MHz). Inert in this "
                    "phase -- the serial section is the same 8251 on both";
        x.kind    = Kind::Enum;
        x.choices = {"sbc100", "sbc200"};
        x.get     = [this] {
            return Value::ofStr(variant_ == Variant::Sbc100 ? "sbc100" : "sbc200");
        };
        x.set = [this](const Value& v, std::string&) {
            variant_ = (v.s() == "sbc100") ? Variant::Sbc100 : Variant::Sbc200;
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        // THE RxD->/DSR JUMPER, AND IT IS A STRAP ON THIS CARD. The SBC-200 solders the
        // 8251's receive-data line to its /DSR input so MSMONR21 can auto-detect baud by
        // timing the start bit in status bit 7. On by default (that is the etch); turn it
        // off for a plain 8251 console, or to run a monitor that does not auto-baud.
        Property x;
        x.name = "rxd2dsr";
        x.help = "RxD strapped to /DSR (the SBC auto-baud jumper). Off = a plain 8251 /DSR";
        x.kind = Kind::Bool;
        x.get  = [this] { return Value::ofBool(u_.dsrSrc == DsrSource::FollowRxD); };
        x.set  = [this](const Value& v, std::string&) {
            u_.dsrSrc = v.b() ? DsrSource::FollowRxD : DsrSource::Inactive;
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "port";
        x.help  = "Base I/O address (a card jumper). Data at BASE, status/command at "
                  "BASE+1. The etch default is 7C";
        x.kind  = Kind::Int;
        x.radix = 16;  // on the wire -> hex (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xFE;
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string&) {
            base_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

// One serial unit. The card has one USART and one connector; every other jumper is a
// board property. The unit exists because CONNECT names one.
std::vector<UnitDef> SbcBoard::units() const {
    return {{"tty", UnitKind::Serial, u_.endpoint()}};
}

std::vector<Property> SbcBoard::unitProperties(const std::string& unit) {
    if (unit != "tty") return {};
    return u_.properties(g_resolver);
}

std::vector<MapEntry> SbcBoard::ioMap() const {
    return {
        {(uint32_t)base_, (uint32_t)base_, "read/write", "8251 -- receive/transmit data"},
        {(uint32_t)base_ + 1, (uint32_t)base_ + 1, "read/write",
         "8251 -- status (read) / mode+command (write)"},
    };
}

bool SbcBoard::connect(const std::string& unit, const std::string& ep, std::string& err) {
    if (unit != "tty") {
        err = "sbc has no unit '" + unit + "' -- it has one, and it is called 'tty'";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    auto s = g_resolver(ep, err);
    if (!s) return false;
    u_.connect(std::move(s));
    refresh();  // a new line, and it may already have something waiting on it
    return true;
}

bool SbcBoard::disconnect(const std::string& unit, std::string& err) {
    if (unit != "tty") {
        err = "sbc has no unit '" + unit + "' -- it has one, and it is called 'tty'";
        return false;
    }
    u_.disconnect();
    refresh();
    return true;
}

} // namespace altair
