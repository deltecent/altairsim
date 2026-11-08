#include "boards/compupro-ss1.h"

#include "core/bus.h"
#include "core/clock.h"
#include "core/statefile.h"
#include "core/value.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <algorithm>
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

// The 8253's counter clock: the bus CLOCK, pin 49, in real time (core/busclock.h). The
// SS-1 ties all three counter inputs to it (§2.7). No clock or no bus: a dead card, 0.
uint64_t Ss1Board::pin49() const {
    return clock_ && bus_ ? bus_->busClock().now(*clock_) : 0;
}

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
// THE DECODE. The dual 8259A (Phase 4), the three 8253 timer counters + control word
// (Phase 3), two RTC ports (Phase 1) and the four UART ports (Phase 2). The clock
// command register and the timer control word are write-only; the clock data register
// and the three timer counters are read/write. The UART's four ports are all read/write
// except status (+13, read-only) and mode (+14, read/write with an internal MR1/MR2
// pointer). The 8259A ports are both read (IRR/ISR/IMR) and write. The math socket's two
// ports are read/write with a chip fitted; empty, they float (the stock board), as does
// the UART status port on a write.
//
// AND THE IntAck CYCLE, but only when the master would actually interrupt OR a CALL
// fetch is already in progress: once the acknowledge starts, the board must keep
// claiming the cycle to drive all three CALL bytes even though the first one set the
// in-service bit that drops the master's INT. Pure -- intaByte_ is read, not moved.
bool Ss1Board::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if ((c.type == Cycle::IoWrite || c.type == Cycle::IoRead) && mathFitted() &&
        (c.port() == mathDataPort() || c.port() == mathCmdPort()))
        return true;
    if (c.type == Cycle::IoWrite) {
        uint8_t p = c.port();
        return p == picMaster0Port() || p == picMaster1Port() || p == picSlave0Port() ||
               p == picSlave1Port() || p == timer0Port() || p == timer1Port() ||
               p == timer2Port() || p == timerCtlPort() || p == clockCmdPort() ||
               p == clockDataPort() || p == uartDataPort() || p == uartModePort() ||
               p == uartCmdPort();
        // NB: the UART status port (+13) is not written (the write address is the SYN
        // register, which this board never uses), so it is not decoded for a write.
    }
    if (c.type == Cycle::IoRead) {
        uint8_t p = c.port();
        return p == picMaster0Port() || p == picMaster1Port() || p == picSlave0Port() ||
               p == picSlave1Port() || p == timer0Port() || p == timer1Port() ||
               p == timer2Port() ||  // the timer control word is write-only
               p == clockDataPort() ||  // clock command is write-only
               p == uartDataPort() || p == uartStatusPort() || p == uartModePort() ||
               p == uartCmdPort();
    }
    if (c.type == Cycle::IntAck)
        return intaByte_ != 0 || (clock_ && master_.intOut(masterLive()));
    return false;
}

// ---------------------------------------------------------------------------
// THE INTERRUPT-ACKNOWLEDGE SEQUENCE. In MCS-80/85 mode an 8259A answers INTA with a
// 3-byte CALL: the opcode (0xCD), then the two address bytes. Our CPU cores fetch each
// of those over its own IntAck cycle (see the header), so the card walks a 3-state
// counter and drives one byte per cycle.
//
// On the first byte the master resolves the winner. If the winner is the cascade line
// (IR7 = the slave), the SLAVE supplies the vector -- and both in-service bits are set,
// which is why the handler must EOI both chips. Otherwise the master's own vector is
// used. Setting the in-service bit drops the master's INT, so we re-drive pin 73 here.
//
// A chip with nothing serviceable answers with its DEFAULT IR7 (the IR7 vector, no ISR
// bit). For the slave that is its own IR7 address. A master default IR7 is level 7,
// which is the cascade input here, so it goes to the slave the same way -- the data
// sheet does not spell that corner out. decodes() claims the cycle only while the
// master's INT is live, so from the bus neither default is reached today.
uint8_t Ss1Board::intAckByte() {
    if (intaByte_ == 0) {
        int mlvl = master_.acknowledge(masterLive());

        Intel8259a* src = &master_;
        int         lvl = mlvl;
        if (mlvl == kMasterCascadeIr) {  // cascade: the slave owns this interrupt
            src = &slave_;
            lvl = slave_.acknowledge(slaveLive());
        }
        intChanged();  // the in-service bit just dropped the master's INT off pin 73

        if (master_.is8086()) {  // no 8086 core exists; this path has no real consumer
            intaByte_ = 0;
            return src->vector8086(lvl);
        }
        intaAddr_ = src->callAddress(lvl);
        intaByte_ = 1;
        return 0xCD;  // CALL
    }
    if (intaByte_ == 1) {
        intaByte_ = 2;
        return (uint8_t)(intaAddr_ & 0xFF);
    }
    intaByte_ = 0;
    return (uint8_t)(intaAddr_ >> 8);
}

uint8_t Ss1Board::read(const BusCycle& c) {
    if (c.type == Cycle::IntAck) return intAckByte();
    if (c.type != Cycle::IoRead) return 0xFF;
    uint8_t p = c.port();
    if (p == clockDataPort()) return rtc_.readData();

    // The 8259A registers (IRR/ISR/IMR) read purely from the controller's state and the
    // live request lines -- no interrupt is moved by a read, so no refresh() is needed.
    if (p == picMaster0Port()) return master_.read(false, masterLive());
    if (p == picMaster1Port()) return master_.read(true, masterLive());
    if (p == picSlave0Port())  return slave_.read(false, slaveLive());
    if (p == picSlave1Port())  return slave_.read(true, slaveLive());

    // The 8253 counter reads are pure (they derive the count from the clock and touch
    // only the read-pointer/latch state), and the timer drives no interrupt yet, so
    // they do not go through refresh().
    if (p == timer0Port()) return timer_.readCounter(0, pin49());
    if (p == timer1Port()) return timer_.readCounter(1, pin49());
    if (p == timer2Port()) return timer_.readCounter(2, pin49());

    // The math chip. Any read clears END (slave IR5), so both paths re-sample.
    if (mathFitted() && (p == mathDataPort() || p == mathCmdPort())) {
        uint8_t v = p == mathDataPort() ? chip().readData(mathAccess(true))
                                        : chip().readStatus(mathAccess(false));
        refresh();
        return v;
    }

    const Clock& clk = clock_ ? *clock_ : deadCard();

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

    // The dual 8259A. Programming a controller (a mask write, an EOI, a mode change)
    // can move the master's INT, so every path falls through to refresh().
    if (p == picMaster0Port()) { master_.write(false, c.data); refresh(); return; }
    if (p == picMaster1Port()) { master_.write(true, c.data); refresh(); return; }
    if (p == picSlave0Port())  { slave_.write(false, c.data); refresh(); return; }
    if (p == picSlave1Port())  { slave_.write(true, c.data); refresh(); return; }

    // The 8253. Its OUT lines feed the slave 8259A now (Phase 4), so a programming
    // write can change an interrupt input -- refresh() re-samples and re-arms.
    if (p == timerCtlPort()) { timer_.writeControl(c.data, pin49()); refresh(); return; }
    if (p == timer0Port())   { timer_.writeCounter(0, c.data, pin49()); refresh(); return; }
    if (p == timer1Port())   { timer_.writeCounter(1, c.data, pin49()); refresh(); return; }
    if (p == timer2Port())   { timer_.writeCounter(2, c.data, pin49()); refresh(); return; }

    // The math chip. A command starts BUSY and will set END/SVREQ at its end; refresh()
    // arms the deadline for that edge.
    if (mathFitted() && p == mathDataPort()) { chip().writeData(c.data, mathAccess(true)); refresh(); return; }
    if (mathFitted() && p == mathCmdPort())  { chip().writeCommand(c.data, mathAccess(true)); refresh(); return; }

    const Clock& clk = clock_ ? *clock_ : deadCard();

    if (p == uartDataPort())      uart_.writeData(c.data, clk);     // transmit
    else if (p == uartModePort()) uart_.writeMode(c.data);          // MR1 then MR2
    else if (p == uartCmdPort())  uart_.writeCommand(c.data, clk);  // TxEN/RxEN/DTR/RTS/...
    refresh();
}

// THE MATH CHIP'S WAIT STATES (reference sec. 7.3). The board holds READY for 2 wait states
// on every math access, because PAUSE comes too late to catch the first one; while a
// command runs, PAUSE holds a data access or a command write until the command ends. The
// chip is handed the access at the tick PAUSE releases it, so a command written while
// another runs starts when that one ends. Paced, the hold is exact; flat out the tick is
// host time, which the held T-states do not move, so the wait is the clock's estimate.
uint64_t Ss1Board::mathAccess(bool pauses) {
    uint64_t now  = pin49();
    uint64_t hold = 2;
    if (pauses && now < chip().busyUntil()) {
        if (clock_ && bus_) {
            uint64_t t = bus_->busClock().tStateOf(chip().busyUntil(), *clock_);
            if (t > clock_->now() + hold) hold = t - clock_->now();
        }
        now = chip().busyUntil();
    }
    holdReady((uint32_t)std::min<uint64_t>(hold, UINT32_MAX));
    return now;
}

// ---------------------------------------------------------------------------
// THE IR INPUT LINES, computed LIVE. The 8253 OUTs come off the clock, the VI lines off
// the bus, the UART's ready flags off its own state -- so the controllers never cache a
// request line; they are handed the current levels on every access (see the chip
// header). This is what keeps assertsInt() pure while its inputs move on other clocks.
uint8_t Ss1Board::slaveLive() const {
    const Clock& clk = clock_ ? *clock_ : deadCard();
    uint64_t     now = pin49();
    uint8_t      v   = 0;
    // slave IR0 = S-100 VI7*, through the stock J8 jumper (pins 8-9). Our own VI output is
    // OR'd in for the reason masterLive() gives.
    uint8_t vi = (uint8_t)((bus_ ? bus_->viLines() : 0) | assertsVi());
    if (vi & 0x80) v |= (uint8_t)(1u << kSlaveVi7);
    if (timer_.out(0, now)) v |= (uint8_t)(1u << kSlaveTimer0);  // slave IR1 = Timer 0 OUT
    if (timer_.out(1, now)) v |= (uint8_t)(1u << kSlaveTimer1);  // slave IR2 = Timer 1 OUT
    if (timer_.out(2, now)) v |= (uint8_t)(1u << kSlaveTimer2);  // slave IR3 = Timer 2 OUT
    if (uart_.txReady(clk)) v |= (uint8_t)(1u << kSlaveTxRdy);   // slave IR6 = 2651 TxRDY
    if (uart_.rxReady())    v |= (uint8_t)(1u << kSlaveRxRdy);   // slave IR7 = 2651 RxRDY
    if (mathFitted()) {
        if (chip().svreqOut(now)) v |= (uint8_t)(1u << kSlaveSvrq);  // slave IR4 = math SVRQ
        if (chip().endOut(now))   v |= (uint8_t)(1u << kSlaveEnd);   // slave IR5 = math END (J6)
    }
    return v;
}

uint8_t Ss1Board::masterLive() const {
    // Master IR0-6 = the S-100 VI0-6 wire-OR. OUR OWN VI contribution is OR'd in
    // directly (not just read back from the bus), for the same reason the 88-VI does it
    // (mits-88virtc.cpp): intChanged() settles pin 73 before it publishes our VI output,
    // so for one instant the bus's copy would be missing our own line -- and we would
    // fail to see, through the master, an interrupt the legacy UART jumper just raised.
    uint8_t vi = bus_ ? bus_->viLines() : 0;
    vi |= assertsVi();
    uint8_t v = (uint8_t)(vi & 0x7F);
    if (slave_.intOut(slaveLive())) v |= (uint8_t)(1u << kMasterCascadeIr);  // IR7 = slave
    return v;
}

// ---------------------------------------------------------------------------
// PIN 73. The master 8259A's INT output is the card's pINT driver -- it has resolved
// the VI lines and the on-board sources and pulled the wire when one wins. The legacy
// Phase-2 `interrupt` jumper still routes the UART's RxRDY straight to pin 73 for a
// machine that does not use the on-board controllers (default `none`, so silent). Pure
// and combinational, like every assertsInt() (DESIGN.md 4.4.1).
bool Ss1Board::assertsInt() const {
    if (!clock_) return false;
    if (master_.intOut(masterLive())) return true;
    return uart_.jumper == IrqJumper::Int && uart_.rxReady();
}

// The legacy Phase-2 direct VI jumper for the UART's RxRDY. Leave it `none` when the
// on-board 8259A is in use (its master already watches the VI lines).
uint8_t Ss1Board::assertsVi() const {
    if (!clock_ || !uart_.rxReady()) return 0;
    return viBit(uart_.jumper);
}

// Which VI level the master would acknowledge, for SHOW BUS IRQ. Master IR0-6 map to
// VI0-6. Master IR7 is the slave cascade: it is VI7 when the slave's winner is its IR0
// (VI7*), and an on-board source (no VI line, -1) for any other slave input.
int Ss1Board::intWinner() const {
    if (!clock_) return -1;
    int w = master_.winner(masterLive());
    if (w >= 0 && w <= 6) return w;
    if (w == kMasterCascadeIr && slave_.winner(slaveLive()) == kSlaveVi7) return 7;
    return -1;
}

// The MSM5832 is battery-backed: neither the front-panel RESET nor a power cycle changes
// the time it keeps -- both events only clear the board-side command latches. The UART's
// RESET pin is not driven from the backplane (the monitor always software-programs the
// chip out of reset); a bus reset just re-polls and re-arms, power-on puts it in its
// known-good state. (Same stance, same reasoning, as SbcBoard::reset.)
void Ss1Board::reset(Reset r) {
    rtc_.reset();
    chip().reset();  // the chip's RESET is the board's CLEAR: pRESET*, POC* or SLAVE CLR*
    if (!clock_) return;
    if (r == Reset::PowerOn) {
        uart_.powerOn(*clock_);
        timer_.powerOn();  // the 8253 has no bus-reset pin either -- power-on only
        master_.powerOn();        // ...nor the 8259As: undefined until ICW1, so all-masked
        slave_.powerOn();
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
    // Latch any rising edges for edge-triggered 8259A mode (harmless in the level-
    // triggered default). Sense the slave first: the master's IR7 reads the slave's INT.
    slave_.senseEdges(slaveLive());
    master_.senseEdges(masterLive());
    intChanged();  // a source may have moved -- re-drive pin 73 through the master's INT

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
    consider(uart_.txFreeAt());  // TxRDY feeds slave IR6; RxRDY (above) feeds IR7

    // A timer OUT feeding an UNMASKED slave interrupt input can move the interrupt on
    // the 8253's own schedule -- arm for the next edge so the card wakes to re-sample.
    // (Masked inputs are skipped: a free-running masked counter would wake us for
    // nothing, exactly the trap the 88-VI's armRtc() calls out.) The edge is a pin-49
    // tick; the bus clock turns it into a T-state. Flat out that T-state is only a
    // keep-alive, and pump() -> refresh() sees the edge land in host time.
    if (clock_ && bus_) {
        uint64_t now = pin49();
        for (int n = 0; n < 3; ++n) {
            if (!slave_.irEnabled(n + 1)) continue;
            if (uint64_t tick = timer_.nextEdge(n, now))
                consider(bus_->busClock().tStateOf(tick, *clock_));
        }
        // A running math command sets END (and SVREQ moves) at its end -- the same edge,
        // for an unmasked IR4 or IR5.
        if (mathFitted() && chip().busy(now) &&
            (slave_.irEnabled(kSlaveSvrq) || slave_.irEnabled(kSlaveEnd)))
            consider(bus_->busClock().tStateOf(chip().busyUntil(), *clock_));
    }

    return best;
}

void Ss1Board::serialize(StateWriter& w) const {
    Board::serialize(w);
    rtc_.serialize(w);
    uart_.serialize(w);
    timer_.serialize(w, pin49());
    master_.serialize(w);
    slave_.serialize(w);
    chip().serialize(w, pin49());
    // intaByte_/intaAddr_ do not travel: a snapshot lands at an instruction boundary,
    // where no acknowledge is mid-flight, so intaByte_ is always 0.
}

void Ss1Board::deserialize(StateReader& r) {
    Board::deserialize(r);
    rtc_.deserialize(r);
    uart_.deserialize(r);
    timer_.deserialize(r, pin49());
    master_.deserialize(r);
    slave_.deserialize(r);
    chip().deserialize(r, pin49());
    intaByte_ = 0;
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
        // A part in a socket: a strap like `base`. Changing it is pulling one chip and
        // fitting another, so the new chip starts from reset.
        Property x;
        x.name    = "math";
        x.help    = "The chip in the math socket (U13): none (the stock empty socket), 9511a "
                    "(AMD Am9511A, Intel 8231A) or 9512 (AMD Am9512, Intel 8232)";
        x.kind    = Kind::Enum;
        x.choices = {"none", "9511a", "9512"};
        x.get     = [this] {
            return Value::ofStr(math_ == Math::Am9511a ? "9511a"
                                : math_ == Math::Am9512 ? "9512" : "none");
        };
        x.set     = [this](const Value& v, std::string&) {
            math_ = v.s() == "9511a" ? Math::Am9511a
                  : v.s() == "9512"  ? Math::Am9512 : Math::None;
            chip().reset();
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
        x.get = [this] { return Value::ofStr(timer_.describe(pin49())); };
        p.push_back(std::move(x));
    }
    {
        // LIVE, read-only: the two 8259As -- their request/in-service/mask registers and
        // whether the master is pulling pin 73. The guest programs them through the
        // ports, so there is no setter and CONFIG SAVE skips it.
        Property x;
        x.name = "pic";
        x.help = "LIVE: the master/slave 8259A -- IRR/ISR/IMR and the master's INT pin";
        x.kind = Kind::Str;
        x.get  = [this] {
            return Value::ofStr("master " + master_.describe(masterLive()) + "; slave " +
                                slave_.describe(slaveLive()));
        };
        p.push_back(std::move(x));
    }
    {
        // LIVE, read-only: the math chip -- busy or idle, its stack pointer, status, the
        // top of the stack and its END/SVREQ (and a 9512's ERR) outputs. "none" with the
        // socket empty.
        Property x;
        x.name = "mathchip";
        x.help = "LIVE: the math chip -- busy/idle, stack pointer, status, top of stack, "
                 "END/SVREQ/ERR";
        x.kind = Kind::Str;
        x.get  = [this] {
            return Value::ofStr(mathFitted() ? chip().describe(pin49()) : "none");
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
    std::vector<MapEntry> m = {
        {picMaster0Port(), picMaster0Port(), "read/write", "8259A master -- ICW1/OCW2/OCW3, IRR/ISR"},
        {picMaster1Port(), picMaster1Port(), "read/write", "8259A master -- ICW2-4/OCW1 (mask)"},
        {picSlave0Port(), picSlave0Port(), "read/write", "8259A slave -- ICW1/OCW2/OCW3, IRR/ISR"},
        {picSlave1Port(), picSlave1Port(), "read/write", "8259A slave -- ICW2-4/OCW1 (mask)"},
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
    if (mathFitted()) {  // an empty socket decodes nothing
        std::string part = math_ == Math::Am9512 ? "9512" : "9511A";
        m.push_back({mathDataPort(), mathDataPort(), "read/write", part + " math chip -- stack push/pop"});
        m.push_back({mathCmdPort(), mathCmdPort(), "read/write", part + " math chip -- command/status"});
    }
    return m;
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

// Install an ALREADY-BUILT stream (the MCP console's filtered scripted line), bypassing the
// resolver -- the caller resolved and wrapped it. Otherwise as connect(). Without it, the
// MCP binding falls back to the bare line and the console's transforms (strip7out) are lost.
bool Ss1Board::connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                             std::string& err) {
    if (unit != "serial") {
        err = "ss1 has no unit '" + unit + "' -- its serial channel is called 'serial'";
        return false;
    }
    uart_.connect(std::move(s));
    refresh();
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
