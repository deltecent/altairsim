#include "test.h"

#include "chips/tms5501.h"
#include "core/clock.h"
#include "core/statefile.h"
#include "host/stream.h"

#include <memory>

using namespace altair;

namespace {

// A TMS 5501 on the bench: the chip, a clock to pace it, and a scripted terminal
// on its line. Built by hand -- there is no FDC board yet, and the point of a chip
// test is to prove the chip against its data sheet with no card in sight.
//
// THE CLOCK COMES FIRST. A chip test that lets the Clock be destroyed before the
// chip is a use-after-free the Mac hides and Windows SEGFAULTs on (the sanitizer
// oracle in the memory notes) -- so `Clock` is the first member and the chip the
// last, and they tear down in that order.
struct Rig {
    Clock            clk;
    Tms5501          chip{"tms0"};
    ScriptedStream*  tty = nullptr;

    Rig() {
        auto s = std::make_unique<ScriptedStream>();
        tty    = s.get();
        chip.connect(std::move(s));
        chip.powerOn(clk);
        // 9600 baud, one stop bit: D6 (9600) + D7 (one stop). A known frame, so the
        // deadline arithmetic below is exact rather than "whatever the default was".
        chip.writeBaud(0xC0);
    }

    // A character time at the rig's frame: 1 start + 8 data + 1 stop = 10 bits at
    // 9600 baud, 2 MHz. The receiver and transmitter are both paced by it.
    uint64_t charT() const { return (uint64_t)(2000000 * 10 / 9600); }  // 2083
    void lineTime() { clk.advance(charT() + 1); }
};

} // namespace

void test_tms5501() {
    SECTION("TMS 5501 -- status is ACTIVE-HIGH (TBE=D7, RDA=D6)");
    {
        Rig g;
        // An idle UART is READY TO SEND and has NOTHING TO SAY. On a 5501 both of
        // those are the true-sense bits -- the inverse of a 6850's status polarity,
        // and the reason the RDOS console driver spins on D7=0 / D6=0.
        uint8_t s = g.chip.readStatus(g.clk);
        CHECK((s & 0x80) != 0, "TBE set -- the transmitter is idle");
        CHECK((s & 0x40) == 0, "RDA clear -- nobody has typed");

        g.tty->feed("A");
        s = g.chip.readStatus(g.clk);
        CHECK((s & 0x40) != 0, "RDA set once a character arrives");
        CHECK(g.chip.readData(g.clk) == 'A', "and the data register yields it");
        CHECK((g.chip.readStatus(g.clk) & 0x40) == 0, "reading the data clears RDA");
    }

    SECTION("TMS 5501 -- TBE is a DEADLINE, not a flag");
    {
        // The character occupies the line for its whole character time; TBE is false
        // until it has finished leaving. Same mechanism the 6850's TDRE is built on,
        // and the same reason a guest can time the line by watching it.
        Rig g;
        CHECK((g.chip.readStatus(g.clk) & 0x80) != 0, "TBE set before we send");

        g.chip.writeData('X', g.clk);
        CHECK((g.chip.readStatus(g.clk) & 0x80) == 0, "TBE CLEAR the instant we send");

        g.clk.advance(g.charT() - 2);
        CHECK((g.chip.readStatus(g.clk) & 0x80) == 0, "still clear one bit-time early");

        g.clk.advance(2);
        CHECK((g.chip.readStatus(g.clk) & 0x80) != 0, "set once the character has left");
        CHECK(g.tty->out() == "X", "and the byte really went out the line");
    }

    SECTION("TMS 5501 -- the receiver is PACED: a second byte waits its character time");
    {
        Rig g;
        g.tty->feed("HI");

        (void)g.chip.readStatus(g.clk);
        CHECK(g.chip.readData(g.clk) == 'H', "the first character arrives");

        g.clk.advance(g.charT() - 2);
        CHECK((g.chip.readStatus(g.clk) & 0x40) == 0,
              "the SECOND is still on the wire one bit-time early");

        g.clk.advance(2);
        CHECK((g.chip.readStatus(g.clk) & 0x40) != 0, "and lands when its character time is up");
        CHECK(g.chip.readData(g.clk) == 'I', "...and it is the byte that was sent");
    }

    SECTION("TMS 5501 -- the baud register is one-hot, highest bit wins");
    {
        // reference/Cromemco TU-ART.md 4: D0..D6 select 110..9600; if several are set
        // the HIGHEST rate wins; if none, the channel is disabled. D7 is the stop-bit
        // count, orthogonal to the rate.
        Rig g;
        g.chip.writeBaud(0x80 | 0x01);  // D0 = 110
        CHECK(g.chip.baud() == 110, "D0 selects 110 baud");

        g.chip.writeBaud(0x80 | 0x40);  // D6 = 9600
        CHECK(g.chip.baud() == 9600, "D6 selects 9600 baud");

        g.chip.writeBaud(0x80 | 0x41);  // D0 and D6 both set
        CHECK(g.chip.baud() == 9600, "with two bits set, the highest rate wins");

        g.chip.writeBaud(0x80);         // no rate bit at all
        CHECK(g.chip.baud() == 0, "no rate bit -> the channel is disabled (baud 0)");
    }

    SECTION("TMS 5501 -- HBD (command D4) octuples the rate");
    {
        // The 5501's only route to 19200/38400/76800: the same one-hot table, times
        // eight. RDOS sets HBD in the command register, not a new baud code.
        Rig g;
        g.chip.writeBaud(0x80 | 0x40);   // 9600
        CHECK(g.chip.baud() == 9600, "9600 with HBD off");

        g.chip.writeCommand(0x10, g.clk);  // HBD
        CHECK(g.chip.baud() == 76800, "HBD octuples 9600 to 76800");

        g.chip.writeCommand(0x00, g.clk);  // HBD off
        CHECK(g.chip.baud() == 9600, "and clearing HBD returns to 9600");
    }

    SECTION("TMS 5501 -- command RES resets the chip");
    {
        // RDOS issues a RES (command D0) at boot. It is a strobe, not a latch (unlike
        // a 6850's divide-11), and it clears the receiver: RDA goes away, TBE comes up.
        Rig g;
        g.tty->feed("Z");
        (void)g.chip.readStatus(g.clk);
        CHECK((g.chip.readStatus(g.clk) & 0x40) != 0, "a character is waiting");

        g.chip.writeCommand(0x01, g.clk);  // RES
        CHECK((g.chip.readStatus(g.clk) & 0x40) == 0, "RES clears RDA");
        CHECK((g.chip.readStatus(g.clk) & 0x80) != 0, "...and leaves the transmitter ready");

        // ...and it does NOT unplug the terminal. A warm reset that dropped the console
        // would be a baffling thing to debug.
        CHECK(g.chip.endpoint() == "scripted", "the endpoint survives a RES");
    }

    SECTION("TMS 5501 -- the LINE IS 8-BIT CLEAN");
    {
        // The 5501 is fixed at 8 data bits and no parity; the high bit is data. 0xC5
        // is 'E'|0x80, the last byte of MITS BASIC's "MEMORY SIZE?" -- inbound it is
        // just as often the payload of an XMODEM block.
        Rig g;
        g.tty->feed(std::string(1, (char)0xC5));
        (void)g.chip.readStatus(g.clk);
        CHECK(g.chip.readData(g.clk) == 0xC5, "0xC5 reaches the guest with bit 7 intact");

        g.chip.writeData((uint8_t)0xC5, g.clk);
        CHECK(g.tty->out() == std::string(1, (char)0xC5), "...and outbound too");
    }

    SECTION("TMS 5501 -- an unconnected line is not an error");
    {
        // An unconnected UART sits with TBE set forever and software that writes to it
        // works fine and talks to nobody -- there is no null-pointer branch in the path.
        Rig g;
        g.chip.disconnect();
        CHECK(g.chip.endpoint() == "null", "disconnect binds the null stream");
        CHECK((g.chip.readStatus(g.clk) & 0x80) != 0, "and it is READY TO SEND");
        g.chip.writeData('x', g.clk);  // must not crash, must not block
        CHECK((g.chip.readStatus(g.clk) & 0x40) == 0, "and never has anything to say");
    }

    SECTION("TMS 5501 -- irq() is always false in Phase 1");
    {
        // The interrupt controller and the five timers are inert stubs. A byte on the
        // line, a timer written, a mask set -- none of it raises the INT pin, on
        // purpose (see the class note). This is the one function the interrupt PR
        // makes real.
        Rig g;
        g.chip.writeMask(0xFF);
        g.chip.writeTimer(0, 0x01);
        g.tty->feed("!");
        (void)g.chip.readStatus(g.clk);
        CHECK(!g.chip.irq(g.clk), "no timer, no receive interrupt -- INT stays low");
        CHECK(g.chip.readIntAddr() == 0xFF, "and the interrupt-address register reads 'none pending'");
    }

    SECTION("TMS 5501 -- snapshot round-trip carries the live state");
    {
        // The guest programs baud/HBD at runtime, so they travel; the strap and the
        // stream (a host handle) do not. Prove a mid-character deadline survives.
        Rig g;
        g.chip.writeBaud(0x80 | 0x08);   // 1200 baud, one stop
        g.chip.writeData('Q', g.clk);    // transmitter now busy until its deadline

        StateWriter w;
        g.chip.serialize(w);

        Rig h;                            // a fresh chip
        StateReader r(w.data());
        h.chip.deserialize(r);
        CHECK(r.ok(), "the blob read back without underrunning");
        CHECK(h.chip.baud() == 1200, "the programmed baud rate came across");
    }
}
