#include "boards/mits-88virtc.h"

#include "core/bus.h"

namespace altair {

// The control register, bit by bit. Named, so the code below reads like the manual.
namespace {
constexpr uint8_t kLevelMask = 0x07;  // bits 0-2, ones-complement (see the header)
constexpr uint8_t kLevelLive = 0x08;  // bit 3
constexpr uint8_t kClearRtcInt = 0x10;  // bit 4
constexpr uint8_t kClearDivider = 0x20;  // bit 5
constexpr uint8_t kRtcEnable = 0x40;  // bit 6
constexpr uint8_t kViEnable = 0x80;  // bit 7
}  // namespace

// ---------------------------------------------------------------------------
// THE DECODE.
//
// The write port always. The IntAck cycle only when we are the one asking -- an
// 88-VI that is disabled, or that has nothing pending the mask will admit, does not
// drive the data bus, the cycle goes unclaimed, and the bus floats 0xFF exactly as
// it does for unmapped memory.
bool VirtcBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type == Cycle::IoWrite) return c.port() == port_;
    if (c.type == Cycle::IntAck) return viEnabled_ && winner() >= 0;
    return false;  // no IN: the card is write-only from the guest's side
}

// "the 88-VI puts the RST instruction with the 3 bit address associated with that
//  priority level on the data bus."
uint8_t VirtcBoard::read(const BusCycle& c) {
    if (c.type != Cycle::IntAck) return 0xFF;
    int w = winner();
    return w >= 0 ? rstFor(w) : 0xFF;
}

void VirtcBoard::write(const BusCycle& c) {
    uint8_t d = c.data;

    // Bits 0-2 are the ones-complement of the level. The monitor writes 000 for
    // level 7; the manual's table says the same; only its prose disagrees, and the
    // header explains why we believe the code.
    curLevel_  = 7 - (d & kLevelMask);
    levelLive_ = (d & kLevelLive) != 0;

    if (d & kClearRtcInt) rtcInt_ = false;   // the service routine acknowledging it
    rtcEnabled_ = (d & kRtcEnable) != 0;
    viEnabled_  = (d & kViEnable) != 0;

    // "A high clears the counter network for the clock frequency of the RTC. This
    //  bit should be set high during initialization." -- restart the chain at zero.
    if (d & kClearDivider) armRtc();

    // Enabling or disabling the RTC starts or stops the chain, and the interrupt we
    // just cleared (or the structure we just enabled) may have moved pin 73.
    armRtc();
    intChanged();
}

// ---------------------------------------------------------------------------
// PRIORITY. VI0 IS HIGHEST, VI7 IS LOWEST -- so the winner is the LOWEST SET BIT.
//
// The current-level register, when it is live, admits only levels of strictly HIGHER
// priority than the one being serviced: "Normal operation defines current priority
// as uninterruptable by another level of the same or lesser priority." Higher
// priority means a smaller number. When it is not live, everything gets through.
int VirtcBoard::winner() const {
    uint8_t lines = pendingLines();
    if (!lines) return -1;
    for (int i = 0; i < 8; ++i) {
        if (!(lines & (1u << i))) continue;
        if (levelLive_ && i >= curLevel_) return -1;  // and nothing below it can either
        return i;
    }
    return -1;
}

uint8_t VirtcBoard::pendingLines() const {
    uint8_t m = bus_ ? bus_->viLines() : 0;
    return (uint8_t)(m | assertsVi());  // our RTC is on that wire like anyone else
}

// Pin 73. Nothing here but reading the pins.
bool VirtcBoard::assertsInt() const { return viEnabled_ && winner() >= 0; }

// "RI" -- the RTC's interrupt output, jumpered to one of the eight levels. Unstrapped
// (`none`), the flip-flop sets and nothing is listening, which is precisely how the
// PS2 package is meant to be run.
uint8_t VirtcBoard::assertsVi() const { return rtcInt_ ? viBit(rtcJumper_) : 0; }

// ---------------------------------------------------------------------------
// THE RTC's DIVIDER CHAIN.
//
// Period in T-STATES, not hertz, because three of the eight jumper combinations are
// fractional (60 Hz / 1000 = 0.06 Hz) and would round to nothing. tStatesPer() gives
// the base tick exactly and the divider is then just multiplication.
uint64_t VirtcBoard::rtcPeriod() const {
    if (!clock_) return 0;
    // "The RTC source may be selected from either a derivative of the 2 megahertz
    //  clock or the line frequency." 10 kHz, or 60 Hz mains.
    long long base = rtcSource_ == RtcSource::Line ? 60 : 10000;
    return clock_->tStatesPer(base) * (uint64_t)rtcDivide_;
}

// Cancel and re-arm -- the idiom every clocked card here uses (see Sio2Board::refresh).
//
// WE ONLY ARM IT WHEN IT CAN BE OBSERVED: the interrupt has to be enabled AND the RI
// strap has to go somewhere. Otherwise the divider is a counter nobody can read, and
// an always-armed periodic timer would leave Clock::queued() permanently non-zero --
// which is one of the two things the run loop uses to decide a HLT has finished
// (core/debug.cpp). A machine with an unstrapped RTC would never stop on a HLT again.
void VirtcBoard::armRtc() {
    if (!clock_) return;
    clock_->cancel(wake_);
    wake_ = Clock::kNone;
    if (!rtcEnabled_ || rtcJumper_ == IrqJumper::None) return;
    uint64_t p = rtcPeriod();
    if (p) wake_ = clock_->after(p, [this] { onRtcTick(); });
}

// The interval elapsed: the flip-flop sets, RI goes out onto its VI line, and the
// chain immediately starts counting the next one. Only the service routine's bit 4
// clears the flip-flop -- "The RTC does not operate in this manner [cleared by
// reading a data channel]... The service routine that handles the RTC must output
// bit 4 high."
void VirtcBoard::onRtcTick() {
    rtcInt_ = true;
    intChanged();  // publishes RI on its VI line -- and that may raise pin 73
    armRtc();
}

// ---------------------------------------------------------------------------
// "POC (power-on-clear) ensures that all functions on the 88-VI (RTC) are disabled
//  when power is first applied."
//
// POC* ONLY. The manual says nothing about the front panel's RESET* reaching this
// logic, and the schematic runs POC to it, so RESET* deliberately does nothing here.
void VirtcBoard::reset(Reset r) {
    if (r != Reset::PowerOn) return;
    viEnabled_  = false;
    levelLive_  = false;
    curLevel_   = 0;
    rtcEnabled_ = false;
    rtcInt_     = false;
    armRtc();
    intChanged();
}

void VirtcBoard::power() { reset(Reset::PowerOn); }

// A jumper moved: `rtc_interrupt` changes which wire RI is soldered to, and
// `rtc_source`/`rtc_divide` change the interval the chain is counting. All three move
// a deadline this card has already set.
void VirtcBoard::configChanged() {
    decodeChanged();
    armRtc();
    intChanged();
}

std::vector<Property> VirtcBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Control port. 0xFE (376 octal) on the real card -- write only";
        x.kind  = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xFF;
        x.get   = [this] { return Value::ofInt(port_); };
        x.set   = [this](const Value& v, std::string&) {
            port_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "rtc_source";
        x.help    = "RTC clock source jumper: the 60 Hz line, or 10 kHz off the 2 MHz clock";
        x.kind    = Kind::Enum;
        x.choices = {"line", "clock"};
        x.get     = [this] {
            return Value::ofStr(rtcSource_ == RtcSource::Line ? "line" : "clock");
        };
        x.set = [this](const Value& v, std::string&) {
            rtcSource_ = v.s() == "line" ? RtcSource::Line : RtcSource::Clock;
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "rtc_divide";
        x.help    = "RTC divider jumper: source frequency / 1, 10, 100 or 1000";
        x.kind    = Kind::Enum;
        x.choices = {"1", "10", "100", "1000"};
        x.get     = [this] { return Value::ofStr(std::to_string(rtcDivide_)); };
        x.set     = [this](const Value& v, std::string&) {
            rtcDivide_ = std::stoi(v.s());
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        // The RI strap. Same vocabulary as every other interrupt jumper in the
        // machine -- MINUS `int`, because RI does not go to pin 73 and the manual is
        // blunt about why: "A system designed to use the 88-VI may not have any I/O
        // board strapped for single level interrupt." Offering a choice the hardware
        // cannot make is how you end up debugging a machine that cannot exist.
        Property x = irqJumperProperty(
            "rtc_interrupt",
            "Where the RTC's interrupt (\"RI\") is jumpered: none | vi0..vi7. "
            "Leave it `none` to run the PS2 package",
            rtcJumper_);
        x.choices = {"none", "vi0", "vi1", "vi2", "vi3", "vi4", "vi5", "vi6", "vi7"};
        auto inner = x.set;
        x.set = [inner](const Value& v, std::string& err) {
            if (v.s() == "int") {
                err = "the RTC's interrupt jumpers to a VI level, not to pin 73 -- an "
                      "88-VI system may not have any board on a single-level interrupt";
                return false;
            }
            return inner(v, err);
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<MapEntry> VirtcBoard::ioMap() const {
    return {{port_, port_, "write", "88-VI/RTC control -- priority, RTC enable, RST vectors"}};
}

} // namespace altair
