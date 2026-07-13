// The MITS 88-VI/RTC -- src/boards/mits-88virtc.h, reference/88-VI-RTC.pdf.
//
// Two of the things pinned here are pinned BECAUSE THE MANUAL IS WRONG ABOUT THEM
// and the code that shipped in 1976 is right. If either of these ever goes red,
// re-read the PS2 monitor's service routine before you "fix" the test:
//
//   * bits 0-2 of the control port are the ONES-COMPLEMENT of the level (7-level).
//     The manual's prose (p.3) says otherwise; its own table (p.5) and the monitor
//     both say this.
//   * bit 3 SET makes the current-level register live. The manual's sentence parses
//     both ways; the monitor sets it on entry and clears it on exit.
//
// And one is pinned because it is the whole reason the PS2 package runs at all:
// an RTC whose RI jumper is not installed interrupts nobody, however enabled it is.

#include "test.h"

#include "boards/mits-2sio.h"
#include "boards/mits-88cpu.h"
#include "boards/mits-88virtc.h"
#include "boards/s100-memory.h"
#include "core/machine.h"
#include "host/stream.h"

#include <string>

using namespace altair;

namespace {

// A machine with an 88-VI/RTC and a 2SIO in it, wired by hand.
struct Rig {
    Machine         m;
    VirtcBoard*     vi  = nullptr;
    Sio2Board*      sio = nullptr;
    MemoryBoard*    mem = nullptr;
    ScriptedStream* tty = nullptr;

    Rig() {
        std::string err;

        // PARANOID MODE. This suite adds EIGHT MORE WIRES to the backplane, and the
        // failure mode of getting one wrong is a guest that waits forever for an
        // interrupt that already happened. Verify re-derives pin 73 AND all eight VI
        // lines on every instruction and aborts the moment a card disagrees.
        m.bus.setVerify(true);

        mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region r;
        r.kind = RegionKind::Ram;
        r.at   = 0;
        r.size = 0x10000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);

        m.add("8080", "cpu0", err);
        vi  = dynamic_cast<VirtcBoard*>(m.add("virtc", "vi0", err));
        sio = dynamic_cast<Sio2Board*>(m.add("2sio", "sio0", err));
        setProperty(*sio, "port", "10", err);  // hex: a port is on the wire

        auto s = std::make_unique<ScriptedStream>();
        tty    = s.get();
        sio->channel("a")->connect(std::move(s));

        m.power();
    }

    // OUT to the control port, the way the guest does it.
    void ctl(uint8_t v) { m.bus.ioWrite(0xFE, v); }

    // What the card drives during an interrupt acknowledge.
    uint8_t intAck() { return m.bus.intAck(); }
};

// The control byte a service routine at `level` writes, per the manual's table:
// bit 3 live + the ones-complement of the level, then ORI C0 for the two enables.
uint8_t levelByte(int level) { return (uint8_t)(0xC0 | 0x08 | (7 - level)); }

}  // namespace

void test_virtc() {
    SECTION("88-VI/RTC -- priority, vectors, and the RTC jumper that is not installed");

    // ---- the vector table. Level n -> RST n -> octal n0. ----
    CHECK(VirtcBoard::rstFor(0) == 0xC7, "level 0 -> RST 0 -> 0x0000");
    CHECK(VirtcBoard::rstFor(4) == 0xE7, "level 4 -> RST 4 -> 0x0020 (the manual's own example)");
    CHECK(VirtcBoard::rstFor(7) == 0xFF, "level 7 -> RST 7 -> 0x0038 -- 'vector 7'");

    // RST 7 is 0xFF, which is ALSO what an unclaimed bus floats to. Worth saying out
    // loud: the two roads to 0x0038 are different roads.
    {
        Rig g;
        CHECK(!g.m.bus.intPending(), "POC leaves the VI structure disabled -- pin 73 is up");
        CHECK(g.intAck() == 0xFF,
              "a disabled 88-VI does not claim IntAck; the bus floats 0xFF = RST 7");
    }

    // ---- the level ENCODING. This is the manual's self-contradiction, pinned. ----
    //
    // The monitor's ISR opens `MVI A,08 / ORI C0 / OUT FE` -- 0xC8 -- and it is the
    // level-7 handler. That is bit 3 set and bits 0-2 = 000 = 7-7. The manual's TABLE
    // agrees (level 7 -> MVI A,10Q = 0x08); its PROSE does not. The table wins.
    CHECK(levelByte(7) == 0xC8, "a level-7 handler writes 0xC8 -- exactly what PS2's ISR writes");
    CHECK(levelByte(2) == 0xCD, "...level 2 writes 0xCD (the manual's worked example, MVI A,15Q)");
    CHECK(levelByte(4) == 0xCB, "...level 4 writes 0xCB = 013Q, per the TABLE, not the prose's '100'");

    // ---- VI0 IS HIGHEST, VI7 IS LOWEST. The winner is the lowest-numbered line. ----
    {
        Rig g;
        g.ctl(0xC0);  // VI enabled, current-level register NOT live: anything may interrupt
        CHECK(g.vi->winner() == -1, "nothing pulling -> nobody wins");
        CHECK(!g.m.bus.intPending(), "...and pin 73 stays up");
    }

    // ---- THE RTC, AND THE JUMPER THAT ISN'T THERE. ----
    //
    // This is the PS2 machine exactly: the monitor's ISR does `ORI C0`, which enables
    // the RTC interrupt whether you like it or not, and there is no RTC handler. The
    // ONLY thing standing between that and a crash is an unsoldered wire.
    {
        Rig g;
        std::string err;
        CHECK(setProperty(*g.vi, "rtc_interrupt", "none", err), "RI unstrapped -- the PS2 machine");
        setProperty(*g.vi, "rtc_source", "line", err);
        setProperty(*g.vi, "rtc_divide", "1", err);

        g.ctl(0xC0);  // exactly what the monitor writes: VI on, RTC interrupt ON

        // Run a full second of emulated time. At 60 Hz / 1 that is sixty ticks.
        for (int i = 0; i < 200; ++i) g.m.clock.advance(20000);

        CHECK(!g.m.bus.intPending(),
              "the RTC fires into an unstrapped RI and interrupts NOBODY -- this is the "
              "whole reason MITS Programming System II can run with interrupts on");
        CHECK(g.m.clock.queued() == 0,
              "...and an unstrapped RTC arms no timer, so a HLT can still finish");
    }

    // ---- ...and with the jumper installed, it does interrupt. ----
    {
        Rig g;
        std::string err;
        CHECK(setProperty(*g.vi, "rtc_interrupt", "vi0", err), "RI -> VI0, the highest priority");
        setProperty(*g.vi, "rtc_source", "line", err);
        setProperty(*g.vi, "rtc_divide", "1", err);

        g.ctl(0xC0);  // VI structure on, RTC interrupt on, level register not live
        CHECK(!g.m.bus.intPending(), "nothing yet -- the interval has not elapsed");

        // 60 Hz at 2 MHz is 33,333 T-states. Step past one.
        for (int i = 0; i < 40; ++i) g.m.clock.advance(1000);

        CHECK(g.m.bus.intPending(), "the interval elapsed: the RTC pulls VI0, the 88-VI pulls pin 73");
        CHECK(g.vi->winner() == 0, "and VI0 is the winner");
        CHECK(g.intAck() == 0xC7, "so the card jams RST 0 onto the bus");

        // "The service routine that handles the RTC must output bit 4 high" to clear
        // the flip-flop. Nothing else does it -- not reading a port, not time passing.
        g.ctl(0xC0 | 0x10);
        CHECK(!g.m.bus.intPending(), "bit 4 clears the RTC's flip-flop and drops the line");
    }

    // ---- the RI strap will not accept `int`, because the hardware cannot. ----
    {
        Rig g;
        std::string err;
        CHECK(!setProperty(*g.vi, "rtc_interrupt", "int", err),
              "RI does not go to pin 73: 'a system designed to use the 88-VI may not have "
              "any I/O board strapped for single level interrupt'");
    }

    // ---- END TO END: a 6850 on VI7 becomes an RST 7, through the card. ----
    //
    // This is the PS2 console's exact wiring, and it is the thing that did not work
    // before this board existed: the strap was accepted and the wire went nowhere.
    {
        Rig g;
        std::string err;
        setProperty(*g.vi, "rtc_interrupt", "none", err);
        CHECK(setUnitProperty(*g.sio, "a", "interrupt", "vi7", err), "console 6850 -> VI7");

        g.ctl(0xC0);  // VI structure enabled, nothing being serviced

        // Master reset, then 8N2 with the receive interrupt enabled (control bit 7).
        g.m.bus.ioWrite(0x10, 0x03);
        g.m.bus.ioWrite(0x10, 0x95);

        CHECK(!g.m.bus.intPending(), "no character yet, so nothing is asking");

        // A character arrives on the line and the 6850 clocks it in on its own clock.
        g.tty->feed("A");
        g.sio->pump();
        for (int i = 0; i < 200; ++i) g.m.clock.advance(1000);

        CHECK(g.m.bus.intPending(), "the 6850 pulls VI7; the 88-VI prioritizes it and pulls pin 73");
        CHECK(g.vi->winner() == 7, "VI7 -- the lowest priority, and the only one asking");
        CHECK(g.intAck() == 0xFF, "-> RST 7 -> 0x0038. THAT is what 'vector 7' means.");

        // Reading the data register drops the 6850's IRQ, and the whole chain lets go.
        g.m.bus.ioRead(0x11);
        CHECK(!g.m.bus.intPending(), "the character was read: VI7 falls, and so does pin 73");
    }

    // ---- ...and the SAME thing on a level whose vector is not 0xFF. ----
    //
    // The check above is real but it cannot tell you the card DROVE anything: RST 7 is
    // 0xFF, and so is a bus nobody is driving. Put the 6850 on VI2 and the vector is
    // 0xD7, which only a card that claimed the cycle can produce.
    {
        Rig g;
        std::string err;
        setProperty(*g.vi, "rtc_interrupt", "none", err);
        CHECK(setUnitProperty(*g.sio, "a", "interrupt", "vi2", err), "console 6850 -> VI2");

        g.ctl(0xC0);
        g.m.bus.ioWrite(0x10, 0x03);
        g.m.bus.ioWrite(0x10, 0x95);  // master reset, then 8N2 + receive interrupt enable

        g.tty->feed("A");
        g.sio->pump();
        for (int i = 0; i < 200; ++i) g.m.clock.advance(1000);

        CHECK(g.vi->winner() == 2, "VI2 is the line being pulled");
        CHECK(g.intAck() == 0xD7, "-> RST 2 -> 0x0010. A floating bus could never say this.");
    }

    // ---- a DISABLED structure drives nothing, even with a line screaming. ----
    //
    // Bit 7 low is what POC leaves behind. The card must then keep its hands off the
    // IntAck cycle entirely -- not drive a "safe" vector, not drive zero. Off is off,
    // and the bus floats, which is the machine's own answer.
    {
        Rig g;
        std::string err;
        setProperty(*g.vi, "rtc_interrupt", "none", err);
        setUnitProperty(*g.sio, "a", "interrupt", "vi2", err);

        g.ctl(0x00);  // the VI structure DISABLED -- bit 7 low
        g.m.bus.ioWrite(0x10, 0x03);
        g.m.bus.ioWrite(0x10, 0x95);
        g.tty->feed("A");
        g.sio->pump();
        for (int i = 0; i < 200; ++i) g.m.clock.advance(1000);

        CHECK(g.m.bus.viLines() == 0x04, "the 6850 is still pulling VI2 -- it has no idea");
        CHECK(!g.m.bus.intPending(), "...but a disabled 88-VI does not pull pin 73");
        CHECK(g.intAck() == 0xFF, "...and does not claim IntAck: the bus floats, as it should");
    }
}
