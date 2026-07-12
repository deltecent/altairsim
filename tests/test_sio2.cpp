#include "test.h"

#include "boards/cpu8080.h"
#include "boards/memory.h"
#include "boards/sio2.h"
#include "core/machine.h"
#include "core/roms.h"
#include "host/endpoint.h"
#include "cli/monitor.h"
#include "host/stream.h"

#include <cstdio>
#include <sstream>

using namespace altair;

namespace {

// A machine with a 2SIO in it, and a scripted terminal on channel A. Built by
// hand rather than from a .toml, so that a config bug cannot make these go red.
struct Rig {
    Machine          m;
    Sio2Board*       sio = nullptr;
    MemoryBoard*     mem = nullptr;
    ScriptedStream*  tty = nullptr;

    Rig(uint8_t port = 0x10) {
        std::string err;

        mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region r;
        r.kind = RegionKind::Ram;
        r.at   = 0;
        r.size = 0x10000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", false, err);

        sio = dynamic_cast<Sio2Board*>(m.add("2sio", "sio0", err));

        // HEX. A port is ON THE WIRE (DESIGN.md 10.0.1), so `port`'s radix is 16 --
        // and the first draft of this test wrote std::to_string(0x10), which is
        // "16", which the property layer correctly read as 0x16 and put the card
        // four ports up the address space. Every status read then floated to FF,
        // which has every bit set, so half the assertions passed for the worst
        // possible reason. The rule is not decoration.
        char hex[8];
        std::snprintf(hex, sizeof hex, "%02X", port);
        CHECK(setProperty(*sio, "port", hex, false, err), "the base port is a hex jumper");

        auto s = std::make_unique<ScriptedStream>();
        tty    = s.get();
        sio->channel("a")->connect(std::move(s));

        m.add("8080", "cpu0", err);
        m.power();
    }

    void run(int steps) {
        for (int i = 0; i < steps; ++i) {
            StepResult s = m.master()->step(m.bus);
            m.clock.advance(s.tStates);
        }
    }

    // Let one character finish arriving on the line.
    //
    // A test that feeds two characters and expects both to be there AT ONCE is
    // asking a 9600-baud line to deliver them in zero time. The receiver is paced
    // by the clock exactly as the transmitter is -- that is the same mechanism
    // TDRE is built on -- so a second character cannot land until the first one's
    // character time has passed. 5000 T-states is comfortably more than any
    // character time at 9600 baud (the longest is 11 bits = 2,291).
    void lineTime() { m.clock.advance(5000); }

    void load(std::initializer_list<uint8_t> code, uint16_t at = 0) {
        uint16_t a = at;
        for (uint8_t b : code) m.bus.memWrite(a++, b);
        m.cpu()->setPc(at);
    }
};

} // namespace

void test_sio2() {
    SECTION("88-2SIO -- the card");
    {
        Rig g;
        CHECK(g.sio->units().size() == 2, "two channels");
        CHECK(g.sio->units()[0].name == "a", "named 'a', not 0");
        CHECK(g.sio->units()[0].kind == UnitKind::Serial, "a serial unit");

        // Four ports and not one more. A card that decoded a fifth would be
        // stealing an address from whatever was next in the machine.
        BusCycle c;
        c.type = Cycle::IoRead;
        for (uint8_t p = 0x10; p <= 0x13; ++p) {
            c.addr = p;
            CHECK(g.sio->decodes(c), "decodes its own four ports");
        }
        c.addr = 0x14;
        CHECK(!g.sio->decodes(c), "does NOT decode the port after them");
        c.addr = 0x0F;
        CHECK(!g.sio->decodes(c), "does NOT decode the port before them");

        // It is an I/O card. It answers no memory address at all.
        c.type = Cycle::MemRead;
        c.addr = 0x10;
        CHECK(!g.sio->decodes(c), "decodes no MEMORY -- it is not there");
    }

    SECTION("88-2SIO -- status, true sense");
    {
        Rig g;
        // Nothing typed yet: RDRF clear, TDRE set. An idle UART is READY TO SEND
        // and has NOTHING TO SAY, and both of those are the true-sense bits.
        uint8_t s = g.m.bus.ioRead(0x10);
        CHECK((s & 0x01) == 0, "RDRF clear -- nobody has typed");
        CHECK((s & 0x02) != 0, "TDRE set -- the transmitter is idle");
        CHECK((s & 0x04) == 0, "DCD clear -- carrier present (the pin is /DCD)");
        CHECK((s & 0x08) == 0, "CTS clear -- clear to send (the pin is /CTS)");

        g.tty->feed("A");
        s = g.m.bus.ioRead(0x10);
        CHECK((s & 0x01) != 0, "RDRF set once a character arrives");
        CHECK(g.m.bus.ioRead(0x11) == 'A', "and the data port yields it");
        CHECK((g.m.bus.ioRead(0x10) & 0x01) == 0, "reading the data clears RDRF");
    }

    SECTION("88-2SIO -- TDRE is a DEADLINE, not a flag");
    {
        // THE BUG THIS TEST EXISTS TO PREVENT. The Python prototype hardwired
        // TDRE=1. The Mike Douglas CP/M BIOS INFERS THE LINE SPEED by timing how
        // long TDRE stays clear -- so hardwiring it does not merely approximate
        // the hardware, it silently changes what the guest decides to do.
        Rig g;
        g.m.bus.ioWrite(0x10, 0x11);  // 8N2 -- 11 bits on the wire per character

        CHECK((g.m.bus.ioRead(0x10) & 0x02) != 0, "TDRE set before we send");

        g.m.bus.ioWrite(0x11, 'X');
        CHECK((g.m.bus.ioRead(0x10) & 0x02) == 0, "TDRE CLEAR the instant we send");

        // 9600 baud, 11 bits, 2 MHz -> 2,291 T-states. Just short of it: still busy.
        uint64_t charT = (uint64_t)(2000000 * 11 / 9600);
        g.m.clock.advance(charT - 2);
        CHECK((g.m.bus.ioRead(0x10) & 0x02) == 0, "still clear one T-state early");

        g.m.clock.advance(2);
        CHECK((g.m.bus.ioRead(0x10) & 0x02) != 0, "set once the character has left");

        CHECK(g.tty->out() == "X", "and the byte really went out the line");
    }

    SECTION("88-2SIO -- the character time follows the WORD FORMAT");
    {
        // 7 bits + parity + 1 stop = 10 on the wire; 8 bits + 2 stop = 11. A guest
        // that configures a Teletype gets a Teletype's timing, and nothing here was
        // told what a Teletype is -- it falls out of the control register.
        Rig g;
        g.m.bus.ioWrite(0x10, 0x08);  // word select 010: 7 data, even parity, 1 stop
        g.m.bus.ioWrite(0x11, 'X');
        uint64_t ten = (uint64_t)(2000000 * 10 / 9600);
        g.m.clock.advance(ten - 1);
        CHECK((g.m.bus.ioRead(0x10) & 0x02) == 0, "7E1: still busy at 10 bits - 1");
        g.m.clock.advance(1);
        CHECK((g.m.bus.ioRead(0x10) & 0x02) != 0, "7E1: done at 10 bit times, not 11");
    }

    SECTION("88-2SIO -- master reset is not decoration");
    {
        // The Python prototype ignored the control register entirely. ALTMON's very
        // first two instructions are `MVI A,3 / OUT 10h` -- if that write does
        // nothing, every machine that starts with a master reset starts wrong.
        Rig g;
        g.tty->feed("Z");
        (void)g.m.bus.ioRead(0x10);  // let it arrive -> RDRF set
        CHECK((g.m.bus.ioRead(0x10) & 0x01) != 0, "a character is waiting");

        g.m.bus.ioWrite(0x10, 0x03);  // divide field == 11 == MASTER RESET
        CHECK((g.m.bus.ioRead(0x10) & 0x01) == 0, "master reset clears RDRF");
        CHECK((g.m.bus.ioRead(0x10) & 0x02) != 0, "master reset leaves TDRE set");

        // ...and it does NOT unplug the terminal. A warm reset that dropped the
        // console would be a baffling thing to debug.
        CHECK(g.sio->channel("a")->endpoint() == "scripted", "the endpoint survives a reset");
    }

    SECTION("88-2SIO -- both channels, independent");
    {
        Rig g;
        auto s = std::make_unique<LoopbackStream>();
        g.sio->channel("b")->connect(std::move(s));

        g.m.bus.ioWrite(0x13, 'q');  // b's data port -- straight back round the loop
        CHECK((g.m.bus.ioRead(0x12) & 0x01) != 0, "b: loopback returns the byte");
        CHECK(g.m.bus.ioRead(0x13) == 'q', "b: and it is the byte we sent");

        // A's line never saw it. Two chips, not one chip with an index.
        CHECK(g.tty->out().empty(), "a: heard nothing -- the channels are independent");
    }

    SECTION("88-2SIO -- the base port is a jumper");
    {
        Rig g(0x14);
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0x14;
        CHECK(g.sio->decodes(c), "moved to 14");
        c.addr = 0x10;
        CHECK(!g.sio->decodes(c), "and is no longer at 10 -- two cards can coexist");
    }

    SECTION("88-2SIO -- interrupts, with NO VI board in the machine");
    {
        // THE ALTAIR'S ACTUAL INTERRUPT PATH (DESIGN.md 4.4). The 6850 raises IRQ,
        // the jumper takes it to pINT, nobody claims the IntAck cycle, the bus
        // floats FF, and the 8080 executes RST 7. No vector logic exists anywhere,
        // and none is needed.
        Rig g;
        Acia* a = g.sio->channel("a");

        CHECK(!g.m.bus.intPending(), "quiet to start with");

        a->jumper = IrqJumper::Int;
        g.m.bus.ioWrite(0x10, 0x91);  // RIE (bit 7) + 8N2, receive interrupt enabled
        CHECK(!g.m.bus.intPending(), "RIE on, but nothing has arrived");

        g.tty->feed("!");
        (void)g.m.bus.ioRead(0x10);  // the character lands
        CHECK(g.m.bus.intPending(), "a character arrived -> the card pulls pINT");
        CHECK((g.m.bus.ioRead(0x10) & 0x80) != 0, "and status bit 7 (IRQ) says so");

        // An interrupt is a LEVEL, not an event. It stays asserted until the guest
        // reads the character -- which is why a board cannot 'lose' one.
        CHECK(g.m.bus.intPending(), "still asserted -- it is a level on a wire");
        (void)g.m.bus.ioRead(0x11);
        CHECK(!g.m.bus.intPending(), "and it drops when the character is read");

        // The un-jumpered case: the CHIP still raises IRQ (it is a pin, and it does
        // not care what you soldered to it), but the WIRE goes nowhere.
        a->jumper = IrqJumper::None;
        g.tty->feed("?");
        g.lineTime();  // the next character has to physically arrive
        (void)g.m.bus.ioRead(0x10);
        CHECK((g.m.bus.ioRead(0x10) & 0x80) != 0, "chip still raises IRQ in its status");
        CHECK(!g.m.bus.intPending(), "but with no jumper, pINT stays high");
    }

    SECTION("88-2SIO -- an unconnected line is not an error");
    {
        // An unconnected 6850 sits there with TDRE set forever, and software that
        // writes to it works fine and talks to nobody. There is no null pointer in
        // the stream path and no branch anywhere for 'what if nothing is plugged in'.
        Rig g;
        CHECK(g.sio->channel("b")->endpoint() == "null", "b starts unconnected");
        CHECK((g.m.bus.ioRead(0x12) & 0x02) != 0, "and is READY TO SEND");
        g.m.bus.ioWrite(0x13, 'x');  // must not crash, must not block
        CHECK((g.m.bus.ioRead(0x12) & 0x01) == 0, "and never has anything to say");
    }

    SECTION("88-2SIO -- the transform chain is the LINE's, not the console's");
    {
        Rig g;
        std::string err;
        CHECK(setUnitProperty(*g.sio, "a", "upper", "true", false, err), "SET sio0:a UPPER=ON");

        g.tty->feed("abc");
        (void)g.m.bus.ioRead(0x10);
        CHECK(g.m.bus.ioRead(0x11) == 'A', "the guest sees uppercase");

        // ...and this is a ScriptedStream, not a console. That is the whole point:
        // the fold lives on the line, so it works on a socket too (DESIGN.md 7.2).
    }

    SECTION("EXACTLY ONE unit may hold the console");
    {
        // Two boards reading one keyboard would each get half the characters. This
        // is not hypothetical -- it is what happens the first time a machine has
        // two 2SIOs and you forget -- and it is invisible until you are debugging
        // why every other keystroke vanished.
        //
        // The first version of the arbitration was DEAD CODE and this test is why
        // it isn't now: it read `is(a[2], "console")`, and `is()` uppercases its
        // token before comparing, so the literal had to be uppercase or it could
        // never match. Both units held the console and nothing said a word.
        Machine m;
        std::string err;
        auto* s0 = dynamic_cast<Sio2Board*>(m.add("2sio", "sio0", err));
        auto* s1 = dynamic_cast<Sio2Board*>(m.add("2sio", "sio1", err));
        CHECK(setProperty(*s1, "port", "14", false, err), "the second card moves out of the way");

        Monitor mon(m);
        std::ostringstream out;
        mon.exec("CONNECT sio0:a console", out);
        CHECK(s0->channel("a")->endpoint() == "console", "sio0:a has the console");

        out.str("");
        mon.exec("CONNECT sio1:a console", out);
        CHECK(s1->channel("a")->endpoint() == "console", "sio1:a takes it");
        CHECK(s0->channel("a")->endpoint() != "console", "and sio0:a NO LONGER has it");
        CHECK(out.str().find("taken from sio0:a") != std::string::npos,
              "and it says who it took it from, out loud");
    }

    SECTION("88-2SIO -- an INTERRUPT-DRIVEN echo, end to end, with no VI board");
    {
        // ACCEPTANCE TEST 4 (docs/boards/88-2sio.md), and the one the whole
        // interrupt design stands or falls on. Nothing here is arranged: the 6850
        // raises IRQ because a character arrived, the jumper takes that to pINT,
        // the 8080 acknowledges at an instruction boundary, NOBODY claims the
        // IntAck cycle because there is no 88-VI card in this machine, the bus
        // floats to FF, and FF is `RST 7`.
        //
        // So the vector is not chosen by anything. It is what an empty backplane
        // reads as, and RST 7 is where the Altair's interrupt went. There is no
        // vector logic in the bus, the board, or the CPU -- and this test would
        // fail if anybody added some.
        Rig g;
        Acia* a = g.sio->channel("a");
        a->jumper = IrqJumper::Int;

        // RST 7 lands at 0038. Read the character and send it straight back.
        g.m.bus.memWrite(0x0038, 0xDB);  // IN 11    -- read the char (clears RDRF, drops IRQ)
        g.m.bus.memWrite(0x0039, 0x11);
        g.m.bus.memWrite(0x003A, 0xD3);  // OUT 11   -- echo it
        g.m.bus.memWrite(0x003B, 0x11);
        g.m.bus.memWrite(0x003C, 0xFB);  // EI
        g.m.bus.memWrite(0x003D, 0xC9);  // RET

        // The mainline does NOTHING but wait to be interrupted.
        g.load({
            0x31, 0x00, 0x02,  // LXI SP,0200   -- RST 7 pushes; it needs somewhere
            0x3E, 0x91,        // MVI A,91      -- RIE + 8 data, no parity, 2 stop
            0xD3, 0x10,        // OUT 10        -- arm the receive interrupt
            0xFB,              // EI
            0xC3, 0x08, 0x01,  // JMP 0108      -- spin here forever
        }, 0x0100);

        g.tty->feed("Hi!");
        g.run(20000);

        CHECK(g.tty->out() == "Hi!", "every character came back, driven by RST 7");
        CHECK(g.m.cpu()->pc() >= 0x0108 && g.m.cpu()->pc() <= 0x010A,
              "and the mainline is still spinning where we left it");
    }

    SECTION("ALTMON -- the ROM runs, and talks");
    {
        // THE ACCEPTANCE TEST. A real 1K monitor PROM, written for real hardware in
        // 2016 by someone who owns one, driving a real 6850 through our bus. It is
        // not a test we wrote; it is a program that either works or does not.
        Machine m;
        std::string err;

        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region ram;
        ram.kind = RegionKind::Ram;
        ram.at   = 0;
        ram.size = 0xC000;  // 48K -- ALTMON puts its stack at C000 and pushes DOWN
        mem->addRegion(ram, err);
        setProperty(*mem, "fill", "zero", false, err);

        Region rom;
        rom.kind  = RegionKind::Rom;
        rom.at    = 0xF800;
        rom.mount = "builtin:altmon";
        CHECK(mem->addRegion(rom, err), "the ALTMON ROM is embedded and mounts");

        auto* sio = dynamic_cast<Sio2Board*>(m.add("2sio", "sio0", err));
        auto  s   = std::make_unique<ScriptedStream>();
        auto* tty = s.get();
        sio->channel("a")->connect(std::move(s));

        m.add("8080", "cpu0", err);
        m.power();
        m.cpu()->setPc(0xF800);

        // Its own command syntax, read out of its own listing: `ahex` reads exactly
        // four hex digits and ABORTS on any byte below '0' -- so a space is not a
        // separator, it is a cancel. The spaces in ALTMON's printed command summary
        // are typography, not grammar.
        tty->feed("DF800F80F");

        for (int i = 0; i < 400000; ++i) {
            StepResult r = m.master()->step(m.bus);
            m.clock.advance(r.tStates);
        }

        const std::string& out = tty->out();
        CHECK(out.find("ALTMON 1.3") != std::string::npos, "it printed its banner");
        CHECK(out.find("DUMP") != std::string::npos, "it echoed the command name");

        // The bytes it dumped are its OWN first sixteen -- which are the 2SIO
        // initialization: MVI A,3 / OUT 10 / OUT 12 / MVI A,11 / OUT 10 / OUT 12.
        // So this line is the ROM reading itself back to us THROUGH the card it is
        // printing the description of.
        CHECK(out.find("3E 03 D3 10 D3 12 3E 11") != std::string::npos,
              "and dumped its own 2SIO init code back to us, correctly");
    }
}
