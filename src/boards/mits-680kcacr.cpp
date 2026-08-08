#include "boards/mits-680kcacr.h"

#include "core/statefile.h"
#include "host/tapemodem.h"

#include <utility>
#include <vector>

namespace altair {

namespace {

// Control (write F010), active-low: an asserted bit is 0. D0/D1 are the two
// interrupt enables (the inherited flip-flops); D6/D7 the motor relay.
constexpr uint8_t kReadIntEnable  = 0x01;  // D0 -- asserted (0) => Read-Data interrupt on
constexpr uint8_t kWriteIntEnable = 0x02;  // D1 -- asserted (0) => Transmit interrupt on
constexpr uint8_t kMotorOff       = 0x40;  // D6 -- asserted (0) => motor OFF (store BF)
constexpr uint8_t kMotorOn        = 0x80;  // D7 -- asserted (0) => motor ON  (store 7F)

// Status (read F010), active-low: an asserted condition reads 0.
constexpr uint8_t kRdaBit = 0x01;  // D0 -- Read Data Available
constexpr uint8_t kTbeBit = 0x80;  // D7 -- Transmit Buffer Empty

// A card in a backplane always has a clock, but Bus::attach() is public, so a board
// CAN be wired up without a machine around it. A UART with no clock cannot receive
// or time a character; it reads as a dead card rather than dereferencing null. (The
// same deadCard() SioBoard keeps privately in its own translation unit.)
Clock& deadCard() {
    static Clock stopped;
    return stopped;
}

} // namespace

// AcrBoard() has already strapped the cassette UART -- base_ = 0x06 (unused here; we
// decode fixed memory addresses), 300 baud, 8 data bits, Rev 1. The KCACR's manual
// frames its UART as 8 data bits, no parity, TWO stop bits (reference section 5), so
// the one strap that differs from the ACR is the stop-bit count.
KcacrBoard::KcacrBoard() {
    u_.stopBits = 2;
}

// ---------------------------------------------------------------------------
// The bus. Two consecutive bytes of 6800 memory; read and write of the same
// address are different registers (reference section 1).
// ---------------------------------------------------------------------------
bool KcacrBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::MemRead && c.type != Cycle::MemWrite) return false;
    return c.addr == kStatusCtrl || c.addr == kData;
}

uint8_t KcacrBoard::read(const BusCycle& c) {
    const Clock& clk = clock_ ? *clock_ : deadCard();
    u_.poll(clk);  // the receiver runs on the UART's clock, not on ours

    // F011 read strobes the chip's /RDAR (clearing Read Data Available); F010 read is
    // the status word.
    uint8_t v = (c.addr == kData) ? u_.readData() : statusByte();

    // READING EITHER REGISTER RESETS THE INTERRUPT-ENABLE LATCHES (reference section 4)
    // -- this is how the 6800's handler acknowledges. In polled use the enables are
    // already clear, so this is a no-op there; in interrupt use it drops IRQ until the
    // driver re-enables. refresh() re-drives the pin and re-arms the deadline.
    inIntEnabled_  = false;
    outIntEnabled_ = false;
    refresh();
    return v;
}

void KcacrBoard::write(const BusCycle& c) {
    if (c.addr == kData) {
        // /TDS: the character goes out, TBE falls until it has had time to leave.
        u_.writeData(c.data, clock_ ? *clock_ : deadCard());
        refresh();
        return;
    }

    // F010 control, active-low. Motor: D7=0 on, D6=0 off (the relay is a KCACR add).
    if (!(c.data & kMotorOn)) motorOn_ = true;
    else if (!(c.data & kMotorOff)) motorOn_ = false;

    // The two interrupt enables, asserted low: D0=0 enables Read-Data, D1=0 Transmit.
    inIntEnabled_  = !(c.data & kReadIntEnable);
    outIntEnabled_ = !(c.data & kWriteIntEnable);

    // MOTOR OFF ALSO RESETS THE INTERRUPTS (reference section 4, the `BF` row). BF has
    // D0=D1=1 already, so the enables clear above -- but the manual states Motor Off as
    // an interrupt reset in its own right, so force it for any value that turns the
    // motor off, not only the canonical BF.
    if (!(c.data & kMotorOff)) {
        inIntEnabled_  = false;
        outIntEnabled_ = false;
    }

    refresh();  // the enables (or the motor) moved -- re-drive IRQ and re-arm the alarm
}

// The active-low status word. Not-asserted bits read 1; an asserted condition reads 0.
// Only D0 (Read Data Available) and D7 (Transmit Buffer Empty) are driven; D1-D6 are
// unused and float high (reference section 2).
uint8_t KcacrBoard::statusByte() const {
    uint8_t s = 0xFF;
    if (rxReady()) s &= (uint8_t)~kRdaBit;  // a byte is waiting -> D0 asserted (0)
    if (txReady()) s &= (uint8_t)~kTbeBit;  // ready for the next byte -> D7 asserted (0)
    return s;
}

// ---------------------------------------------------------------------------
// PIN: the 6800 IRQ. An enabled condition that is true pulls it -- combinational and
// pure. No VI straps: the KCACR wires straight to the 6800's single IRQ line.
// ---------------------------------------------------------------------------
bool KcacrBoard::assertsInt() const {
    if (!clock_) return false;  // no crystal: the UART is not running
    if (inIntEnabled_ && rxReady()) return true;
    if (outIntEnabled_ && txReady()) return true;
    return false;
}

// ---------------------------------------------------------------------------
// RESET. SioBoard::reset() masters the UART and clears the two interrupt-enable
// flip-flops; the KCACR adds the motor relay, which comes up closed at power-up (the
// 88-UIO's documented assumption -- no period driver runs the transport without first
// setting the motor, so nothing should be able to tell).
// ---------------------------------------------------------------------------
void KcacrBoard::reset(Reset r) {
    SioBoard::reset(r);
    motorOn_ = true;
}

// ---------------------------------------------------------------------------
// Reflection. The tape properties (AcrBoard's mode/format/leader/.../rate/counter/
// stop, plus the UART's baud/data_bits/stop_bits/parity straps), MINUS the SIO's
// electrical straps that do not exist on a memory-mapped 6800 card -- the port base,
// the Rev0/Rev1 status-word switch, and the two S-100 VI interrupt straps. Plus the
// one thing this board adds: a read-only view of the motor relay.
// ---------------------------------------------------------------------------
std::vector<Property> KcacrBoard::properties() {
    std::vector<Property> all = AcrBoard::properties();

    static const char* kDrop[] = {"port", "rev", "in_int", "out_int"};
    std::vector<Property> p;
    for (Property& x : all) {
        bool dropped = false;
        for (const char* d : kDrop) dropped = dropped || x.name == d;
        if (!dropped) p.push_back(std::move(x));
    }

    // THE MOTOR RELAY -- READ-ONLY (no setter). The GUEST drives it, with a store to
    // the control register (STA F010: 7F on, BF off); a SET that fought the program for
    // it would offer a control the hardware does not have. SHOW reads it.
    Property m;
    m.name    = "motor";
    m.help    = "Tape-recorder motor relay (guest-driven: STA F010 7F = on, BF = off)";
    m.kind    = Kind::Enum;
    m.choices = {"on", "off"};
    m.get     = [this] { return Value::ofStr(motorOn_ ? "on" : "off"); };
    p.push_back(std::move(m));

    return p;
}

std::vector<MapEntry> KcacrBoard::memMap() const {
    return {
        {(uint32_t)kStatusCtrl, (uint32_t)kStatusCtrl, "read/write",
         "KCACR -- status (read) / control: motor + interrupt enables (write)"},
        {(uint32_t)kData, (uint32_t)kData, "read/write",
         "KCACR -- read data / write data, via the modem, to the cassette"},
    };
}

// ---------------------------------------------------------------------------
// SNAPSHOT / RESTORE. AcrBoard writes [Board fields][cassette UART][int-enables][tape
// mode + head]; the KCACR adds its one runtime latch, the motor relay. The tape
// modulation is fixed (kcs300), not a config switch, so nothing else travels.
// ---------------------------------------------------------------------------
void KcacrBoard::serialize(StateWriter& w) const {
    AcrBoard::serialize(w);
    w.boolean(motorOn_);
}

void KcacrBoard::deserialize(StateReader& r) {
    AcrBoard::deserialize(r);
    motorOn_ = r.boolean();
}

// The one modem this board has -- Kansas City Standard, the shipping constant the
// 88-UIO's SW-1=kansas also returns (host/tapemodem.h).
std::vector<TapeFormat> KcacrBoard::modem() const {
    return {tapeformats::kcs300()};
}

} // namespace altair
