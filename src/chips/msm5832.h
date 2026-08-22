#pragma once
//
// OKI MSM5832 -- a real-time clock/calendar CHIP, NOT A CARD.
//
// A CMOS BCD clock/calendar, crystal-timed and battery-backable, giving
// seconds/minutes/hours/day-of-week/day/month/year with no CPU polling overhead.
// The CompuPro System Support 1 carries one; the next card with an MSM5832 gets this
// for free. Modeled from the OKI datasheet as reprinted in the System Support 1
// manual (reference/CompuPro System Support 1.md sec. 5, reference/OKI MSM5832.md),
// NOT from any one program that drives it.
//
// It knows nothing about S-100. It has two ports -- a command register and a data
// register -- and it reads the HOST's wall clock. There is no oscillator to model:
// the "chip" is the host's own time-of-day, offset by whatever the guest last set.
//
// ---------------------------------------------------------------------------
// HOW SETTING A HOST-BACKED CLOCK WORKS.
//
// The displayed time is `localCalendar(host_now + offset_)`. offset_ is a signed
// second count that is 0 at power-on (the guest sees real wall time) and moves only
// when the guest SETS the clock. It is battery-backed: a RESET does not clear it, and
// it travels in a SNAPSHOT. That is the whole model -- the clock always ticks from
// the host, and a set is just a remembered delta.
//
// A set is a per-digit edit under Hold (datasheet sequence): the guest raises Hold,
// writes each digit, then drops Hold. On the rising edge we SNAPSHOT the current
// display into an edit buffer; each Write strobe updates one digit of the buffer;
// on the falling edge -- and only if a digit was actually written -- we compose the
// buffer back into a host time_t (via mktime) and store the new offset_. A Hold that
// only fenced a glitch-free READ writes nothing, so dropping it changes nothing.
//
// ---------------------------------------------------------------------------
// TWO REGISTERS HAVE MODE BITS WHERE YOU'D EXPECT MORE DIGIT (datasheet, manual p.28):
//
//   Hours-10 (digit 5): bits 1:0 = the tens digit (0-2); bit2 = AM(0)/PM(1); bit3 =
//     12-hour(0)/24-hour(1) mode. This chip runs in 24-HOUR MODE -- reads set bit3.
//   Days-10  (digit 8): bits 1:0 = the tens digit (0-3); bit2 = leap-year (1 = 29
//     days in February). Reads set bit2 from the displayed year.
//
// And the seconds are WRITE-IGNORED, not read-as-zero: reads return the running
// seconds like any digit; only writes to Seconds-1/Seconds-10 are discarded (forced
// to 0), so setting the clock always zeroes the seconds ("Both seconds digits are not
// settable to anything but zeroes", manual p.28).

#include "platform/localtime.h"

#include <cstdint>
#include <ctime>
#include <string>

namespace altair {

class StateWriter;
class StateReader;

class Msm5832 {
public:
    // ---- the two ports the board decodes to us ----

    // Command register (Base+10). Hold(6) / Write(5) / Read(4) / digit-select(3-0).
    void writeCommand(uint8_t v);

    // Data register (Base+11). Low nibble is the BCD digit to stage for a Write strobe.
    void writeData(uint8_t v);

    // Data register (Base+11) read: the selected digit. While Hold fences an edit this
    // is the frozen snapshot; otherwise it is the live host time. Not const-idempotent
    // (wall time advances), which is exactly a hardware read.
    uint8_t readData() const;

    // ---- lifecycle ----

    // RESET clears the transient latches but NOT offset_: the MSM5832 is battery-backed
    // and keeps time across a front-panel RESET and a power cycle alike.
    void reset();

    // ---- SNAPSHOT / RESTORE (DESIGN.md 13). offset_ is the real state; the edit
    // latches travel too so a snapshot taken mid-set restores mid-set. ----
    void serialize(StateWriter& w) const;
    void deserialize(StateReader& r);

    // ---- for SHOW / status ----
    platform::CalendarTime displayed() const;  // host time + offset_, broken down
    std::string describe() const;              // "2026-08-22 14:03:07 (host+0s)"

private:
    // The nibble a given digit (0-12) reads as, from a broken-down time.
    static uint8_t digitOf(int sel, const platform::CalendarTime& c);
    // Compose the 13-digit edit buffer back into a host time_t and store the offset.
    void applyEdit();
    // Snapshot the live display into the edit buffer if we are not already editing.
    void ensureEdit();

    long long offset_ = 0;   // seconds added to host time; battery-backed, snapshotted
    uint8_t digitSel_ = 0;   // command bits 3-0
    uint8_t dataLatch_ = 0;  // last data-register write, low nibble
    bool hold_ = false;      // command bit 6
    bool editing_ = false;   // Hold fenced an edit and we snapshotted
    bool dirty_ = false;     // a Write strobe changed a digit -- apply on Hold release
    uint8_t editDigits_[13] = {};  // the frozen/edited display, one nibble per digit
};

}  // namespace altair
