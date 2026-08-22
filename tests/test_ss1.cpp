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
#include "chips/sig2651.h"
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

// A bare 8253 on a Clock -- the CHIP, exercised directly. The clock runs at the 8253's
// own 2 MHz counter rate, so one advanced T-state is exactly one counter tick and the
// mode arithmetic reads straight off the numbers.
struct TimerRig {
    Clock     clk;
    Intel8253 t{"timer"};

    TimerRig() {
        clk.setHz(2000000);  // one counter tick per T-state
        t.powerOn(clk);
    }
    void advance(uint64_t dt) { clk.advance(dt); }
};

// Control-word helpers: SC1/SC0 (counter) | RL1/RL0 | M2/M1/M0 | BCD.
constexpr uint8_t kLsb = 1, kBoth = 3;  // read/load format (MSB-only is unused here)
uint8_t ctrl(int counter, uint8_t rl, uint8_t mode, bool bcd = false) {
    return (uint8_t)((counter << 6) | (rl << 4) | (mode << 1) | (bcd ? 1 : 0));
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

        g.u.writeData('X', g.clk);
        CHECK((g.status() & kTxRDY) == 0, "the instant a character goes out, TxRDY clears -- BUSY");
        CHECK((g.status() & kTxEMT) == 0, "and TxEMT (D2) too: the shift register is busy");
        CHECK(g.tty->out() == "X", "and the character really is on the line");

        g.advance(kChar + kBit);
        CHECK((g.status() & kTxRDY) != 0, "one character time later it is ready again");
        CHECK((g.status() & kTxEMT) != 0, "and TxEMT set: nothing left to send");
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
        g.t.writeControl(ctrl(0, kLsb, 0), g.clk);  // counter 0, LSB, mode 0, binary
        CHECK(!g.t.out(0, g.clk), "mode 0: OUT is low the moment the control word lands");

        g.t.writeCounter(0, 100, g.clk);  // N = 100
        CHECK(!g.t.out(0, g.clk), "mode 0: still low with the count loaded");
        CHECK(g.t.count(0, g.clk) == 100, "the count reads back the value just loaded");

        g.advance(40);
        CHECK(g.t.count(0, g.clk) == 60, "40 ticks later the count has counted down to 60");

        g.advance(59);  // 99 ticks total
        CHECK(!g.t.out(0, g.clk), "one tick short of terminal count, OUT is still low");
        g.advance(1);   // 100 ticks: terminal count
        CHECK(g.t.out(0, g.clk), "at terminal count OUT goes high");
        g.advance(5000);
        CHECK(g.t.out(0, g.clk), "and in mode 0 it stays high");
    }

    SECTION("SS-1 8253 -- the counter-latch command freezes a 16-bit read");
    {
        TimerRig g;
        g.t.writeControl(ctrl(0, kBoth, 0), g.clk);  // counter 0, LSB-then-MSB, mode 0
        g.t.writeCounter(0, 0x00, g.clk);            // LSB
        g.t.writeCounter(0, 0x02, g.clk);            // MSB -> N = 0x0200, counting starts
        g.advance(0x100);                            // 256 ticks -> count 0x0100

        g.t.writeControl(ctrl(0, 0 /*latch*/, 0), g.clk);  // Counter Latch Command
        g.advance(50);  // the counter keeps running, but the latched value must not move

        uint8_t lo = g.t.readCounter(0, g.clk);
        uint8_t hi = g.t.readCounter(0, g.clk);
        CHECK(((hi << 8) | lo) == 0x0100,
              "the latch froze the count at 0x0100 despite 50 more ticks");
        // After both bytes are read the latch releases: a fresh read tracks live again.
        uint8_t lo2 = g.t.readCounter(0, g.clk);
        uint8_t hi2 = g.t.readCounter(0, g.clk);
        CHECK(((hi2 << 8) | lo2) < 0x0100, "reading both bytes released the latch");
    }

    SECTION("SS-1 8253 -- mode 2: rate generator");
    {
        TimerRig g;
        g.t.writeControl(ctrl(1, kLsb, 2), g.clk);  // counter 1, LSB, mode 2
        g.t.writeCounter(1, 4, g.clk);              // N = 4
        CHECK(g.t.out(1, g.clk), "mode 2: OUT idles high");

        g.advance(3);  // the count would read 1 here
        CHECK(!g.t.out(1, g.clk), "mode 2: OUT drops low for the one tick before reload");
        CHECK(g.t.count(1, g.clk) == 1, "mode 2 counts N..1");
        g.advance(1);  // reload
        CHECK(g.t.out(1, g.clk), "mode 2: OUT is high again and the period reloaded");
        CHECK(g.t.count(1, g.clk) == 4, "and the count is back at N");
        g.advance(3);
        CHECK(!g.t.out(1, g.clk), "mode 2: it pulses low again exactly one period later");
    }

    SECTION("SS-1 8253 -- mode 3: square wave (even and odd counts)");
    {
        TimerRig g;
        g.t.writeControl(ctrl(2, kLsb, 3), g.clk);  // counter 2, LSB, mode 3
        g.t.writeCounter(2, 4, g.clk);              // N = 4 -> 2 high, 2 low
        CHECK(g.t.out(2, g.clk), "mode 3: high for the first half of the period");
        g.advance(1);
        CHECK(g.t.out(2, g.clk), "still high at tick 1");
        g.advance(1);
        CHECK(!g.t.out(2, g.clk), "low for the second half (tick 2)");
        g.advance(1);
        CHECK(!g.t.out(2, g.clk), "still low at tick 3");
        g.advance(1);
        CHECK(g.t.out(2, g.clk), "high again at the start of the next period");

        // An odd count spends the extra tick HIGH: N=5 -> 3 high, 2 low.
        TimerRig h;
        h.t.writeControl(ctrl(2, kLsb, 3), h.clk);
        h.t.writeCounter(2, 5, h.clk);
        h.advance(2);
        CHECK(h.t.out(2, h.clk), "odd N: still high at tick 2 (the extra tick is high)");
        h.advance(1);
        CHECK(!h.t.out(2, h.clk), "odd N: low from tick 3");
    }

    SECTION("SS-1 8253 -- nextEdge reports the next OUT transition");
    {
        TimerRig g;
        g.t.writeControl(ctrl(0, kLsb, 0), g.clk);
        g.t.writeCounter(0, 100, g.clk);
        uint64_t base = g.clk.now();
        CHECK(g.t.nextEdge(0, g.clk) == base + 100,
              "mode 0 nextEdge is the terminal-count moment");
        g.advance(100);
        CHECK(g.t.nextEdge(0, g.clk) == 0, "past terminal count mode 0 has no further edge");

        // A rate generator always has a next edge (it is periodic).
        g.t.writeControl(ctrl(1, kLsb, 2), g.clk);
        g.t.writeCounter(1, 10, g.clk);
        uint64_t b1 = g.clk.now();
        CHECK(g.t.nextEdge(1, g.clk) == b1 + 9, "mode 2 next edge is the low pulse (count 1)");
    }

    SECTION("SS-1 8253 -- BCD counting");
    {
        TimerRig g;
        g.t.writeControl(ctrl(0, kLsb, 0, /*bcd*/ true), g.clk);
        g.t.writeCounter(0, 0x50, g.clk);  // BCD 50
        CHECK(g.t.count(0, g.clk) == 0x50, "a BCD count reads back as BCD 50");
        g.advance(10);
        CHECK(g.t.count(0, g.clk) == 0x40, "and decrements in BCD to 40");
        g.advance(40);
        CHECK(g.t.out(0, g.clk), "BCD mode 0 hits terminal count after 50 ticks, not 0x50");
    }

    SECTION("SS-1 8253 -- the counter clock is 2 MHz regardless of the CPU clock");
    {
        // A 4 MHz CPU: the 8253 still counts at 2 MHz, so one tick takes two T-states.
        Clock     clk;
        clk.setHz(4000000);
        Intel8253 t{"timer"};
        t.powerOn(clk);
        t.writeControl(ctrl(0, kLsb, 0), clk);
        t.writeCounter(0, 10, clk);
        clk.advance(19);  // 9 counter ticks
        CHECK(!t.out(0, clk), "9 counter ticks (19 T-states) short of TC: OUT still low");
        clk.advance(1);    // the 20th T-state completes the 10th tick
        CHECK(t.out(0, clk), "the 10th counter tick (20 T-states) reaches terminal count");
    }

    SECTION("SS-1 8253 -- snapshot carries the running counters");
    {
        TimerRig g;
        g.t.writeControl(ctrl(0, kLsb, 0), g.clk);
        g.t.writeCounter(0, 200, g.clk);
        g.advance(60);  // count = 140
        StateWriter w;
        g.t.serialize(w);

        Clock clk2;
        clk2.setHz(2000000);
        clk2.advance(g.clk.now());  // the restored machine is at the same emulated time
        Intel8253 t2{"timer"};
        StateReader r(w.data());
        t2.deserialize(r);
        CHECK(r.ok(), "the snapshot reads back without underrun");
        CHECK(t2.count(0, clk2) == 140,
              "the restored counter keeps counting from the same tick");
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
        CHECK(!ss1->timer().out(0, m.clock), "and OUT is still low");
        m.clock.advance(200);  // 100 more ticks -> terminal count
        CHECK(ss1->timer().out(0, m.clock), "OUT is high once the counter reaches zero");
    }
}
