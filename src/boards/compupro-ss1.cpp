#include "boards/compupro-ss1.h"

#include "core/bus.h"
#include "core/clock.h"
#include "core/statefile.h"
#include "core/value.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <utility>

namespace altair {

namespace {

EndpointResolver g_resolver;

// A card in a backplane always has a clock, but Bus::attach() is public, so a board CAN
// be wired up without a machine around it. A UART with no clock is a chip with no
// crystal: it reads as a dead card rather than dereferencing a null pointer. (Same idiom
// as SbcBoard::deadCard.)
Clock& deadCard() {
    static Clock stopped;
    return stopped;
}

}  // namespace

void Ss1Board::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

Ss1Board::Ss1Board() {
    // A card with nothing plugged into its serial connector has a DEAD line (a NullStream),
    // never a dangling one -- so programLine()/driveControl() have a stream to talk to
    // before any CONNECT. (Same idiom as SbcBoard's constructor.)
    uart_.disconnect();
}

// A card can be pulled from a RUNNING machine; a deadline that fires into a destroyed
// board is a use-after-free with a long fuse. (Same idiom as SbcBoard::~SbcBoard.)
Ss1Board::~Ss1Board() {
    if (clock_) clock_->cancel(wake_);
}

// ---------------------------------------------------------------------------
// THE DECODE. The three 8253 timer counters + its control word (Phase 3), two RTC ports
// (Phase 1) and the four UART ports (Phase 2). The clock command register and the timer
// control word are write-only; the clock data register and the three timer counters are
// read/write. The UART's four ports are all read/write except status (+13, read-only) and
// mode (+14, read/write with an internal MR1/MR2 pointer). Everything else in the 16-port
// block floats -- which is exactly right for the not-yet-landed 8259A and the empty math
// socket.
bool Ss1Board::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type == Cycle::IoWrite) {
        uint8_t p = c.port();
        return p == timer0Port() || p == timer1Port() || p == timer2Port() ||
               p == timerCtlPort() || p == clockCmdPort() || p == clockDataPort() ||
               p == uartDataPort() || p == uartModePort() || p == uartCmdPort();
        // NB: the UART status port (+13) is not written (the write address is the SYN
        // register, which this board never uses), so it is not decoded for a write.
    }
    if (c.type == Cycle::IoRead) {
        uint8_t p = c.port();
        return p == timer0Port() || p == timer1Port() ||
               p == timer2Port() ||  // the timer control word is write-only
               p == clockDataPort() ||  // clock command is write-only
               p == uartDataPort() || p == uartStatusPort() || p == uartModePort() ||
               p == uartCmdPort();
    }
    return false;
}

uint8_t Ss1Board::read(const BusCycle& c) {
    if (c.type != Cycle::IoRead) return 0xFF;
    uint8_t p = c.port();
    if (p == clockDataPort()) return rtc_.readData();

    const Clock& clk = clock_ ? *clock_ : deadCard();

    // The 8253 counter reads are pure (they derive the count from the clock and touch
    // only the read-pointer/latch state), and the timer drives no interrupt yet, so
    // they do not go through refresh().
    if (p == timer0Port()) return timer_.readCounter(0, clk);
    if (p == timer1Port()) return timer_.readCounter(1, clk);
    if (p == timer2Port()) return timer_.readCounter(2, clk);

    uint8_t v = 0xFF;
    if (p == uartDataPort())        v = uart_.readData(clk);    // clears RxRDY
    else if (p == uartStatusPort()) v = uart_.readStatus(clk);
    else if (p == uartModePort())   v = uart_.readMode();
    else if (p == uartCmdPort())    v = uart_.readCommand();
    else                            return 0xFF;                // not a UART port
    refresh();  // reading data cleared RxRDY; the read may have advanced the receiver
    return v;
}

void Ss1Board::write(const BusCycle& c) {
    if (c.type != Cycle::IoWrite) return;
    uint8_t p = c.port();

    if (p == clockCmdPort()) {
        rtc_.writeCommand(c.data);
        return;
    }
    if (p == clockDataPort()) {
        rtc_.writeData(c.data);
        return;
    }

    const Clock& clk = clock_ ? *clock_ : deadCard();

    // The 8253. Its OUT lines feed no interrupt until Phase 4, so programming it does
    // not move pin 73 -- no refresh() needed here.
    if (p == timerCtlPort()) { timer_.writeControl(c.data, clk); return; }
    if (p == timer0Port())   { timer_.writeCounter(0, c.data, clk); return; }
    if (p == timer1Port())   { timer_.writeCounter(1, c.data, clk); return; }
    if (p == timer2Port())   { timer_.writeCounter(2, c.data, clk); return; }

    if (p == uartDataPort())      uart_.writeData(c.data, clk);     // transmit
    else if (p == uartModePort()) uart_.writeMode(c.data);          // MR1 then MR2
    else if (p == uartCmdPort())  uart_.writeCommand(c.data, clk);  // TxEN/RxEN/DTR/RTS/...
    refresh();
}

// ---------------------------------------------------------------------------
// THE 2651's INTERRUPT WIRE, through the `interrupt` unit jumper. On the standard board
// RxRDY/TxRDY feed the on-board 8259A (Phase 4); until that lands, the jumper lets a
// machine route the receive interrupt (RxRDY) straight to pINT or an S-100 VI line.
// Default `none` -> nothing asserts, so a stock config stays silent. Pure and
// combinational, like every assertsInt()/assertsVi() (DESIGN.md 4.4.1).
bool Ss1Board::assertsInt() const {
    return clock_ && uart_.jumper == IrqJumper::Int && uart_.rxReady();
}

uint8_t Ss1Board::assertsVi() const {
    if (!clock_ || !uart_.rxReady()) return 0;
    return viBit(uart_.jumper);
}

// The MSM5832 is battery-backed: neither the front-panel RESET nor a power cycle changes
// the time it keeps -- both events only clear the board-side command latches. The UART's
// RESET pin is not driven from the backplane (the monitor always software-programs the
// chip out of reset); a bus reset just re-polls and re-arms, power-on puts it in its
// known-good state. (Same stance, same reasoning, as SbcBoard::reset.)
void Ss1Board::reset(Reset r) {
    rtc_.reset();
    if (!clock_) return;
    if (r == Reset::PowerOn) {
        uart_.powerOn(*clock_);
        timer_.powerOn(*clock_);  // the 8253 has no bus-reset pin either -- power-on only
    }
    refresh();  // do NOT clear wake_ first: a bus reset does not empty the queue.
}

void Ss1Board::power() { reset(Reset::PowerOn); }

// THE ONE DOOR THE OUTSIDE WORLD COMES THROUGH (DESIGN.md 7.1).
void Ss1Board::pump() {
    uart_.pump();
    refresh();
}

// A strap moved: `base` (moves the block in I/O space) or a unit property (`baud` changes
// how long a character takes, so every deadline is aimed at the wrong T-state; `connect`
// is a new line, possibly with something already on it).
void Ss1Board::configChanged() {
    decodeChanged();
    uart_.programLine();
    refresh();
}

// ---------------------------------------------------------------------------
// THE CARD'S OWN CLOCK (DESIGN.md 7.5). The receiver is advanced, the interrupt wire is
// re-driven (the bus is not going to come and ask), and one alarm is armed for the next
// moment the chip changes with nobody touching it -- a receive frame completing (RxRDY
// rises, which may raise the interrupt), or the transmitter draining.
// ---------------------------------------------------------------------------
void Ss1Board::refresh() {
    if (!clock_) return;

    uart_.poll(*clock_);
    intChanged();  // RxRDY may have moved -- re-drive the interrupt wire

    clock_->cancel(wake_);
    wake_ = Clock::kNone;
    if (uint64_t next = nextEdge()) wake_ = clock_->at(next, [this] { refresh(); });
}

uint64_t Ss1Board::nextEdge() const {
    const Clock& clk = clock_ ? *clock_ : deadCard();

    uint64_t best = 0;
    auto consider = [&](uint64_t when) {
        if (when <= clk.now()) return;  // already past: it is already showing in status
        if (!best || when < best) best = when;
    };

    if (uart_.rxFrameActive())      consider(uart_.rxDoneAt());
    else if (uart_.rxWaiting())     consider(uart_.rxNextAt());
    consider(uart_.txFreeAt());

    return best;
}

void Ss1Board::serialize(StateWriter& w) const {
    Board::serialize(w);
    rtc_.serialize(w);
    uart_.serialize(w);
    timer_.serialize(w);
}

void Ss1Board::deserialize(StateReader& r) {
    Board::deserialize(r);
    rtc_.deserialize(r);
    uart_.deserialize(r);
    timer_.deserialize(r);
    refresh();  // re-drive the interrupt wire and re-arm the deadline from restored state
}

std::vector<std::string> Ss1Board::drainLog() { return uart_.drainLog(); }

std::vector<Property> Ss1Board::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name = "base";
        x.help = "Base port of the 16-port I/O block. CompuPro standard is 50H";
        x.kind = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min = 0;
        x.max = 0xF0;  // the block is 16 ports wide; base+15 must stay under 0x100
        x.get = [this] { return Value::ofInt(base_); };
        x.set = [this](const Value& v, std::string&) {
            base_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        // LIVE, read-only: the time the clock is showing right now (host time plus
        // whatever the guest last set). No setter -- the guest sets the clock by
        // programming the MSM5832, not by a monitor SET, so CONFIG SAVE skips this.
        Property x;
        x.name = "clock";
        x.help = "LIVE: the date/time the MSM5832 is showing, and its offset from host time";
        x.kind = Kind::Str;
        x.get = [this] { return Value::ofStr(rtc_.describe()); };
        p.push_back(std::move(x));
    }
    {
        // LIVE, read-only: each 8253 counter's mode, current count and OUT level. The
        // guest programs the timer through its ports, not through a monitor SET.
        Property x;
        x.name = "timer";
        x.help = "LIVE: the three 8253 counters -- mode, current count and OUT level";
        x.kind = Kind::Str;
        x.get = [this] {
            const Clock& clk = clock_ ? *clock_ : deadCard();
            return Value::ofStr(timer_.describe(clk));
        };
        p.push_back(std::move(x));
    }
    return p;
}

// One serial unit -- the 2651 channel. The base is a board property; the line's rate,
// interrupt jumper and endpoint are unit properties.
std::vector<UnitDef> Ss1Board::units() const {
    return {{"serial", UnitKind::Serial, uart_.endpoint()}};
}

std::vector<Property> Ss1Board::unitProperties(const std::string& unit) {
    if (unit != "serial") return {};
    // The 2651's own `connect` property setter opens the endpoint, so it -- like the
    // board's connect() -- must rebase a relative in:/out: PATH from a machine file.
    return uart_.properties(
        rebasingResolver(g_resolver, [this](const std::string& p) { return resolvePath(p); }));
}

std::vector<MapEntry> Ss1Board::ioMap() const {
    return {
        {timer0Port(), timer0Port(), "read/write", "8253 timer -- counter 0"},
        {timer1Port(), timer1Port(), "read/write", "8253 timer -- counter 1"},
        {timer2Port(), timer2Port(), "read/write", "8253 timer -- counter 2"},
        {timerCtlPort(), timerCtlPort(), "write", "8253 timer -- control word"},
        {uartDataPort(), uartDataPort(), "read/write", "2651 UART -- receive/transmit data"},
        {uartStatusPort(), uartStatusPort(), "read", "2651 UART -- status"},
        {uartModePort(), uartModePort(), "read/write", "2651 UART -- mode registers 1/2"},
        {uartCmdPort(), uartCmdPort(), "read/write", "2651 UART -- command"},
        {clockCmdPort(), clockCmdPort(), "write", "MSM5832 clock -- command"},
        {clockDataPort(), clockDataPort(), "read/write", "MSM5832 clock -- data"},
    };
}

bool Ss1Board::connect(const std::string& unit, const std::string& ep, std::string& err) {
    if (unit != "serial") {
        err = "ss1 has no unit '" + unit + "' -- its serial channel is called 'serial'";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    // A machine-file in:/out: PATH is relative to the machine file; rebase the copy the
    // resolver opens (rebaseEndpointPaths knows the grammar). The chip echoes describe()
    // for its `connect` property, so SHOW/CONFIG SAVE report the resolved path.
    std::vector<std::string> paths;
    std::string spec = rebaseEndpointPaths(ep, [&](const std::string& p) {
        paths.push_back(p);
        return resolvePath(p);
    });
    auto s = g_resolver(spec, err);
    if (!s) {
        for (const std::string& p : paths) err += pathNote(p);
        return false;
    }
    uart_.connect(std::move(s));
    refresh();  // a new line, and it may already have something waiting on it
    return true;
}

bool Ss1Board::disconnect(const std::string& unit, std::string& err) {
    if (unit != "serial") {
        err = "ss1 has no unit '" + unit + "' -- its serial channel is called 'serial'";
        return false;
    }
    uart_.disconnect();
    refresh();
    return true;
}

}  // namespace altair
