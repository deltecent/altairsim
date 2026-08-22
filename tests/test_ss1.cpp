// The CompuPro System Support 1 -- Phase 1: the OKI MSM5832 real-time clock.
//
// Everything here drives REAL bus cycles (ioWrite/ioRead), through the MSM5832's own
// command/data protocol, exactly as a guest program would. The clock reads the HOST's
// wall time, so the read tests compare against the host clock (with a small tolerance
// for the second that can tick mid-test), and the set tests check exact readback of a
// time the guest programmed in.

#include "test.h"

#include "boards/compupro-ss1.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "platform/localtime.h"

#include <cstdint>
#include <cstdlib>
#include <ctime>

using namespace altair;

namespace {

// Command-register bits (base+10).
constexpr uint8_t kHold = 0x40, kWrite = 0x20, kRead = 0x10;

uint8_t cmdPort(uint8_t base) { return (uint8_t)(base + 10); }
uint8_t dataPort(uint8_t base) { return (uint8_t)(base + 11); }

// Read one BCD digit the way a program does: select it (with Read), then read data.
uint8_t readDigit(Machine& m, uint8_t base, uint8_t sel) {
    m.bus.ioWrite(cmdPort(base), (uint8_t)(kRead | sel));
    return m.bus.ioRead(dataPort(base));
}

// Write one digit the way a program does: select under Hold, stage data, strobe Write.
void writeDigit(Machine& m, uint8_t base, uint8_t sel, uint8_t val) {
    m.bus.ioWrite(cmdPort(base), (uint8_t)(kHold | sel));
    m.bus.ioWrite(dataPort(base), (uint8_t)(val & 0x0F));
    m.bus.ioWrite(cmdPort(base), (uint8_t)(kHold | kWrite | sel));
    m.bus.ioWrite(cmdPort(base), (uint8_t)(kHold | sel));
}

// Program a full date/time (two-digit year). Also *attempts* to set the seconds to 59,
// to prove that write is ignored. Weekday is derived from the date, not set.
void setClock(Machine& m, uint8_t base, int y2, int mo, int d, int h, int mi) {
    m.bus.ioWrite(cmdPort(base), kHold);  // raise Hold to edit
    writeDigit(m, base, 0, 9);            // seconds ones -- must be ignored
    writeDigit(m, base, 1, 5);            // seconds tens -- must be ignored
    writeDigit(m, base, 2, (uint8_t)(mi % 10));
    writeDigit(m, base, 3, (uint8_t)(mi / 10));
    writeDigit(m, base, 4, (uint8_t)(h % 10));
    writeDigit(m, base, 5, (uint8_t)(h / 10));
    writeDigit(m, base, 7, (uint8_t)(d % 10));
    writeDigit(m, base, 8, (uint8_t)(d / 10));
    writeDigit(m, base, 9, (uint8_t)(mo % 10));
    writeDigit(m, base, 10, (uint8_t)(mo / 10));
    writeDigit(m, base, 11, (uint8_t)(y2 % 10));
    writeDigit(m, base, 12, (uint8_t)(y2 / 10));
    m.bus.ioWrite(cmdPort(base), 0x00);  // drop Hold -> the set is applied
}

int minuteOf(Machine& m, uint8_t base) {
    return (readDigit(m, base, 3) & 0x0F) * 10 + (readDigit(m, base, 2) & 0x0F);
}
int hourOf(Machine& m, uint8_t base) {
    return (readDigit(m, base, 5) & 0x03) * 10 + (readDigit(m, base, 4) & 0x0F);
}
int dayOf(Machine& m, uint8_t base) {
    return (readDigit(m, base, 8) & 0x03) * 10 + (readDigit(m, base, 7) & 0x0F);
}
int monthOf(Machine& m, uint8_t base) {
    return (readDigit(m, base, 10) & 0x0F) * 10 + (readDigit(m, base, 9) & 0x0F);
}
int year2Of(Machine& m, uint8_t base) {
    return (readDigit(m, base, 12) & 0x0F) * 10 + (readDigit(m, base, 11) & 0x0F);
}
int secOf(Machine& m, uint8_t base) {
    return (readDigit(m, base, 1) & 0x0F) * 10 + (readDigit(m, base, 0) & 0x0F);
}

}  // namespace

void test_ss1() {
    SECTION("CompuPro System Support 1 -- MSM5832 real-time clock");

    // ---- reads mirror the host wall clock (offset starts at 0) ----
    {
        Machine m;
        auto* ss1 = new Ss1Board();
        ss1->id = "ss1";
        m.bus.attach(ss1);

        // Reconstruct the whole displayed time and compare to the host epoch. Reading
        // the digits takes microseconds, and the +-2s window absorbs a second that
        // happens to tick across the read.
        int sec = secOf(m, 0x50), mi = minuteOf(m, 0x50), h = hourOf(m, 0x50);
        int d = dayOf(m, 0x50), mo = monthOf(m, 0x50), y2 = year2Of(m, 0x50);
        platform::CalendarTime host = platform::localCalendar(std::time(nullptr));
        std::tm tm{};
        tm.tm_sec = sec;
        tm.tm_min = mi;
        tm.tm_hour = h;
        tm.tm_mday = d;
        tm.tm_mon = mo - 1;
        tm.tm_year = ((host.year / 100) * 100 + y2) - 1900;
        tm.tm_isdst = -1;
        long long chip = (long long)std::mktime(&tm);
        long long now = (long long)std::time(nullptr);
        CHECK(std::llabs(chip - now) <= 2, "the clock reads the host wall time at power-on");

        // The command register is write-only: an IN there is nobody's and floats.
        CHECK(m.bus.ioRead(cmdPort(0x50)) == 0xFF, "the clock command port is write-only");
    }

    // ---- setting the clock: exact readback, seconds forced to 0, mode/leap bits ----
    {
        Machine m;
        auto* ss1 = new Ss1Board();
        ss1->id = "ss1";
        m.bus.attach(ss1);

        // 2032 is a leap year, so Feb 29 is a valid date to program -- and it exercises
        // the leap-year bit and the mktime compose path at once.
        setClock(m, 0x50, /*y2*/ 32, /*mo*/ 2, /*d*/ 29, /*h*/ 13, /*mi*/ 45);

        CHECK(year2Of(m, 0x50) == 32, "the year reads back what we set");
        CHECK(monthOf(m, 0x50) == 2, "the month reads back what we set");
        CHECK(dayOf(m, 0x50) == 29, "the day reads back what we set (Feb 29 in a leap year)");
        CHECK(hourOf(m, 0x50) == 13, "the hour reads back what we set");
        CHECK(minuteOf(m, 0x50) == 45, "the minute reads back what we set");
        CHECK(secOf(m, 0x50) <= 2,
              "the seconds are forced to 0 on a set -- a write of 59 is ignored");

        // Hours-10 carries the 24-hour mode bit (bit3); Days-10 carries the leap bit
        // (bit2), which is set because 2032 is a leap year.
        CHECK((readDigit(m, 0x50, 5) & 0x08) != 0, "Hours-10 reports 24-hour mode (bit3)");
        CHECK((readDigit(m, 0x50, 8) & 0x04) != 0, "Days-10 reports the leap year (bit2)");

        // A non-leap year clears the leap bit.
        setClock(m, 0x50, /*y2*/ 30, /*mo*/ 6, /*d*/ 15, /*h*/ 9, /*mi*/ 5);
        CHECK((readDigit(m, 0x50, 8) & 0x04) == 0, "Days-10 clears the leap bit in 2030");
        CHECK(year2Of(m, 0x50) == 30 && monthOf(m, 0x50) == 6, "the new set took");
    }

    // ---- the clock is battery-backed: a set survives RESET and power-on ----
    {
        Machine m;
        auto* ss1 = new Ss1Board();
        ss1->id = "ss1";
        m.bus.attach(ss1);

        setClock(m, 0x50, 32, 2, 29, 13, 45);
        ss1->reset(Reset::Bus);
        CHECK(year2Of(m, 0x50) == 32, "a front-panel RESET does not lose the time");
        ss1->power();
        CHECK(year2Of(m, 0x50) == 32, "power-on does not lose the time (it is battery-backed)");
    }

    // ---- SNAPSHOT / RESTORE carries the set time into a fresh board ----
    {
        Machine m;
        auto* ss1 = new Ss1Board();
        ss1->id = "ss1";
        m.bus.attach(ss1);
        setClock(m, 0x50, 32, 2, 29, 13, 45);

        StateWriter w;
        ss1->serialize(w);

        Machine m2;
        auto* b2 = new Ss1Board();
        b2->id = "ss1";
        m2.bus.attach(b2);
        StateReader r(w.data());
        b2->deserialize(r);
        CHECK(r.ok(), "the snapshot reads back without underrun");
        CHECK(year2Of(m2, 0x50) == 32 && monthOf(m2, 0x50) == 2 && dayOf(m2, 0x50) == 29,
              "the restored board shows the time the snapshot captured");
    }

    // ---- the base strap moves the whole block ----
    {
        Machine m;
        auto* ss1 = new Ss1Board();
        ss1->id = "ss1";
        m.bus.attach(ss1);

        std::string err;
        CHECK(setProperty(*ss1, "base", "60", err), "move the block to base 60");

        setClock(m, 0x60, 25, 12, 31, 23, 59);
        CHECK(year2Of(m, 0x60) == 25 && hourOf(m, 0x60) == 23,
              "the clock answers at its new base");
        CHECK(m.bus.ioRead(0x5B) == 0xFF, "...and no longer at the old data port");
    }
}
