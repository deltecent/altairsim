// The CompuPro System Support 1 -- Phase 1: the OKI MSM5832 real-time clock.
//
// Everything here drives REAL bus cycles (ioWrite/ioRead), through the MSM5832's own
// command/data protocol, exactly as a guest program would. The clock reads the HOST's
// wall time, so the read tests compare against the host clock (with a small tolerance
// for the second that can tick mid-test), and the set tests check exact readback of a
// time the guest programmed in.

#include "test.h"

#include "boards/compupro-ss1.h"
#include "boards/s100-memory.h"
#include "chips/intel8253.h"
#include "chips/intel8259a.h"
#include "chips/sig2651.h"
#include "core/busclock.h"
#include "core/clock.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/stream.h"
#include "platform/localtime.h"

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <memory>

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

// ---- Phase 2: the 2651 UART ----

// STATUS byte bits (reference/Signetics 2651 USART.md §2.2).
constexpr uint8_t kTxRDY = 0x01, kRxRDY = 0x02, kTxEMT = 0x04, kDCD = 0x40, kDSR = 0x80;

// A bare 2651 on a Clock, driving a scripted terminal -- the CHIP, exercised directly.
struct UartRig {
    Clock           clk;
    Sig2651         u{"serial"};
    ScriptedStream* tty = nullptr;

    UartRig() {
        clk.setHz(4000000);  // 4 MHz, so at 9600 baud one bit = ~416 T-states
        auto s = std::make_unique<ScriptedStream>();
        tty    = s.get();
        u.connect(std::move(s));
        u.powerOn(clk);
    }

    // Program the chip the manual's own way (§2.6): MR1, then MR2, then a command word.
    // 0x4E/0x7E/0x27 = async 8N1, 9600 baud, TxEN|RxEN|DTR|RTS.
    void program(uint8_t mr1 = 0x4E, uint8_t mr2 = 0x7E, uint8_t cmd = 0x27) {
        u.writeMode(mr1);
        u.writeMode(mr2);
        u.writeCommand(cmd, clk);
    }

    uint8_t status() { return u.readStatus(clk); }
    void    advance(uint64_t dt) { clk.advance(dt); }
};

// bit period and whole-character time at 9600/4MHz, for readable assertions.
constexpr uint64_t kBit  = 4000000 / 9600;  // ~416
constexpr uint64_t kChar = kBit * 10;       // 8N1 = 10 bits

// ---- Phase 3: the 8253 interval timer ----

// A bare 8253 -- the CHIP, exercised directly. It counts edges of its counter clock, and
// the rig hands it that count, so the mode arithmetic reads straight off the numbers.
struct TimerRig {
    uint64_t  now = 0;  // counter-clock edges
    Intel8253 t{"timer"};

    TimerRig() { t.powerOn(); }
    void advance(uint64_t dt) { now += dt; }
};

// Control-word helpers: SC1/SC0 (counter) | RL1/RL0 | M2/M1/M0 | BCD.
constexpr uint8_t kLsb = 1, kBoth = 3;  // read/load format (MSB-only is unused here)
uint8_t ctrl(int counter, uint8_t rl, uint8_t mode, bool bcd = false) {
    return (uint8_t)((counter << 6) | (rl << 4) | (mode << 1) | (bcd ? 1 : 0));
}

// ---- Phase 4: the dual 8259A interrupt controllers ----

// ICW4 values (D0 = uPM: 0 = MCS-80/85, 1 = 8086). MCS uses the manual's 0x10 (SFNM set,
// which we accept as ordinary fully-nested); the 8086 test uses 0x01.
constexpr uint8_t kIcw4Mcs = 0x10, kIcw4_8086 = 0x01;

// Program one lone 8259A. icw1 must set SNGL (D1) so no ICW3 is expected.
void initPic(Intel8259a& p, uint8_t icw1, uint8_t icw2, uint8_t icw4, uint8_t mask) {
    p.powerOn();
    p.write(false, icw1);              // ICW1
    p.write(true, icw2);               // ICW2
    if (icw1 & 0x01) p.write(true, icw4);  // ICW4, only if IC4 (ICW1 D0) set
    p.write(true, mask);               // OCW1 (the mask)
}

// Program the SS-1's two 8259As over the bus in the standard cascade: master vectors at
// 0x0200, slave at 0x2200, both level-triggered with interval 4. `mMask`/`sMask` are the
// OCW1 masks (a 0 bit is unmasked).
void initSs1Pics(Machine& m, uint8_t base, uint8_t mMask, uint8_t sMask, uint8_t icw1 = 0x1D) {
    uint8_t m0 = (uint8_t)(base + 0), m1 = (uint8_t)(base + 1);
    uint8_t s0 = (uint8_t)(base + 2), s1 = (uint8_t)(base + 3);
    m.bus.ioWrite(m0, icw1);       // master ICW1: 1D = cascade, level, interval 4, IC4
    m.bus.ioWrite(m1, 0x02);       //        ICW2: vectors at 0x0200
    m.bus.ioWrite(m1, 0x80);       //        ICW3: a slave on IR7
    m.bus.ioWrite(m1, kIcw4Mcs);   //        ICW4: MCS-80/85
    m.bus.ioWrite(m1, mMask);      //        OCW1
    m.bus.ioWrite(s0, icw1);       // slave  ICW1
    m.bus.ioWrite(s1, 0x22);       //        ICW2: vectors at 0x2200
    m.bus.ioWrite(s1, 0x07);       //        ICW3: slave ID 7 (wired to master IR7)
    m.bus.ioWrite(s1, kIcw4Mcs);   //        ICW4
    m.bus.ioWrite(s1, sMask);      //        OCW1
}

// A 64K-RAM + ss1 + CPU machine, powered, ready to program. The CPU clocks at 4 MHz so
// the 2 MHz 8253 takes two T-states per tick.
Ss1Board* buildIntMachine(Machine& m, const char* cpu = "8080") {
    std::string err;
    auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
    Region rr;
    rr.kind = RegionKind::Ram;
    rr.at   = 0;
    rr.size = 0x10000;
    mem->addRegion(rr, err);
    setProperty(*mem, "fill", "zero", err);
    auto* ss1 = dynamic_cast<Ss1Board*>(m.add("ss1", "ss0", err));
    m.add(cpu, "cpu0", err);
    setProperty(*(dynamic_cast<Board*>(m.find("cpu0"))), "clock_hz", "4000000", err);
    m.power();
    return ss1;
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

    // =====================================================================
    // Phase 2 -- the 2651 UART.
    // =====================================================================

    SECTION("SS-1 2651 -- the mode-register pointer: MR1 THEN MR2");
    {
        // MR1 and MR2 share the mode address; an internal pointer routes the first mode
        // write to MR1 and the next to MR2. Program MR1 = 7 data bits and MR2 = 9600. If
        // the pointer were wrong (0x4A taken as MR2), the baud would be 2400 (nibble A)
        // and the frame 8 bits -- so this single check pins the ordering.
        UartRig g;
        g.u.writeMode(0x4A);  // MR1: async 16x, 7 data bits, 1 stop, no parity
        g.u.writeMode(0x7E);  // MR2: 9600 baud (low nibble E)
        CHECK(g.u.params().dataBits == 7, "MR1 set the frame (7 data bits)");
        CHECK(g.u.params().baud == 9600, "MR2 set the baud (9600), not the frame");

        // The pointer wrapped back to MR1 after MR2, so a re-program starts at MR1 again.
        g.u.writeMode(0x4E);  // MR1 again: 8 data bits
        CHECK(g.u.params().dataBits == 8, "after MR2 the pointer wrapped: this write is MR1 again");

        // powerOn resets the pointer to MR1 as well.
        g.u.powerOn(g.clk);
        g.u.writeMode(0xEE);  // MR1: 8 data, 2 stop (the manual's 8-2 sample)
        CHECK(g.u.params().dataBits == 8 && g.u.params().stopBits == 2,
              "power-on rewinds the pointer: this write is MR1 (8 data, 2 stop)");
    }

    SECTION("SS-1 2651 -- the MR2 baud table");
    {
        UartRig g;
        auto baudFor = [&](uint8_t nibble) {
            g.u.powerOn(g.clk);
            g.u.writeMode(0x4E);                       // MR1
            g.u.writeMode((uint8_t)(0x70 | nibble));   // MR2: board 0111 hi nibble + rate
            return g.u.params().baud;
        };
        CHECK(baudFor(0x0) == 50, "nibble 0 -> 50 baud");
        CHECK(baudFor(0x5) == 300, "nibble 5 -> 300 baud");
        CHECK(baudFor(0xE) == 9600, "nibble E -> 9600 baud");
        CHECK(baudFor(0xF) == 19200, "nibble F -> 19200 baud");
    }

    SECTION("SS-1 2651 -- status polarity: TxRDY=D0, RxRDY=D1, DCD/DSR asserted");
    {
        UartRig g;
        g.program();
        uint8_t s = g.status();
        CHECK((s & kTxRDY) != 0, "an idle transmitter is ready: D0 set");
        CHECK((s & kRxRDY) == 0, "nobody typed: D1 clear");
        CHECK((s & kDCD) != 0 && (s & kDSR) != 0,
              "DCD (D6) and DSR (D7) read asserted for a byte-clean transport");
    }

    SECTION("SS-1 2651 -- TxRDY is a DEADLINE, not a flag");
    {
        UartRig g;
        g.program();
        CHECK((g.status() & kTxRDY) != 0, "idle: ready");

        // DOUBLE-buffered (#899): a byte to an idle transmitter goes straight to the shift
        // register, so TxRDY stays set and only TxEMT drops. A second byte fills the holding
        // register and drops TxRDY until the first has left.
        g.u.writeData('X', g.clk);
        CHECK((g.status() & kTxRDY) != 0, "one byte: straight to the shift register -- still ready");
        CHECK((g.status() & kTxEMT) == 0, "but TxEMT (D2) drops: the shift register is busy");
        g.u.writeData('Y', g.clk);
        CHECK((g.status() & kTxRDY) == 0, "a second byte while the first shifts: TxRDY clears -- BUSY");
        CHECK(g.tty->out() == "XY", "and both characters really are on the line");

        g.advance(kChar - kBit);
        CHECK((g.status() & kTxRDY) == 0, "a bit time before X has left: still busy");
        g.advance(2 * kBit);
        CHECK((g.status() & kTxRDY) != 0, "X has left: Y moves to the shift register, TxRDY set");
        CHECK((g.status() & kTxEMT) == 0, "and TxEMT still clear while Y shifts");

        g.advance(kChar);
        CHECK((g.status() & kTxEMT) != 0, "Y has left too: TxEMT set, nothing left to send");
    }

    SECTION("SS-1 2651 -- TxEN (command D0) gates TxRDY and TxEMT, as a real SS-1 reads them");
    {
        // The status values a real SS-1 read in each state (#908).
        UartRig g;
        CHECK(g.status() == 0xC0, "power-on, not programmed: no TxRDY, no TxEMT");
        g.program(0x4E, 0x7E, 0x26);  // TxEN off, RxEN|DTR|RTS on
        CHECK(g.status() == 0xC0, "command 26 (TxEN off): no TxRDY, no TxEMT");
        g.u.writeCommand(0x27, g.clk);
        CHECK(g.status() == 0xC1, "command 27: TxRDY, but no TxEMT before a byte has left");
        g.u.writeData('A', g.clk);
        g.advance(kChar + kBit);
        CHECK(g.status() == 0xC5, "a byte has left: TxRDY and TxEMT");
        g.u.writeCommand(0x26, g.clk);
        CHECK(g.status() == 0xC0, "command 26 again: both clear");

        // A byte written while TxEN is off waits in the chip, and leaves when TxEN is set.
        g.u.writeData('B', g.clk);
        g.advance(kChar + kBit);
        CHECK(g.status() == 0xC0, "a byte written with TxEN off: still no TxRDY or TxEMT");
        CHECK(g.tty->out() == "A", "and the byte has not left");
        g.u.writeCommand(0x27, g.clk);
        CHECK(g.tty->out() == "AB", "command 27 sends the waiting byte");
        CHECK(g.status() == 0xC1, "it is in the shift register: TxRDY, no TxEMT");
        g.advance(kChar + kBit);
        CHECK(g.status() == 0xC5, "it has left: TxRDY and TxEMT");
    }

    SECTION("SS-1 2651 -- a character is clocked in over a frame time");
    {
        UartRig g;
        g.program();
        g.tty->feed("A");
        g.u.pump();
        g.status();               // the frame begins on this poll (the byte leaves the stream)
        CHECK((g.status() & kRxRDY) == 0, "mid-frame the byte is not yet ready");
        g.advance(kChar + kBit);  // ...one character time later it has finished arriving
        CHECK((g.status() & kRxRDY) != 0, "a character finished arriving: D1 set");
        CHECK(g.u.readData(g.clk) == 'A', "and the data port yields it");
        CHECK((g.status() & kRxRDY) == 0, "reading the data clears RxRDY");
    }

    SECTION("SS-1 2651 -- the receiver is gated by RxEN (command D2)");
    {
        UartRig g;
        g.program(0x4E, 0x7E, 0x01);  // command: TxEN only, RxEN OFF
        g.tty->feed("Z");
        g.u.pump();
        g.status();
        g.advance(kChar + kBit);
        CHECK((g.status() & kRxRDY) == 0, "with RxEN off, no character is taken off the line");

        g.u.writeCommand(0x05, g.clk);  // TxEN|RxEN
        g.status();                     // this poll starts the frame now that RxEN is on
        g.advance(kChar + kBit);        // ...and a char-time later it has arrived
        CHECK((g.status() & kRxRDY) != 0, "enabling RxEN lets the waiting byte come in");
        CHECK(g.u.readData(g.clk) == 'Z', "and it is the byte that was waiting");
    }

    SECTION("SS-1 2651 -- snapshot carries the programmed frame and baud");
    {
        UartRig g;
        g.program(0x4A, 0x75, 0x27);  // 7 data bits, 300 baud (nibble 5)
        StateWriter w;
        g.u.serialize(w);

        Clock clk2;
        clk2.setHz(4000000);
        clk2.advance(g.clk.now());
        Sig2651 u2{"serial"};
        u2.connect(std::make_unique<NullStream>());
        StateReader r(w.data());
        u2.deserialize(r);
        CHECK(r.ok(), "the snapshot reads back without underrun");
        CHECK(u2.params().dataBits == 7, "the restored chip keeps MR1's frame (7 data bits)");
        CHECK(u2.params().baud == 300, "and MR2's baud (300)");
    }

    // ---- the UART on a real bus ----

    SECTION("SS-1 board -- the four UART ports decode at base+12..+15");
    {
        Machine m;
        auto* ss1 = new Ss1Board();
        ss1->id = "ss1";
        m.bus.attach(ss1);

        auto decodes = [&](Cycle t, uint8_t port) {
            BusCycle c;
            c.type = t;
            c.addr = port;
            return ss1->decodes(c);
        };
        CHECK(decodes(Cycle::IoRead, 0x5C), "data port (5C) reads");
        CHECK(decodes(Cycle::IoWrite, 0x5C), "data port (5C) writes");
        CHECK(decodes(Cycle::IoRead, 0x5D), "status port (5D) reads");
        CHECK(!decodes(Cycle::IoWrite, 0x5D), "status port (5D) is read-only (write is the unused SYN reg)");
        CHECK(decodes(Cycle::IoRead, 0x5E) && decodes(Cycle::IoWrite, 0x5E), "mode port (5E) reads and writes");
        CHECK(decodes(Cycle::IoRead, 0x5F) && decodes(Cycle::IoWrite, 0x5F), "command port (5F) reads and writes");
        // The math socket (+8/+9) is unpopulated -- those ports float.
        CHECK(!decodes(Cycle::IoRead, 0x58) && !decodes(Cycle::IoRead, 0x59),
              "the empty math socket (58/59) floats");
    }

    SECTION("SS-1 board -- receive end to end over the bus");
    {
        Machine     m;
        std::string err;
        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region rr;
        rr.kind = RegionKind::Ram;
        rr.at   = 0;
        rr.size = 0x10000;
        mem->addRegion(rr, err);
        setProperty(*mem, "fill", "zero", err);

        auto* ss1 = dynamic_cast<Ss1Board*>(m.add("ss1", "ss0", err));
        CHECK(ss1 != nullptr, "the 'ss1' board type is registered");
        auto s = std::make_unique<ScriptedStream>();
        ScriptedStream* tty = s.get();
        ss1->uart().connect(std::move(s));

        m.add("z80", "cpu0", err);
        setProperty(*(dynamic_cast<Board*>(m.find("cpu0"))), "clock_hz", "4000000", err);
        m.power();

        // Program the UART through the bus (MR1, MR2, command) then receive a byte.
        m.bus.ioWrite(0x5E, 0x4E);  // MR1: 8N1
        m.bus.ioWrite(0x5E, 0x7E);  // MR2: 9600
        m.bus.ioWrite(0x5F, 0x27);  // command: TxEN|RxEN|DTR|RTS
        tty->feed("Q");
        ss1->pump();
        m.clock.advance(kChar + kBit);
        CHECK((m.bus.ioRead(0x5D) & kRxRDY) != 0, "IN 5D shows RxRDY once the frame arrived");
        CHECK(m.bus.ioRead(0x5C) == 'Q', "IN 5C reads the received byte");

        // Transmit: OUT the data port, the byte lands on the line.
        m.bus.ioWrite(0x5C, '!');
        CHECK(tty->out() == "!", "OUT 5C transmits");
    }

    SECTION("SS-1 board -- the RxRDY interrupt jumper (off by default)");
    {
        Machine     m;
        std::string err;
        m.bus.setVerify(true);  // re-derive pin 73 every cycle -- catch a missing intChanged()
        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region rr;
        rr.kind = RegionKind::Ram;
        rr.at   = 0;
        rr.size = 0x10000;
        mem->addRegion(rr, err);
        setProperty(*mem, "fill", "zero", err);

        auto* ss1 = dynamic_cast<Ss1Board*>(m.add("ss1", "ss0", err));
        auto  s   = std::make_unique<ScriptedStream>();
        ScriptedStream* tty = s.get();
        ss1->uart().connect(std::move(s));

        m.add("z80", "cpu0", err);
        setProperty(*(dynamic_cast<Board*>(m.find("cpu0"))), "clock_hz", "4000000", err);
        m.power();

        m.bus.ioWrite(0x5E, 0x4E);
        m.bus.ioWrite(0x5E, 0x7E);
        m.bus.ioWrite(0x5F, 0x27);

        // Default jumper is none: a received byte raises RxRDY but no interrupt.
        tty->feed("A");
        ss1->pump();
        m.clock.advance(kChar + kBit);
        CHECK(ss1->uart().rxReady(), "the byte arrived (RxRDY up)");
        CHECK(!m.bus.intPending(), "but with the jumper at none, no interrupt is asserted");

        // Strap the interrupt to pINT: now the same RxRDY raises /INT. setUnitProperty
        // re-settles the board (configChanged -> refresh -> intChanged), the same path the
        // monitor's SET takes.
        CHECK(setUnitProperty(*ss1, "serial", "interrupt", "int", err),
              "route the UART interrupt to pINT");
        CHECK(m.bus.intPending(), "RxRDY now raises /INT through the jumper");
        CHECK(m.bus.ioRead(0x5C) == 'A', "the ISR reads the data port...");
        CHECK(!m.bus.intPending(), "...which clears RxRDY, so /INT drops");
    }

    // =====================================================================
    // Phase 3 -- the 8253 interval timer.
    // =====================================================================

    SECTION("SS-1 8253 -- mode 0: interrupt on terminal count");
    {
        TimerRig g;
        g.t.writeControl(ctrl(0, kLsb, 0), g.now);  // counter 0, LSB, mode 0, binary
        CHECK(!g.t.out(0, g.now), "mode 0: OUT is low the moment the control word lands");

        g.t.writeCounter(0, 100, g.now);  // N = 100
        CHECK(!g.t.out(0, g.now), "mode 0: still low with the count loaded");
        CHECK(g.t.count(0, g.now) == 100, "the count reads back the value just loaded");

        g.advance(40);
        CHECK(g.t.count(0, g.now) == 60, "40 ticks later the count has counted down to 60");

        g.advance(59);  // 99 ticks total
        CHECK(!g.t.out(0, g.now), "one tick short of terminal count, OUT is still low");
        g.advance(1);   // 100 ticks: terminal count
        CHECK(g.t.out(0, g.now), "at terminal count OUT goes high");
        g.advance(5000);
        CHECK(g.t.out(0, g.now), "and in mode 0 it stays high");
    }

    SECTION("SS-1 8253 -- the counter-latch command freezes a 16-bit read");
    {
        TimerRig g;
        g.t.writeControl(ctrl(0, kBoth, 0), g.now);  // counter 0, LSB-then-MSB, mode 0
        g.t.writeCounter(0, 0x00, g.now);            // LSB
        g.t.writeCounter(0, 0x02, g.now);            // MSB -> N = 0x0200, counting starts
        g.advance(0x100);                            // 256 ticks -> count 0x0100

        g.t.writeControl(ctrl(0, 0 /*latch*/, 0), g.now);  // Counter Latch Command
        g.advance(50);  // the counter keeps running, but the latched value must not move

        uint8_t lo = g.t.readCounter(0, g.now);
        uint8_t hi = g.t.readCounter(0, g.now);
        CHECK(((hi << 8) | lo) == 0x0100,
              "the latch froze the count at 0x0100 despite 50 more ticks");
        // After both bytes are read the latch releases: a fresh read tracks live again.
        uint8_t lo2 = g.t.readCounter(0, g.now);
        uint8_t hi2 = g.t.readCounter(0, g.now);
        CHECK(((hi2 << 8) | lo2) < 0x0100, "reading both bytes released the latch");
    }

    SECTION("SS-1 8253 -- mode 2: rate generator");
    {
        TimerRig g;
        g.t.writeControl(ctrl(1, kLsb, 2), g.now);  // counter 1, LSB, mode 2
        g.t.writeCounter(1, 4, g.now);              // N = 4
        CHECK(g.t.out(1, g.now), "mode 2: OUT idles high");

        g.advance(3);  // the count would read 1 here
        CHECK(!g.t.out(1, g.now), "mode 2: OUT drops low for the one tick before reload");
        CHECK(g.t.count(1, g.now) == 1, "mode 2 counts N..1");
        g.advance(1);  // reload
        CHECK(g.t.out(1, g.now), "mode 2: OUT is high again and the period reloaded");
        CHECK(g.t.count(1, g.now) == 4, "and the count is back at N");
        g.advance(3);
        CHECK(!g.t.out(1, g.now), "mode 2: it pulses low again exactly one period later");
    }

    SECTION("SS-1 8253 -- mode 3: square wave (even and odd counts)");
    {
        TimerRig g;
        g.t.writeControl(ctrl(2, kLsb, 3), g.now);  // counter 2, LSB, mode 3
        g.t.writeCounter(2, 4, g.now);              // N = 4 -> 2 high, 2 low
        CHECK(g.t.out(2, g.now), "mode 3: high for the first half of the period");
        g.advance(1);
        CHECK(g.t.out(2, g.now), "still high at tick 1");
        g.advance(1);
        CHECK(!g.t.out(2, g.now), "low for the second half (tick 2)");
        g.advance(1);
        CHECK(!g.t.out(2, g.now), "still low at tick 3");
        g.advance(1);
        CHECK(g.t.out(2, g.now), "high again at the start of the next period");

        // An odd count spends the extra tick HIGH: N=5 -> 3 high, 2 low.
        TimerRig h;
        h.t.writeControl(ctrl(2, kLsb, 3), h.now);
        h.t.writeCounter(2, 5, h.now);
        h.advance(2);
        CHECK(h.t.out(2, h.now), "odd N: still high at tick 2 (the extra tick is high)");
        h.advance(1);
        CHECK(!h.t.out(2, h.now), "odd N: low from tick 3");
    }

    SECTION("SS-1 8253 -- mode 3 read-back: steps by 2, reloads N each half period");
    {
        // 16 latched reads of counter 2 on a real SS-1 (#908), the first 32 ticks after the
        // load and each one 76 ticks after the last. The same reads of a mode 2 load of 1001
        // fit the same 32 and 76.
        struct Real { uint16_t n; uint16_t reads[16]; };
        static const Real kReal[] = {
            {1000, {936, 784, 632, 480, 328, 176, 24, 872, 720, 568, 416, 264, 112, 960, 808, 656}},
            {1001, {938, 786, 634, 482, 330, 178, 26, 874, 722, 570, 418, 266, 114, 964, 812, 660}},
            {1003, {940, 788, 636, 484, 332, 180, 28, 878, 726, 574, 422, 270, 118, 970, 818, 666}},
        };
        for (const Real& r : kReal) {
            TimerRig g;
            g.t.writeControl(ctrl(2, kBoth, 3), g.now);
            g.t.writeCounter(2, (uint8_t)(r.n & 0xFF), g.now);
            g.t.writeCounter(2, (uint8_t)(r.n >> 8), g.now);
            g.advance(32);
            for (int i = 0; i < 16; ++i) {
                g.t.writeControl(ctrl(2, 0 /*latch*/, 0), g.now);
                uint8_t lo = g.t.readCounter(2, g.now);
                uint8_t hi = g.t.readCounter(2, g.now);
                std::string what = "mode 3 N=" + std::to_string(r.n) + " read " +
                                   std::to_string(i) + ": got " +
                                   std::to_string((hi << 8) | lo) + ", the real board read " +
                                   std::to_string(r.reads[i]);
                CHECK(((hi << 8) | lo) == r.reads[i], what.c_str());
                g.advance(76);
            }
        }

        // A whole period of a small odd count: N, then N-1 .. 2 high; N, then N-3 .. 2 low.
        // It never reads 0 or more than N.
        TimerRig h;
        h.t.writeControl(ctrl(2, kLsb, 3), h.now);
        h.t.writeCounter(2, 7, h.now);
        const uint16_t want7[] = {7, 6, 4, 2, 7, 4, 2, 7};
        for (int i = 0; i < 8; ++i) {
            std::string what = "mode 3 N=7 tick " + std::to_string(i) + ": got " +
                               std::to_string(h.t.count(2, h.now));
            CHECK(h.t.count(2, h.now) == want7[i], what.c_str());
            h.advance(1);
        }
    }

    SECTION("SS-1 8253 -- nextEdge reports the next OUT transition");
    {
        TimerRig g;
        g.t.writeControl(ctrl(0, kLsb, 0), g.now);
        g.t.writeCounter(0, 100, g.now);
        uint64_t base = g.now;
        CHECK(g.t.nextEdge(0, g.now) == base + 100,
              "mode 0 nextEdge is the terminal-count moment");
        g.advance(100);
        CHECK(g.t.nextEdge(0, g.now) == 0, "past terminal count mode 0 has no further edge");

        // A rate generator always has a next edge (it is periodic).
        g.t.writeControl(ctrl(1, kLsb, 2), g.now);
        g.t.writeCounter(1, 10, g.now);
        uint64_t b1 = g.now;
        CHECK(g.t.nextEdge(1, g.now) == b1 + 9, "mode 2 next edge is the low pulse (count 1)");
    }

    SECTION("SS-1 8253 -- BCD counting");
    {
        TimerRig g;
        g.t.writeControl(ctrl(0, kLsb, 0, /*bcd*/ true), g.now);
        g.t.writeCounter(0, 0x50, g.now);  // BCD 50
        CHECK(g.t.count(0, g.now) == 0x50, "a BCD count reads back as BCD 50");
        g.advance(10);
        CHECK(g.t.count(0, g.now) == 0x40, "and decrements in BCD to 40");
        g.advance(40);
        CHECK(g.t.out(0, g.now), "BCD mode 0 hits terminal count after 50 ticks, not 0x50");
    }

    SECTION("SS-1 8253 -- the counter clock is 2 MHz regardless of the CPU clock");
    {
        // A 4 MHz CPU, paced: the 8253 counts the 2 MHz bus CLOCK, so one tick takes two
        // T-states.
        Clock    clk;
        BusClock bc;
        clk.setHz(4000000);
        bc.power(clk);
        Intel8253 t{"timer"};
        t.powerOn();
        t.writeControl(ctrl(0, kLsb, 0), bc.now(clk));
        t.writeCounter(0, 10, bc.now(clk));
        clk.advance(19);  // 9 counter ticks
        CHECK(!t.out(0, bc.now(clk)), "9 counter ticks (19 T-states) short of TC: OUT still low");
        clk.advance(1);    // the 20th T-state completes the 10th tick
        CHECK(t.out(0, bc.now(clk)), "the 10th counter tick (20 T-states) reaches terminal count");
    }

    SECTION("SS-1 8253 -- snapshot carries the running counters");
    {
        TimerRig g;
        g.advance(1000);
        g.t.writeControl(ctrl(0, kLsb, 0), g.now);
        g.t.writeCounter(0, 200, g.now);
        g.advance(60);  // count = 140
        StateWriter w;
        g.t.serialize(w, g.now);

        // The count of edges does not travel in a snapshot: the machine it is restored
        // into has its own, here younger than the counter's phase.
        uint64_t  now2 = 5;
        Intel8253 t2{"timer"};
        StateReader r(w.data());
        t2.deserialize(r, now2);
        CHECK(r.ok(), "the snapshot reads back without underrun");
        CHECK(t2.count(0, now2) == 140, "the restored counter keeps its phase");
        CHECK(t2.nextEdge(0, now2) == now2 + 140, "and reaches terminal count 140 ticks on");
        CHECK(t2.count(0, now2 + 40) == 100, "and counts on from there");
    }

    // ---- the timer on a real bus ----

    SECTION("SS-1 board -- the timer ports decode at base+4..+7");
    {
        Machine m;
        auto* ss1 = new Ss1Board();
        ss1->id = "ss1";
        m.bus.attach(ss1);

        auto decodes = [&](Cycle t, uint8_t port) {
            BusCycle c;
            c.type = t;
            c.addr = port;
            return ss1->decodes(c);
        };
        CHECK(decodes(Cycle::IoRead, 0x54) && decodes(Cycle::IoWrite, 0x54), "counter 0 (54)");
        CHECK(decodes(Cycle::IoRead, 0x55) && decodes(Cycle::IoWrite, 0x55), "counter 1 (55)");
        CHECK(decodes(Cycle::IoRead, 0x56) && decodes(Cycle::IoWrite, 0x56), "counter 2 (56)");
        CHECK(decodes(Cycle::IoWrite, 0x57), "the control word (57) is written");
        CHECK(!decodes(Cycle::IoRead, 0x57), "the control word (57) is write-only");
    }

    SECTION("SS-1 board -- program and read the timer over the bus");
    {
        Machine     m;
        std::string err;
        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region rr;
        rr.kind = RegionKind::Ram;
        rr.at   = 0;
        rr.size = 0x10000;
        mem->addRegion(rr, err);
        setProperty(*mem, "fill", "zero", err);

        auto* ss1 = dynamic_cast<Ss1Board*>(m.add("ss1", "ss0", err));
        CHECK(ss1 != nullptr, "the 'ss1' board type is registered");

        m.add("z80", "cpu0", err);
        setProperty(*(dynamic_cast<Board*>(m.find("cpu0"))), "clock_hz", "4000000", err);
        m.power();

        // Counter 0, mode 0, LSB only, N = 200. The counter clocks at 2 MHz, so a 4 MHz
        // CPU takes two T-states per tick: 200 ticks = 400 T-states to terminal count.
        m.bus.ioWrite(0x57, ctrl(0, kLsb, 0));
        m.bus.ioWrite(0x54, 200);
        m.clock.advance(200);  // 100 ticks
        CHECK(m.bus.ioRead(0x54) == 100, "IN 54 reads the counter halfway down");
        CHECK(!ss1->timer().out(0, m.bus.busClock().now(m.clock)), "and OUT is still low");
        m.clock.advance(200);  // 100 more ticks -> terminal count
        CHECK(ss1->timer().out(0, m.bus.busClock().now(m.clock)),
              "OUT is high once the counter reaches zero");
    }

    // ---- flat out: the counters count pin 49 in real time (issue #836) ----

    SECTION("SS-1 board -- flat out, the 8253 counts host time, not T-states");
    {
        Machine  m;
        uint64_t ns = 1000000000;  // the host clock, frozen until the test moves it
        m.bus.busClock().setHostNs([&ns] { return ns; });
        auto* ss1 = buildIntMachine(m);
        std::string err;
        setProperty(*(dynamic_cast<Board*>(m.find("cpu0"))), "clock_hz", "0", err);  // flat out

        // Counter 0, mode 0, LSB then MSB, N = 50000 (25 ms of CLOCK).
        m.bus.ioWrite(0x57, ctrl(0, kBoth, 0));
        m.bus.ioWrite(0x54, 50000 & 0xFF);
        m.bus.ioWrite(0x54, 50000 >> 8);
        auto read0 = [&] {
            m.bus.ioWrite(0x57, ctrl(0, 0 /*latch*/, 0));
            uint8_t lo = m.bus.ioRead(0x54);
            uint8_t hi = m.bus.ioRead(0x54);
            return (hi << 8) | lo;
        };
        m.clock.advance(1000000);
        CHECK(read0() == 50000, "a million T-states with the host clock frozen: no count");
        ns += 1000000;  // 1 ms
        CHECK(read0() == 48000, "1 ms of host time counts 2000 ticks of the 2 MHz CLOCK");
        (void)ss1;
    }

    SECTION("SS-1 board -- flat out, a terminal count interrupts in host time");
    {
        Machine  m;
        uint64_t ns = 1000000000;
        m.bus.busClock().setHostNs([&ns] { return ns; });
        buildIntMachine(m);
        std::string err;
        setProperty(*(dynamic_cast<Board*>(m.find("cpu0"))), "clock_hz", "0", err);

        initSs1Pics(m, 0x50, (uint8_t)(~(1u << 7) & 0xFFu), (uint8_t)(~(1u << 1) & 0xFFu));
        m.bus.ioWrite(0x57, ctrl(0, kLsb, 0));  // Timer 0, mode 0
        m.bus.ioWrite(0x54, 50);                // N = 50 ticks = 25 us
        m.clock.advance(1000000);
        m.pump();
        CHECK(!m.bus.intPending(), "a million T-states, host clock frozen: no terminal count");
        ns += 25000;  // 25 us = 50 ticks
        m.pump();
        CHECK(m.bus.intPending(), "25 us of host time: the TC raises slave IR1 -> pin 73");
        CHECK(m.bus.intAck() == 0xCD, "INTA 1: the CALL opcode");
        CHECK(m.bus.intAck() == 0x04, "INTA 2: address low (0x2204)");
        CHECK(m.bus.intAck() == 0x22, "INTA 3: address high");
    }

    // =====================================================================
    // Phase 4 -- the dual 8259A interrupt controllers.
    // =====================================================================

    SECTION("SS-1 8259A -- ICW init and the MCS-80/85 CALL address");
    {
        Intel8259a p{"pic"};
        initPic(p, 0x1F, 0x02, kIcw4Mcs, 0x00);  // single, level, interval 4, base 0x0200
        CHECK(!p.is8086(), "ICW4 D0 clear -> MCS-80/85 mode");
        CHECK(p.callAddress(0) == 0x0200, "level 0 -> CALL 0200");
        CHECK(p.callAddress(3) == 0x020C, "level 3 -> CALL 0200 + 3*4");
        CHECK(p.callAddress(7) == 0x021C, "level 7 -> CALL 0200 + 7*4");

        initPic(p, 0x1B, 0x02, kIcw4Mcs, 0x00);  // interval 8 (ICW1 D2 clear)
        CHECK(p.callAddress(2) == 0x0210, "interval 8: level 2 -> CALL 0200 + 2*8");
    }

    SECTION("SS-1 8259A -- priority, masking, fully-nested in-service");
    {
        Intel8259a p{"pic"};
        initPic(p, 0x1F, 0x20, kIcw4Mcs, 0x00);  // level, single, all unmasked
        CHECK(p.winner(0x00) == -1, "no request -> no winner");
        CHECK(p.winner(1u << 2) == 2, "IR2 asking, IR2 wins");
        CHECK(p.winner((1u << 2) | (1u << 5)) == 2,
              "IR2 outranks IR5 (a lower number is higher priority)");
        CHECK(p.acknowledge((1u << 2) | (1u << 5)) == 2, "the acknowledge latches IR2 in service");
        CHECK(p.winner((1u << 2) | (1u << 5)) == -1,
              "IR2 in service blocks IR2 and everything lower");
        CHECK(p.winner((1u << 0) | (1u << 5)) == 0, "...but IR0 (higher) still preempts");
        p.write(false, 0x20);  // non-specific EOI: clears the highest in-service (IR2)
        CHECK(p.winner((1u << 2) | (1u << 5)) == 2,
              "after EOI, IR2 (line still high) is serviceable again");
        p.write(true, (uint8_t)(~(1u << 5) & 0xFFu));  // OCW1: unmask only IR5
        CHECK(p.winner(0xFF) == 5, "the mask lets only IR5 through");
    }

    SECTION("SS-1 8259A -- EOI variants and priority rotation");
    {
        Intel8259a p{"pic"};
        initPic(p, 0x1F, 0x20, kIcw4Mcs, 0x00);
        p.acknowledge(1u << 3);  // IR3 in service
        CHECK(p.winner((1u << 3) | (1u << 4)) == -1, "IR3 in service blocks IR4 (lower)");
        p.write(false, (uint8_t)(0x60 | 3));  // specific EOI IR3
        CHECK(p.winner((1u << 3) | (1u << 4)) == 3,
              "specific EOI frees IR3; IR3 outranks IR4");
        p.acknowledge(1u << 3);
        p.write(false, 0xA0);  // rotate on non-specific EOI -> IR3 becomes lowest priority
        CHECK(p.winner((1u << 3) | (1u << 4)) == 4,
              "after rotate-on-EOI, IR3 is lowest so IR4 now outranks it");
    }

    SECTION("SS-1 8259A -- edge- vs level-triggered request sensing");
    {
        Intel8259a p{"pic"};
        initPic(p, 0x13, 0x20, kIcw4Mcs, 0x00);  // 0x13: EDGE (D3 clear), single, IC4
        p.senseEdges(1u << 2);  // IR2 high since init -> no rising edge (edge sense reset)
        CHECK(p.winner(1u << 2) == -1, "edge mode: a line high since init is not an edge");
        p.senseEdges(0x00);
        p.senseEdges(1u << 2);  // a fresh low->high transition
        CHECK(p.winner(1u << 2) == 2, "a rising edge latches the request");
        p.acknowledge(1u << 2);
        p.write(false, 0x20);   // EOI
        p.senseEdges(1u << 2);  // still high, but no NEW edge
        CHECK(p.winner(1u << 2) == -1,
              "edge mode: a still-high line does not re-request after EOI");

        Intel8259a q{"pic"};
        initPic(q, 0x1B, 0x20, kIcw4Mcs, 0x00);  // level (D3 set)
        CHECK(q.winner(1u << 2) == 2, "level mode: a high line is a request with no edge");
    }

    SECTION("SS-1 8259A -- an edge request needs IR held high until the acknowledge (#838)");
    {
        Intel8259a p{"pic"};
        initPic(p, 0x13, 0x20, kIcw4Mcs, 0x00);  // edge, single, IC4
        p.senseEdges(0x00);
        p.senseEdges(1u << 2);  // a rising edge latches IR2
        CHECK(p.winner(1u << 2) == 2, "the edge is latched while IR2 is high");
        CHECK(p.winner(0x00) == -1, "IR2 fell before the acknowledge: no request");
        CHECK(p.irr(0x00) == 0x00, "and the IRR shows no request");
        CHECK(!p.intOut(0x00), "so INT drops with the line");
        CHECK(p.acknowledge(0x00) == 7, "an acknowledge with no request is the default IR7");
        CHECK(p.isr() == 0x00, "the default IR7 sets no in-service bit");
        CHECK(p.callAddress(7) == 0x2038, "and its CALL is the IR7 address (interval 8)");

        Intel8259a q{"pic"};
        initPic(q, 0x13, 0x20, kIcw4Mcs, 0x00);
        q.senseEdges(0x00);
        q.senseEdges(1u << 7);
        CHECK(q.acknowledge(1u << 7) == 7 && q.isr() == 0x80,
              "a real IR7 request sets ISR bit 7 -- the way to tell it from a default IR7");

        Intel8259a l{"pic"};
        initPic(l, 0x1B, 0x20, kIcw4Mcs, 0x00);  // level mode
        CHECK(l.acknowledge(0x00) == 7 && l.isr() == 0x00,
              "level mode: nothing requesting at the acknowledge is the default IR7 too");
    }

    SECTION("SS-1 8259A -- 8086 mode yields a single vector byte");
    {
        Intel8259a p{"pic"};
        initPic(p, 0x1B, 0x40, kIcw4_8086, 0x00);
        CHECK(p.is8086(), "ICW4 D0 set -> 8086 mode");
        CHECK(p.vector8086(3) == 0x43, "8086 vector = (ICW2 & F8) | level");
    }

    SECTION("SS-1 8259A -- the cascade routes a slave source through master IR7");
    {
        Intel8259a master{"m"}, slave{"s"};
        // master: cascade (D1 clear), level, interval 4, IC4; slave on IR7.
        master.powerOn();
        master.write(false, 0x1D);
        master.write(true, 0x02);
        master.write(true, 0x80);       // ICW3: slave present on IR7
        master.write(true, kIcw4Mcs);
        master.write(true, 0x00);
        slave.powerOn();
        slave.write(false, 0x1D);
        slave.write(true, 0x22);        // vectors at 0x2200
        slave.write(true, 0x07);        // ICW3: slave ID 7
        slave.write(true, kIcw4Mcs);
        slave.write(true, 0x00);

        uint8_t sLive = 1u << 6;  // slave IR6 asking
        CHECK(slave.intOut(sLive), "the slave's INT is up");
        uint8_t mLive = slave.intOut(sLive) ? (uint8_t)(1u << 7) : 0;  // -> master IR7
        CHECK(master.winner(mLive) == 7, "the master resolves the cascade line, IR7");
        CHECK(master.acknowledge(mLive) == 7, "the master acknowledges IR7...");
        CHECK(slave.acknowledge(sLive) == 6, "...and the slave supplies the source, IR6");
        CHECK(slave.callAddress(6) == 0x2218, "the vector is the SLAVE's: 0x2200 + 6*4");
    }

    // ---- the two controllers on a real bus, driving real interrupts ----

    SECTION("SS-1 board -- the 8259A ports decode at base+0..+3");
    {
        Machine m;
        auto* ss1 = new Ss1Board();
        ss1->id = "ss1";
        m.bus.attach(ss1);
        auto decodes = [&](Cycle t, uint8_t port) {
            BusCycle c;
            c.type = t;
            c.addr = port;
            return ss1->decodes(c);
        };
        for (uint8_t p = 0x50; p <= 0x53; ++p) {
            CHECK(decodes(Cycle::IoRead, p) && decodes(Cycle::IoWrite, p),
                  "an 8259A port (50-53) reads and writes");
        }
        CHECK(!decodes(Cycle::IntAck, 0),
              "an unprogrammed board claims no IntAck cycle -- the bus floats to RST 7");
        // And it never pulls PHANTOM* -- our CPU issues an INTA per injected byte, so the
        // CALL bytes come from the board, not memory (see the board header).
        BusCycle ic;
        ic.type = Cycle::IntAck;
        CHECK(!ss1->assertsPhantom(ic), "the SS-1 needs no PHANTOM* trick in this simulator");
    }

    SECTION("SS-1 board -- a timer terminal count interrupts through the cascade");
    {
        Machine m;
        m.bus.setVerify(true);
        buildIntMachine(m);

        // master unmask IR7 (the slave); slave unmask IR1 (Timer 0 OUT).
        initSs1Pics(m, 0x50, /*mMask*/ (uint8_t)(~(1u << 7) & 0xFFu), /*sMask*/ (uint8_t)(~(1u << 1) & 0xFFu));
        m.bus.ioWrite(0x57, ctrl(0, kLsb, 0));  // Timer 0, mode 0
        m.bus.ioWrite(0x54, 50);                // N = 50 ticks
        CHECK(!m.bus.intPending(), "before terminal count, nothing is asking");

        m.clock.advance(100);  // 50 ticks at 2 MHz on a 4 MHz CPU
        CHECK(m.bus.intPending(),
              "the timer's TC raises slave IR1 -> master IR7 -> pin 73");
        // The acknowledge drives the SLAVE's CALL vector for IR1: 0x2200 + 1*4 = 0x2204.
        CHECK(m.bus.intAck() == 0xCD, "INTA 1: the CALL opcode");
        CHECK(m.bus.intAck() == 0x04, "INTA 2: address low (0x2204)");
        CHECK(m.bus.intAck() == 0x22, "INTA 3: address high");
        CHECK(!m.bus.intPending(),
              "the in-service bits are set now, so pin 73 has dropped");
    }

    SECTION("SS-1 board -- edge mode: a tick that falls before the acknowledge is lost (#838)");
    {
        Machine m;
        m.bus.setVerify(true);
        buildIntMachine(m);

        // Timer 0, mode 3, N = 100: OUT high 50 ticks, low 50 ticks = 100 T-states each.
        // Programmed first, so OUT is already high when ICW1 resets the edge sense.
        m.bus.ioWrite(0x57, ctrl(0, kLsb, 3));
        m.bus.ioWrite(0x54, 100);
        // Both PICs edge-triggered (ICW1 15 = cascade, edge, interval 4, IC4); master IR7
        // (the slave) and slave IR1 (Timer 0 OUT) unmasked.
        initSs1Pics(m, 0x50, (uint8_t)(~(1u << 7) & 0xFFu), (uint8_t)(~(1u << 1) & 0xFFu), 0x15);
        CHECK(!m.bus.intPending(), "OUT high since ICW1 is not an edge");

        m.clock.advance(150);  // in the low half
        CHECK(!m.bus.intPending(), "OUT low: nothing asking");
        m.clock.advance(100);  // in the high half: a rising edge
        CHECK(m.bus.intPending(), "the rising edge raises slave IR1 -> master IR7 -> pin 73");
        m.clock.advance(100);  // low again, and nobody acknowledged (a long DI window)
        CHECK(!m.bus.intPending(),
              "IR1 fell before the acknowledge: the request and pin 73 are gone");
        m.clock.advance(100);  // the next rising edge
        CHECK(m.bus.intPending(), "the next tick asks again");
        CHECK(m.bus.intAck() == 0xCD, "INTA 1: CALL");
        CHECK(m.bus.intAck() == 0x04, "INTA 2: the slave's IR1 address low (0x2204)");
        CHECK(m.bus.intAck() == 0x22, "INTA 3: address high");
    }

    SECTION("SS-1 board -- an acknowledge with nothing requesting is the default IR7 (#838)");
    {
        Machine m;
        auto* ss1 = buildIntMachine(m);
        initSs1Pics(m, 0x50, (uint8_t)(~(1u << 7) & 0xFFu), 0xFF);  // slave all masked
        BusCycle ic;
        ic.type = Cycle::IntAck;
        // decodes() would not claim this cycle -- nothing is asking -- so drive the board
        // directly: this is the path an IR that drops after INT was sampled would take.
        CHECK(ss1->read(ic) == 0xCD, "INTA 1: CALL, not a floating RST 7");
        CHECK(ss1->read(ic) == 0x1C, "INTA 2: the slave's IR7 address low (0x221C)");
        CHECK(ss1->read(ic) == 0x22, "INTA 3: address high");
        CHECK(ss1->slave().isr() == 0x00, "the default IR7 sets no slave in-service bit");
    }

    SECTION("SS-1 board -- the UART's RxRDY interrupts through the cascade");
    {
        Machine m;
        m.bus.setVerify(true);
        auto* ss1 = buildIntMachine(m);
        auto  s   = std::make_unique<ScriptedStream>();
        ScriptedStream* tty = s.get();
        ss1->uart().connect(std::move(s));

        m.bus.ioWrite(0x5E, 0x4E);  // UART MR1: 8N1
        m.bus.ioWrite(0x5E, 0x7E);  // MR2: 9600
        m.bus.ioWrite(0x5F, 0x27);  // command: TxEN|RxEN|DTR|RTS
        // Unmask master IR7 and slave IR7 (RxRDY). Mask slave IR6 (TxRDY) so the idle
        // transmitter does not interrupt and steal the acknowledge.
        initSs1Pics(m, 0x50, (uint8_t)(~(1u << 7) & 0xFFu), (uint8_t)(~(1u << 7) & 0xFFu));

        tty->feed("Q");
        ss1->pump();
        m.clock.advance(kChar + kBit);
        CHECK(m.bus.intPending(),
              "a received character raises slave IR7 (RxRDY) -> master IR7 -> pin 73");
        // The slave's CALL vector for IR7: 0x2200 + 7*4 = 0x221C.
        CHECK(m.bus.intAck() == 0xCD, "INTA 1: CALL");
        CHECK(m.bus.intAck() == 0x1C, "INTA 2: address low (0x221C)");
        CHECK(m.bus.intAck() == 0x22, "INTA 3: address high");
    }

    SECTION("SS-1 board -- connectStream installs a pre-built line on the serial unit");
    {
        // The --mcp console binding hands the board a scripted line wrapped in the console's
        // transforms. Without this seam it fell back to a bare line, and strip7out was lost.
        Machine m;
        auto* ss1 = buildIntMachine(m);
        auto            s   = std::make_unique<ScriptedStream>();
        ScriptedStream* raw = s.get();
        std::string     err;
        CHECK(ss1->connectStream("serial", std::move(s), err), "connectStream binds the serial unit");
        CHECK(ss1->unitStream("serial") == raw, "and its line is the stream we handed in");

        m.bus.ioWrite(0x5E, 0x4E);  // UART MR1: 8N1
        m.bus.ioWrite(0x5E, 0x7E);  // MR2: 9600
        m.bus.ioWrite(0x5F, 0x27);  // command: TxEN|RxEN|DTR|RTS
        raw->feed("W");
        ss1->pump();
        m.clock.advance(kChar + kBit);
        CHECK((m.bus.ioRead(0x5D) & kRxRDY) != 0, "RxRDY: the byte arrived");
        CHECK(m.bus.ioRead(0x5C) == 'W', "a byte on the pre-built line reaches the guest");

        CHECK(!ss1->connectStream("tty", std::make_unique<NullStream>(), err),
              "connectStream to a missing unit fails");
    }

    SECTION("SS-1 board -- the master prioritizes the S-100 VI lines");
    {
        Machine m;
        m.bus.setVerify(true);
        auto* ss1 = buildIntMachine(m);
        auto  s   = std::make_unique<ScriptedStream>();
        ScriptedStream* tty = s.get();
        ss1->uart().connect(std::move(s));

        // Route the UART's RxRDY straight to VI2 (the legacy jumper) and unmask master
        // IR2 -- the SS-1's master is the machine's VI priority encoder, so a VI2 request
        // comes back in through master IR2 and is vectored with the MASTER's vector.
        std::string err;
        CHECK(setUnitProperty(*ss1, "serial", "interrupt", "vi2", err), "strap RxRDY to VI2");
        initSs1Pics(m, 0x50, (uint8_t)(~(1u << 2) & 0xFFu), 0xFF);  // master: only IR2; slave: all masked

        m.bus.ioWrite(0x5E, 0x4E);
        m.bus.ioWrite(0x5E, 0x7E);
        m.bus.ioWrite(0x5F, 0x27);
        tty->feed("A");
        ss1->pump();
        m.clock.advance(kChar + kBit);
        CHECK(m.bus.viLines() == (1u << 2), "the UART is pulling VI2");
        CHECK(ss1->intWinner() == 2, "the master resolves VI2 as the winner");
        CHECK(m.bus.intPending(), "and drives pin 73 for it");
        CHECK(m.bus.intAck() == 0xCD, "INTA 1: CALL");
        CHECK(m.bus.intAck() == 0x08, "INTA 2: the MASTER's vector low (0x0200 + 2*4 = 0x0208)");
        CHECK(m.bus.intAck() == 0x02, "INTA 3: address high");
    }

    SECTION("SS-1 board -- VI7* is slave IR0 (J8 pins 8-9), through the cascade");
    {
        // Another board pulls VI7: an IO-4 strapped to put its idle transmitter on VI7.
        Machine m;
        m.bus.setVerify(true);
        auto*       ss1 = buildIntMachine(m);
        std::string err;
        auto*       io  = m.add("io4", "io0", err);
        CHECK(io != nullptr, "add an IO-4");
        if (io) CHECK(setUnitProperty(*io, "a", "tx_int", "vi7", err), "strap its transmitter to VI7");
        m.power();
        // Master: only IR7 (the cascade). Slave: only IR0.
        initSs1Pics(m, 0x50, (uint8_t)(~(1u << 7) & 0xFFu), (uint8_t)(~(1u << 0) & 0xFFu));

        CHECK(m.bus.viLines() == (1u << 7), "the IO-4 is pulling VI7");
        CHECK((ss1->slaveLive() & 0x01) != 0, "VI7 is on slave IR0");
        CHECK(m.bus.intPending(), "slave IR0 -> master IR7 -> pin 73");
        CHECK(ss1->intWinner() == 7, "SHOW BUS IRQ reports VI7 as the winner");
        CHECK(m.bus.intAck() == 0xCD, "INTA 1: CALL");
        CHECK(m.bus.intAck() == 0x00, "INTA 2: the slave's IR0 vector low (0x2200 + 0*4)");
        CHECK(m.bus.intAck() == 0x22, "INTA 3: address high");

        if (io) setUnitProperty(*io, "a", "tx_int", "none", err);
        CHECK(m.bus.viLines() == 0, "the IO-4 lets go of VI7");
        CHECK((ss1->slaveLive() & 0x01) == 0, "and slave IR0 drops");
        CHECK(ss1->intWinner() == -1, "nothing wins");
    }

    SECTION("SS-1 board -- TxRDY on slave IR6 waits for TxEN, as on a real SS-1");
    {
        Machine m;
        auto* ss1 = buildIntMachine(m);
        m.bus.ioWrite(0x5E, 0x4E);  // UART MR1: 8N1
        m.bus.ioWrite(0x5E, 0x7E);  // MR2: 9600
        CHECK((ss1->slaveLive() & (1u << 6)) == 0, "not programmed: slave IR6 is low");
        m.bus.ioWrite(0x5F, 0x26);  // command: RxEN|DTR|RTS, TxEN off
        CHECK((ss1->slaveLive() & (1u << 6)) == 0, "command 26 (TxEN off): slave IR6 is low");
        m.bus.ioWrite(0x5F, 0x27);  // TxEN on
        CHECK((ss1->slaveLive() & (1u << 6)) != 0, "command 27: TxRDY raises slave IR6");
        CHECK(m.bus.ioRead(0x5D) == 0xC1, "and the status port reads 41 with DSR and DCD");
    }

    SECTION("SS-1 board -- an interrupt actually vectors the CPU through the CALL");
    {
        Machine m;
        buildIntMachine(m, "8080");

        // EI; HLT at 0x0000. The CPU halts with interrupts enabled; the run loop keeps
        // time turning while a deadline is queued, so the timer fires and the injected
        // CALL redirects execution to the slave's IR1 vector at 0x2204, where a HLT waits.
        m.bus.memWrite(0x0000, 0xFB);  // EI
        m.bus.memWrite(0x0001, 0x76);  // HLT
        m.bus.memWrite(0x2204, 0x76);  // HLT at the timer-0 vector

        initSs1Pics(m, 0x50, (uint8_t)(~(1u << 7) & 0xFFu), (uint8_t)(~(1u << 1) & 0xFFu));
        m.bus.ioWrite(0x57, ctrl(0, kLsb, 0));
        m.bus.ioWrite(0x54, 40);  // a short interval so it fires within the run cap

        m.debug.add(BreakKind::Pc, 0x2204, 0x2204);
        RunResult r = m.debug.run(100000);
        CHECK(m.cpu()->pc() == 0x2204,
              "the timer interrupt vectored the CPU to the CALL target at 0x2204");
        (void)r;
    }

    SECTION("SS-1 board -- snapshot carries the 8259A state");
    {
        Machine m;
        auto* ss1 = buildIntMachine(m);
        initSs1Pics(m, 0x50, 0x7F, 0xF5);  // master IR7 unmasked; slave IR1/IR3 unmasked
        ss1->slave().acknowledge(1u << 1);  // put slave IR1 in service

        StateWriter w;
        ss1->serialize(w);

        Machine m2;
        auto* b2 = buildIntMachine(m2);
        StateReader r(w.data());
        b2->deserialize(r);
        CHECK(r.ok(), "the snapshot reads back without underrun");
        CHECK(b2->master().imr() == 0x7F, "the restored master keeps its mask");
        CHECK(b2->slave().imr() == 0xF5, "the restored slave keeps its mask");
        CHECK(b2->slave().isr() == (1u << 1), "the restored slave keeps its in-service bit");
    }

    // =====================================================================
    // The math socket (#393) -- base+8 data, base+9 command/status.
    // =====================================================================

    SECTION("SS-1 board -- the math socket is empty by default and floats");
    {
        Machine m;
        auto* ss1 = buildIntMachine(m);
        CHECK(storedText(ss1->properties(), "math", "?") == "none", "math defaults to none (the stock board)");
        CHECK(m.bus.ioRead(0x58) == 0xFF, "IN 58 floats with the socket empty");
        CHECK(m.bus.ioRead(0x59) == 0xFF, "IN 59 floats with the socket empty");
        bool listed = false;
        for (const MapEntry& e : ss1->ioMap()) listed |= e.lo == 0x58 || e.lo == 0x59;
        CHECK(!listed, "an empty socket puts nothing in the I/O map");
    }

    // A machine with a 9511A in the socket. 4 MHz CPU: two T-states per pin-49 tick.
    auto mathMachine = [](Machine& m) {
        auto* ss1 = buildIntMachine(m);
        std::string err;
        CHECK(setProperty(*ss1, "math", "9511a", err), "fit a 9511A: math = 9511a");
        m.bus.takeWaitStates();
        return ss1;
    };
    auto push16 = [](Machine& m, uint16_t v) {
        m.bus.ioWrite(0x58, (uint8_t)v);  // LS byte first
        m.bus.ioWrite(0x58, (uint8_t)(v >> 8));
    };
    auto push32 = [](Machine& m, uint32_t v) {
        for (int i = 0; i < 4; ++i) m.bus.ioWrite(0x58, (uint8_t)(v >> (8 * i)));
    };
    auto pop32 = [](Machine& m) {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v = (v << 8) | m.bus.ioRead(0x58);  // MS byte first
        return v;
    };
    constexpr uint8_t kSadd = 0x6C, kFadd = 0x10, kSr = 0x80;
    constexpr uint64_t kSaddT = 2 * 18, kFaddT = 2 * 368;  // T-states at 4 MHz

    SECTION("SS-1 board -- a 9511A in the socket, through the bus");
    {
        Machine m;
        auto* ss1 = mathMachine(m);
        bool listed = false;
        for (const MapEntry& e : ss1->ioMap()) listed |= e.lo == 0x58 || e.lo == 0x59;
        CHECK(listed, "a fitted chip puts +8/+9 in the I/O map");

        // The SS-1 manual's test program: 3000H + 4000H by SADD gives 70H then 00H.
        push16(m, 0x3000);
        push16(m, 0x4000);
        m.bus.ioWrite(0x59, kSadd);
        CHECK(m.bus.ioRead(0x59) == Am9511a::kBusy, "IN 59 at once reads BUSY");
        m.clock.advance(kSaddT);
        CHECK(m.bus.ioRead(0x59) == 0x00, "after 18 ticks: not busy, status 00");
        CHECK(m.bus.ioRead(0x58) == 0x70, "IN 58: the MS byte of the sum, 70H");
        CHECK(m.bus.ioRead(0x58) == 0x00, "IN 58: then the LS byte, 00H");
    }

    SECTION("SS-1 board -- 2 wait states on every math access, PAUSE holds longer");
    {
        Machine m;
        auto* ss1 = mathMachine(m);
        m.bus.ioWrite(0x58, 0x00);
        CHECK(m.bus.takeWaitStates() == 2, "a data write while idle: the board's 2 wait states");
        m.bus.ioRead(0x59);
        CHECK(m.bus.takeWaitStates() == 2, "a status read: 2 wait states");

        push32(m, 0x03A00000);  // 5.0
        push32(m, 0x03C00000);  // 6.0
        m.bus.ioWrite(0x59, kFadd);
        m.bus.takeWaitStates();
        m.bus.ioRead(0x59);
        CHECK(m.bus.takeWaitStates() == 2, "a status read while BUSY is not held by PAUSE");
        m.clock.advance(100);
        CHECK(m.bus.ioRead(0x58) == 0x04, "a data read while BUSY gets the result (MS byte)");
        CHECK(m.bus.takeWaitStates() == kFaddT - 100,
              "PAUSE held it for the rest of FADD's 368 ticks, in T-states");
        CHECK(!ss1->math().endOut(m.bus.busClock().now(m.clock) + 1000),
              "the read landed when PAUSE let go, so it cleared END: no stale END after it");
        CHECK(m.bus.ioRead(0x58) == 0xB0, "the rest of 11.0 = 04B00000");
        m.bus.ioRead(0x58);
        m.bus.ioRead(0x58);

        // A command written while another runs starts when that one ends. (The run loop
        // charges held wait states to the clock; here the clock is moved past FADD by hand.)
        m.clock.advance(kFaddT);
        push16(m, 1);
        push16(m, 2);
        uint64_t t0 = m.bus.busClock().now(m.clock);
        m.bus.ioWrite(0x59, kSadd);
        m.bus.ioWrite(0x59, kSadd);
        CHECK(ss1->math().busyUntil() == t0 + 2 * 18,
              "the second command waited for the first: it ends 36 ticks after the first began");
    }

    SECTION("SS-1 board -- math END raises slave IR5 and vectors to 2214H");
    {
        Machine m;
        mathMachine(m);
        initSs1Pics(m, 0x50, (uint8_t)(~(1u << 7) & 0xFFu), (uint8_t)(~(1u << 5) & 0xFFu));
        push16(m, 0x3000);
        push16(m, 0x4000);
        m.bus.ioWrite(0x59, kSadd);
        m.clock.advance(kSaddT - 2);
        CHECK(!m.bus.intPending(), "no interrupt while the command runs");
        m.clock.advance(2);
        CHECK(m.bus.intPending(), "the command ends: END on slave IR5 pulls pin 73");
        CHECK(m.bus.intAck() == 0xCD, "INTA 1: CALL");
        CHECK(m.bus.intAck() == 0x14, "INTA 2: address low (0x2200 + 5*4 = 0x2214)");
        CHECK(m.bus.intAck() == 0x22, "INTA 3: address high");
    }

    SECTION("SS-1 board -- a status read clears END, so slave IR5 drops");
    {
        Machine m;
        auto* ss1 = mathMachine(m);
        initSs1Pics(m, 0x50, (uint8_t)(~(1u << 7) & 0xFFu), (uint8_t)(~(1u << 5) & 0xFFu));
        m.bus.ioWrite(0x59, kSadd);
        m.clock.advance(kSaddT);
        CHECK(ss1->slaveLive() & (1u << 5), "END is on slave IR5");
        CHECK(m.bus.intPending(), "and pulls pin 73");
        m.bus.ioRead(0x59);
        CHECK(!(ss1->slaveLive() & (1u << 5)), "IN 59 cleared END (EACK* is tied high)");
        CHECK(!m.bus.intPending(), "and the level-triggered request is gone");
    }

    SECTION("SS-1 board -- SVREQ (SR bit) on slave IR4, cleared by an SR = 0 command");
    {
        Machine m;
        auto* ss1 = mathMachine(m);
        initSs1Pics(m, 0x50, (uint8_t)(~(1u << 7) & 0xFFu), (uint8_t)(~(1u << 4) & 0xFFu));
        m.bus.ioWrite(0x59, kSr | kSadd);
        m.clock.advance(kSaddT);
        CHECK(ss1->slaveLive() & (1u << 4), "an SR = 1 command ends: SVREQ on slave IR4");
        CHECK(m.bus.intAck() == 0xCD && m.bus.intAck() == 0x10 && m.bus.intAck() == 0x22,
              "and it vectors to 2210H");
        m.bus.ioRead(0x59);
        CHECK(ss1->slaveLive() & (1u << 4), "a status read leaves SVREQ (SVACK* is tied high)");
        m.bus.ioWrite(0x59, kSadd);
        m.clock.advance(kSaddT);
        CHECK(!(ss1->slaveLive() & (1u << 4)), "an SR = 0 command clears SVREQ at its end");
    }

    SECTION("SS-1 board -- a bus reset resets the math chip (CLEAR)");
    {
        Machine m;
        auto* ss1 = mathMachine(m);
        m.bus.ioWrite(0x58, 0x12);  // one stray byte: the stack is misaligned
        m.bus.ioWrite(0x59, kSadd);
        m.reset(Reset::Bus);
        uint64_t now = m.bus.busClock().now(m.clock);
        CHECK(!ss1->math().busy(now), "RESET stops the command");
        CHECK(ss1->math().describe(now).find("sp=0 ") != std::string::npos,
              "and puts the stack pointer back to its start");
        CHECK(m.bus.ioRead(0x59) == 0x00, "status reads 00 after reset");
    }

    SECTION("SS-1 board -- snapshot carries the math chip, and the time left on a command");
    {
        Machine m;
        auto* ss1 = mathMachine(m);
        push32(m, 0x03A00000);
        push32(m, 0x03C00000);
        m.bus.ioWrite(0x59, kFadd);
        m.clock.advance(100);  // 50 of FADD's 368 ticks gone

        StateWriter w;
        ss1->serialize(w);

        Machine m2;
        auto* b2 = mathMachine(m2);
        StateReader r(w.data());
        b2->deserialize(r);
        CHECK(r.ok(), "the snapshot reads back without underrun");
        uint64_t now2 = m2.bus.busClock().now(m2.clock);
        CHECK(b2->math().busyUntil() == now2 + 318, "the restored command has 318 ticks left");
        m2.clock.advance(2 * 318);
        CHECK(pop32(m2) == 0x04B00000, "and the restored stack holds 5 + 6 = 11");
    }

    SECTION("SS-1 board -- a 9512 in the socket: the manual's 9512 test, END on IR5");
    {
        Machine m;
        auto* ss1 = buildIntMachine(m);
        std::string err;
        CHECK(setProperty(*ss1, "math", "9512", err), "fit a 9512: math = 9512");
        CHECK(storedText(ss1->properties(), "math", "?") == "9512", "math reads back 9512");
        bool listed = false;
        for (const MapEntry& e : ss1->ioMap()) listed |= e.lo == 0x58 && e.note.find("9512") == 0;
        CHECK(listed, "the I/O map names the 9512");
        initSs1Pics(m, 0x50, (uint8_t)(~(1u << 7) & 0xFFu), (uint8_t)(~(1u << 5) & 0xFFu));
        m.bus.takeWaitStates();

        // The SS-1 manual's 9512 path: push 1.0 twice (TBL3), SADD (01H), pop 2.0 (TBL4).
        push32(m, 0x3F800000);
        push32(m, 0x3F800000);
        m.bus.ioWrite(0x59, 0x01);
        CHECK(ss1->fpu().busy(m.bus.busClock().now(m.clock)), "the 9512 runs it, not the 9511A");
        m.clock.advance(2 * 512 - 2);
        CHECK(!m.bus.intPending(), "SADD runs 512 ticks");
        m.clock.advance(2);
        CHECK(m.bus.intPending(), "then END on slave IR5 pulls pin 73 (J6 set for the 9512)");
        CHECK(pop32(m) == 0x40000000, "IN 58 x 4: 40H 00H 00H 00H, MS byte first");
        CHECK(!m.bus.intPending(), "the reads cleared END");

        // ERROR goes to J7 only: a divide by zero raises no slave IR.
        push32(m, 0x3F800000);
        push32(m, 0);
        m.bus.ioWrite(0x59, 0x04);  // SDIV by 0
        m.clock.advance(2 * 264);
        uint64_t now = m.bus.busClock().now(m.clock);
        CHECK(ss1->fpu().errOut(now), "the chip's ERR is up");
        CHECK((ss1->slaveLive() & 0x31) == (1u << 5),
              "but of IR0, IR4 and IR5 only END's IR5 is up: ERROR reaches no slave IR");

        CHECK(setProperty(*ss1, "math", "9511a", err), "swap in a 9511A");
        CHECK(m.bus.ioRead(0x59) == 0x00, "the new chip starts from reset");
    }
}
