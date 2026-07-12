#include "test.h"

#include "boards/mits-88cpu.h"
#include "boards/mits-88sio.h"
#include "boards/s100-memory.h"
#include "core/machine.h"
#include "host/stream.h"

#include <cstdio>
#include <memory>

using namespace altair;

namespace {

// A machine with an 88-SIO in it and a scripted terminal on its one port. Built by
// hand rather than from a .toml, so that a config bug cannot make these go red.
struct Rig {
    Machine         m;
    SioBoard*       sio = nullptr;
    MemoryBoard*    mem = nullptr;
    ScriptedStream* tty = nullptr;

    Rig() {
        std::string err;

        // PARANOID MODE, PERMANENTLY ON IN THIS SUITE. This card drives pin 73, and
        // it is the one that has to remember to SAY so -- after a register access,
        // after a character lands, when a deadline comes due, when a pad moves. Miss
        // one and the guest waits forever for an interrupt that already happened.
        // This re-derives the whole wire on every instruction and aborts the moment
        // the card and the backplane disagree about it.
        m.bus.setVerify(true);

        mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region r;
        r.kind = RegionKind::Ram;
        r.at   = 0;
        r.size = 0x10000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);

        sio = dynamic_cast<SioBoard*>(m.add("sio", "sio0", err));

        auto s = std::make_unique<ScriptedStream>();
        tty    = s.get();
        sio->attachStream(std::move(s));

        m.add("8080", "cpu0", err);
        m.power();
    }

    void run(int steps) {
        for (int i = 0; i < steps; ++i) {
            StepResult s = m.master()->step(m.bus);
            m.clock.advance(s.tStates);
        }
    }

    // Let one character finish arriving on the line. The receiver is paced by the
    // clock exactly as the transmitter is, so a second character cannot land until
    // the first one's character time has passed. At 9600 baud, 8N1, a character is
    // 10 bits = 2,083 T-states; 5000 is comfortably more.
    void lineTime() { m.clock.advance(5000); }

    void load(std::initializer_list<uint8_t> code, uint16_t at = 0) {
        uint16_t a = at;
        for (uint8_t b : code) m.bus.memWrite(a++, b);
        m.cpu()->setPc(at);
    }

    uint8_t status() { return m.bus.ioRead(0x00); }
    uint8_t data() { return m.bus.ioRead(0x01); }
};

// Read a property back the way SHOW does -- through the reflection layer, not by
// reaching into the board. If `rev` is not gettable, that is a bug in the board.
std::string prop(Board& b, const std::string& name) {
    for (Property& p : b.properties())
        if (p.name == name) return p.get().text(p.radix);
    return "(no such property)";
}

// The status bits, by the manual's names. Spelled out here because the whole point
// of this card is that they do NOT mean what the same bit means on a 2SIO.
constexpr uint8_t kOutputNotReady = 0x80;  // bit 7, INVERTED: CLEAR = ready to send
constexpr uint8_t kInputNotReady  = 0x01;  // bit 0, INVERTED: CLEAR = a byte is waiting
constexpr uint8_t kRev0DataAvail  = 0x20;  // bit 5, true sense, Rev 0 only
constexpr uint8_t kRev0TxEmpty    = 0x02;  // bit 1, true sense, Rev 0 only

} // namespace

void test_88sio() {
    SECTION("88-SIO -- the card");
    {
        Rig g;
        CHECK(g.sio->units().size() == 1, "one port, not two -- this is not a 2SIO");
        CHECK(g.sio->units()[0].name == "tty", "and it is called 'tty'");
        CHECK(g.sio->units()[0].kind == UnitKind::Serial, "a serial unit");

        // TWO ports and not one more. A card that decoded a third would be stealing
        // an address from whatever was next in the machine.
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0x00;
        CHECK(g.sio->decodes(c), "decodes the control channel (even)");
        c.addr = 0x01;
        CHECK(g.sio->decodes(c), "decodes the data channel (odd)");
        c.addr = 0x02;
        CHECK(!g.sio->decodes(c), "does NOT decode the port after them");

        // It is an I/O card. It answers no memory address at all.
        c.type = Cycle::MemRead;
        c.addr = 0x00;
        CHECK(!g.sio->decodes(c), "decodes no MEMORY -- it is not there");

        // The address decode ignores A0 and uses it to pick the channel, so an odd
        // base is not a card you could build. The manual: "any even numbered address
        // from 0 to 376 (octal)".
        std::string err;
        CHECK(!setProperty(*g.sio, "port", "11", err), "an ODD base is refused");
        CHECK(err.find("even") != std::string::npos, "and it says why, in words");
        CHECK(setProperty(*g.sio, "port", "10", err), "an even base is taken");
        c.type = Cycle::IoRead;
        c.addr = 0x10;
        CHECK(g.sio->decodes(c), "and the card MOVED -- the decode followed the pad");
        c.addr = 0x00;
        CHECK(!g.sio->decodes(c), "and no longer answers where it was");
    }

    SECTION("88-SIO -- status is INVERTED, and that is the whole trap");
    {
        // THE BUG THIS TEST EXISTS TO PREVENT. On the 2SIO, bit set = condition
        // true. Here, bit CLEAR = READY. A driver written for one card silently
        // misbehaves on the other, and the two cards can be in the same machine.
        Rig g;

        uint8_t s = g.status();
        CHECK((s & kOutputNotReady) == 0, "idle transmitter: bit 7 CLEAR means READY");
        CHECK((s & kInputNotReady) != 0, "nobody typed: bit 0 SET means NOT ready");

        g.tty->feed("A");
        g.sio->pump();
        s = g.status();
        CHECK((s & kInputNotReady) == 0, "a character arrived: bit 0 goes CLEAR");
        CHECK(g.data() == 'A', "and the data channel yields it");
        CHECK((g.status() & kInputNotReady) != 0, "reading the data clears DAV again");
    }

    SECTION("88-SIO -- Rev 1 is the default, and bits 5 and 1 are gone");
    {
        // Rev 1 IS the errata modification done at the factory (Patrick,
        // 2026-07-12): TBMT and DAV are rerouted to bits 7 and 0, and bits 5 and 1
        // are left undriven.
        Rig g;
        std::string err;
        CHECK(prop(*g.sio, "rev") == "1", "a card out of the box is a Rev 1");

        // THE WHOLE STATUS BYTE, PINNED. Bits 6, 5 and 1 are undriven on a Rev 1 and
        // read as 1 (Patrick, 2026-07-12; and an unconnected TTL input into the 8T97
        // floats high). Spelled out as a whole byte, not a mask, so the convention
        // cannot be quietly changed by accident somewhere else.
        //
        //   0x63 = 0110_0011 : bit7 clear (ready to send), bits 6/5/1 undriven high,
        //                      bit 0 set (nothing typed), no errors.
        CHECK(g.status() == 0x63, "an idle Rev 1 reads exactly 63");

        g.tty->feed("A");
        g.sio->pump();
        uint8_t s = g.status();
        CHECK((s & kInputNotReady) == 0, "Rev 1 reports the character at bit 0");

        // ...and bits 5 and 1 DO NOT MOVE, because on a Rev 1 they are not connected
        // to anything. This is exactly how a Rev 0 driver comes to grief on a Rev 1
        // board: it polls bit 5, sees it stuck high, and reads garbage forever.
        CHECK((s & kRev0DataAvail) != 0, "bit 5 is high -- but only because it is UNDRIVEN");
        CHECK(s == 0x62, "the character shows ONLY at bit 0: 62, not 63");
    }

    SECTION("88-SIO -- Rev 0 ALSO exposes DAV at bit 5 and TBMT at bit 1");
    {
        Rig g;
        std::string err;
        CHECK(setProperty(*g.sio, "rev", "0", err), "the revision is a config option");

        //   0x43 = 0100_0011 : bit7 clear (ready), bit 6 undriven high, bit 5 clear
        //                      (no data), bit 1 SET (TBMT), bit 0 set (nothing typed).
        // Note bit 5 now READS ZERO where the Rev 1 read it as one: it is DRIVEN here.
        uint8_t s = g.status();
        CHECK(s == 0x43, "an idle Rev 0 reads exactly 43 -- a DIFFERENT byte from a Rev 1");
        CHECK((s & kRev0TxEmpty) != 0, "Rev 0 bit 1: TBMT, TRUE sense -- set = empty");
        CHECK((s & kRev0DataAvail) == 0, "Rev 0 bit 5: DAV, TRUE sense -- clear = nothing");

        // ...and the inverted ready bits say the SAME two things at the same time.
        // That redundancy is the board, not a bug: bits 7 and 0 came from the
        // device's handshake lines, bits 5 and 1 from the UART.
        CHECK((s & kOutputNotReady) == 0, "and bit 7 agrees -- ready to send");

        g.tty->feed("Z");
        g.sio->pump();
        s = g.status();
        CHECK((s & kRev0DataAvail) != 0, "bit 5 SET: a word is in the buffer");
        CHECK((s & kInputNotReady) == 0, "and bit 0 CLEAR says the same thing, inverted");
        CHECK(g.data() == 'Z', "and it is the character we fed");
        CHECK((g.status() & kRev0DataAvail) == 0, "reading the data clears bit 5 too");
    }

    SECTION("88-SIO -- TBMT is a DEADLINE, not a flag");
    {
        // The transmitter is BUSY until the character has had time to leave the
        // shift register. A hardwired "always ready" is the easy lie, and it changes
        // what a BIOS that TIMES the line decides to do.
        Rig g;
        CHECK((g.status() & kOutputNotReady) == 0, "idle: ready");

        g.m.bus.ioWrite(0x01, 'X');
        CHECK((g.status() & kOutputNotReady) != 0,
              "the instant a character goes out, bit 7 SETS -- NOT ready");
        CHECK(g.tty->out() == "X", "and the character really is on the line");

        g.lineTime();
        CHECK((g.status() & kOutputNotReady) == 0,
              "and one character time later it is ready again, with nobody touching it");
    }

    SECTION("88-SIO -- the control channel holds TWO BITS, and they are the enables");
    {
        // There is no 6850-style control register here. Word format and baud are
        // soldered pads. The ONLY thing an OUT to the control channel can do is set
        // the two interrupt-enable flip-flops: D0 = input, D1 = output.
        Rig g;
        std::string err;
        setProperty(*g.sio, "in_int", "int", err);
        setProperty(*g.sio, "out_int", "none", err);

        g.tty->feed("A");
        g.sio->pump();
        CHECK(!g.m.bus.intPending(),
              "a character is waiting, but the interrupt is NOT ENABLED -- pin 73 is idle");

        g.m.bus.ioWrite(0x00, 0x01);  // D0: enable the INPUT interrupt
        CHECK(g.m.bus.intPending(), "enable it and the card pulls pin 73 at once");

        g.m.bus.ioWrite(0x00, 0x00);  // disable both
        CHECK(!g.m.bus.intPending(), "disable it and the card lets go");

        // D1 is the OUTPUT interrupt, and it is a different wire. Enabling it while
        // only the INPUT pad is strapped to pin 73 must do nothing.
        g.m.bus.ioWrite(0x00, 0x02);
        CHECK(!g.m.bus.intPending(),
              "D1 enables the OUTPUT interrupt -- whose pad goes nowhere");
    }

    SECTION("88-SIO -- IN and OUT are TWO straps, at independent priorities");
    {
        // The assembly manual: "You may connect the 'OUT' (output device) pad to
        // some priority level, and the 'IN' (input device) pad to some priority
        // level." So they are two wires, and a card can have its receiver on a
        // vectored line and its transmitter on pin 73 -- which nothing but two
        // independent straps can express.
        Rig g;
        std::string err;
        setProperty(*g.sio, "in_int", "vi3", err);   // receiver -> a VI line
        setProperty(*g.sio, "out_int", "int", err);  // transmitter -> pin 73

        g.m.bus.ioWrite(0x00, 0x03);  // enable BOTH interrupts

        // The transmitter is idle, so it is asking, and its pad IS on pin 73.
        CHECK(g.m.bus.intPending(), "the OUT strap pulls pin 73");

        // Now silence the transmitter and let ONLY the receiver ask.
        setProperty(*g.sio, "out_int", "none", err);
        g.tty->feed("A");
        g.sio->pump();
        CHECK(g.sio->dataAvailable(), "the receiver has a character and is asking");
        CHECK(!g.m.bus.intPending(),
              "but its pad goes to VI3, and there is no 88-VI card -- so pin 73 stays idle");
    }

    SECTION("88-SIO -- NOBODY IS ASKING: the card acts on its own deadline");
    {
        // The case the poll-every-instruction model could not do (DESIGN.md 4.4.1).
        // A driver enables the output interrupt, sends a character, and HALTs. There
        // is no bus cycle, nothing reads a register, and the CPU is stopped. The ONLY
        // thing that can wake this machine is a deadline the CARD set for itself when
        // the character went out.
        Rig g;
        std::string err;
        setProperty(*g.sio, "out_int", "int", err);

        g.load({
            0x31, 0x00, 0x02,  // LXI SP,0200
            0x3E, 'C',         // MVI A,'C'
            0xD3, 0x01,        // OUT 01     -- send it. TBMT falls; the card sets an alarm
            0x3E, 0x02,        // MVI A,02
            0xD3, 0x00,        // OUT 00     -- D1: enable the OUTPUT interrupt
            0xFB,              // EI
            0x76,              // HLT        -- and now nothing is asking anybody anything
        }, 0x0100);

        g.m.debug.add(BreakKind::Pc, 0x0038, 0x0038);  // RST 7 lands here

        RunResult r = g.m.debug.run(2000);
        CHECK(r.why == StopReason::Breakpoint,
              "the interrupt ARRIVED -- the halted machine was woken by the card");
        CHECK(g.m.cpu()->pc() == 0x0038, "at RST 7, which is what an empty backplane reads as");
        CHECK(g.tty->out() == "C", "and the character had really gone out");
    }

    SECTION("88-SIO -- an INTERRUPT-DRIVEN echo, end to end, with no VI board");
    {
        // THE ACCEPTANCE TEST. Nothing here is arranged: the UART asks because a
        // character arrived, software had enabled the input interrupt, the IN pad
        // takes the request to pin 73, the 8080 acknowledges at an instruction
        // boundary, NOBODY claims the IntAck cycle because there is no 88-VI card in
        // this machine, the bus floats to FF, and FF is `RST 7`.
        //
        // The vector is not chosen by anything. It is what an empty backplane reads
        // as. There is no vector logic in the bus, the board, or the CPU -- and this
        // test would fail if anybody added some.
        Rig g;
        std::string err;
        setProperty(*g.sio, "in_int", "int", err);

        // RST 7 lands at 0038. Read the character and send it straight back.
        g.m.bus.memWrite(0x0038, 0xDB);  // IN 01   -- read it (clears DAV, drops the request)
        g.m.bus.memWrite(0x0039, 0x01);
        g.m.bus.memWrite(0x003A, 0xD3);  // OUT 01  -- echo it
        g.m.bus.memWrite(0x003B, 0x01);
        g.m.bus.memWrite(0x003C, 0xFB);  // EI
        g.m.bus.memWrite(0x003D, 0xC9);  // RET

        // The mainline does NOTHING but wait to be interrupted.
        g.load({
            0x31, 0x00, 0x02,  // LXI SP,0200  -- RST 7 pushes; it needs somewhere
            0x3E, 0x01,        // MVI A,01     -- D0: the INPUT interrupt
            0xD3, 0x00,        // OUT 00
            0xFB,              // EI
            0xC3, 0x08, 0x01,  // JMP 0108     -- spin here forever
        }, 0x0100);

        g.tty->feed("Hi!");
        g.run(20000);

        CHECK(g.tty->out() == "Hi!", "every character came back, driven by RST 7");
        CHECK(g.m.cpu()->pc() >= 0x0108 && g.m.cpu()->pc() <= 0x010A,
              "and the mainline is still spinning where we left it");
    }

    SECTION("88-SIO -- SIOECHO.ASM: a PERIOD program, not one we wrote");
    {
        // ACCEPTANCE TEST. deramp.com/downloads/altair/software/utilities/other/,
        // written by someone who owns the hardware. We did not design it to pass and
        // we may not adjust it if it fails -- which is the entire point (DESIGN.md
        // 0.1). It independently corroborates three separate claims this board makes:
        // the ports are 00/01, bit 0 is the receive flag, and BIT 0 IS INVERTED.
        //
        //   ; SIO (not 2SIO) echo test
        //           org     0
        //   loop    in      00h        ;wait for character
        //           rrc
        //           jc      loop       ;nothing yet (negative logic)
        //           in      01h        ;read the character
        //           out     01h        ;echo it
        //           jmp     loop
        //
        // "negative logic" is the author's own comment, and it is the whole board.
        // RRC rotates bit 0 into carry; JC loops WHILE IT IS SET. On a card whose
        // ready bits were true-sense this program would spin forever with a character
        // waiting -- so if we had copied the 2SIO's polarity, this is the test that
        // would have caught it.
        Rig g;
        g.load({
            0xDB, 0x00,        // loop: IN  00
            0x0F,              //       RRC
            0xDA, 0x00, 0x00,  //       JC  loop
            0xDB, 0x01,        //       IN  01
            0xD3, 0x01,        //       OUT 01
            0xC3, 0x00, 0x00,  //       JMP loop
        }, 0x0000);

        g.tty->feed("Hello");
        g.run(20000);
        CHECK(g.tty->out() == "Hello", "the period echo test echoes, polling the inverted flag");
    }

    SECTION("88-SIO -- SIOINT.ASM: the period INTERRUPT test, verbatim");
    {
        // ACCEPTANCE TEST, same source. This one corroborates the interrupt design end
        // to end: D0 of the control channel is the RECEIVE interrupt enable ("receive
        // ints on"), the request reaches pin 73, and the vector is RST 7 -- which the
        // author simply ASSUMES, putting his handler at 038h without a word of
        // explanation, because on a real Altair with no 88-VI card that is where the
        // interrupt goes. Nothing in our bus chooses it either.
        //
        //           org     0
        //           lxi     sp,0100h   ;init stack pointer
        //           mvi     a,01h      ;receive ints on
        //           out     00h
        //           ei                 ;enable 8080 interrupts
        //   loop    nop / nop / nop
        //           jmp     loop
        //
        //           org     038h       ;RST7 entry address
        //           push    psw        ;save A and status flags
        //           in      01h        ;read the character
        //           out     01h        ;echo it
        //           pop     psw        ;restore A and status flags
        //           ei
        //           ret
        //
        // The accumulator save/restore is not decoration: the author's comment says he
        // is verifying "that the accumulator does not get changed by the interrupt
        // routine". So this exercises the CPU's interrupt path too, not just the card's.
        Rig g;
        std::string err;
        setProperty(*g.sio, "in_int", "int", err);  // the IN pad, soldered to pin 73

        g.load({
            0x31, 0x00, 0x01,  // LXI SP,0100
            0x3E, 0x01,        // MVI A,01     -- receive ints on
            0xD3, 0x00,        // OUT 00
            0xFB,              // EI
            0x00, 0x00, 0x00,  // loop: NOP NOP NOP
            0xC3, 0x08, 0x00,  //       JMP loop
        }, 0x0000);

        uint16_t at = 0x0038;
        for (uint8_t b : {0xF5, 0xDB, 0x01, 0xD3, 0x01, 0xF1, 0xFB, 0xC9})
            g.m.bus.memWrite(at++, b);  // PUSH PSW / IN 01 / OUT 01 / POP PSW / EI / RET

        g.tty->feed("Hi!");
        g.run(20000);

        CHECK(g.tty->out() == "Hi!", "the period interrupt test echoes, driven by RST 7");
        CHECK(g.m.cpu()->pc() >= 0x0008 && g.m.cpu()->pc() <= 0x000D,
              "and it is back in its NOP loop, exactly where the interrupt found it");
    }

    SECTION("88-SIO -- a reset does not unplug the terminal");
    {
        Rig g;
        std::string err;
        setProperty(*g.sio, "in_int", "int", err);
        g.m.bus.ioWrite(0x00, 0x01);  // enable the input interrupt

        g.sio->reset(Reset::Bus);

        // The cable is still in the socket: a character fed AFTER the reset still
        // arrives. (Comparing endpoint() to units()[0].state would have been vacuous
        // -- both call stream_->describe() -- so this feeds the line instead.)
        g.tty->feed("A");
        g.sio->pump();
        CHECK(g.sio->dataAvailable(),
              "the line survived the reset -- a warm reset does not pull the cable out");
        CHECK(g.data() == 'A', "and it is really the character we fed");

        // ...but the interrupt-enable flip-flops were cleared, so the card is silent.
        CHECK(!g.m.bus.intPending(),
              "the interrupt ENABLES were cleared, so nothing is pulling pin 73");
    }
}
