#include "test.h"

#include "boards/mits-88c700.h"
#include "boards/mits-88cpu.h"
#include "boards/s100-memory.h"
#include "core/debug.h"
#include "core/machine.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <string>

using namespace altair;

namespace {

// A machine with an 88-C700 in it and a scripted "printer" on its one line. The
// stream is bound through the REAL connect path (resolveEndpoint("scripted")), so
// the test exercises the same wiring an operator's CONNECT does -- then reads back
// what was "printed" from ScriptedStream::out().
struct Rig {
    Machine         m;
    C700Board*      lpt = nullptr;
    ScriptedStream* prn = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);

        m.add("memory", "mem0", err);
        lpt = dynamic_cast<C700Board*>(m.add("c700", "lpt0", err));
        lpt->connect("prn", "scripted", err);
        prn = dynamic_cast<ScriptedStream*>(lpt->unitStream("prn"));

        m.add("8080", "cpu0", err);
        m.power();
    }

    uint8_t status() { return m.bus.ioRead(0x02); }         // IN  02 -- the status word
    uint8_t readData() { return m.bus.ioRead(0x03); }       // IN  03 -- write-only channel
    void    send(uint8_t b) { m.bus.ioWrite(0x03, b); }     // OUT 03 -- a character
    void    control(uint8_t b) { m.bus.ioWrite(0x02, b); }  // OUT 02 -- PRIME / int-enable
};

std::string prop(Board& b, const std::string& name) {
    for (Property& p : b.properties())
        if (p.name == name) return p.get().text(p.radix);
    return "(no such property)";
}

// The status bits, by the manual's names (reference Table 1). ACTIVE HIGH -- the
// opposite of the 88-SIO, which is the whole reason this card is spelled out.
constexpr uint8_t kAcknowledge = 0x01;  // bit 0: SET = will accept a byte
constexpr uint8_t kBusy        = 0x02;  // bit 1: SET = busy
constexpr uint8_t kIntEnable   = 0x40;  // bit 6: SET = interrupts enabled
constexpr uint8_t kIntRequest  = 0x80;  // bit 7: SET = an interrupt is being requested

// Advance emulated time past the ACKNOWLEDGE handshake (~40 T-states at 2 MHz). Well
// clear of it, but nowhere near enough to matter -- the deadline is a one-shot.
void settle(Rig& g) { g.m.clock.advance(1000); }

} // namespace

void test_c700() {
    SECTION("88-C700 -- the card");
    {
        Rig g;
        CHECK(g.lpt->units().size() == 1, "one line -- a printer has one connector");
        CHECK(g.lpt->units()[0].name == "prn", "and it is called 'prn'");
        CHECK(g.lpt->units()[0].kind == UnitKind::Serial, "a serial unit");

        // Two ports and not one more: Control/Status at 02, Data at 03.
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0x02;
        CHECK(g.lpt->decodes(c), "decodes the control/status channel (even)");
        c.addr = 0x03;
        CHECK(!g.lpt->decodes(c),
              "does NOT decode an IN at the data channel -- a printer sends nothing back, "
              "so nothing drives the bus and it floats (issue #26)");
        c.type = Cycle::IoWrite;
        CHECK(g.lpt->decodes(c), "but it certainly decodes the OUT -- that is the whole card");
        c.type = Cycle::IoRead;
        c.addr = 0x04;
        CHECK(!g.lpt->decodes(c), "does NOT decode the port after them");
        c.type = Cycle::MemRead;
        c.addr = 0x02;
        CHECK(!g.lpt->decodes(c), "decodes no MEMORY -- it is an I/O card");

        // A0 picks the channel, so an odd base is not a card you could build. The
        // manual: "The Control/Status address is always even."
        std::string err;
        CHECK(!setProperty(*g.lpt, "port", "05", err), "an ODD base is refused");
        CHECK(err.find("even") != std::string::npos, "and it says why, in words");
        CHECK(setProperty(*g.lpt, "port", "10", err), "an even base is taken");
        c.type = Cycle::IoRead;
        c.addr = 0x10;
        CHECK(g.lpt->decodes(c), "and the card MOVED -- the decode followed the base");
        c.addr = 0x02;
        CHECK(!g.lpt->decodes(c), "and no longer answers where it was");
    }

    SECTION("88-C700 -- status is ACTIVE HIGH, and a byte-sink is always ready");
    {
        Rig g;
        uint8_t s = g.status();
        CHECK((s & kAcknowledge) != 0, "idle: bit 0 SET means WILL accept a byte");
        CHECK((s & kBusy) == 0, "and bit 1 CLEAR means not busy");

        // The DATA channel is write-only: reading it drives nothing, so the bus floats.
        // The VALUE was always FF; what issue #26 changed is WHO SAYS SO. The card used
        // to claim the cycle and return an FF of its own, which is the one thing a board
        // may not do (test_boundary.cpp) -- so check the provenance, not just the byte.
        CHECK(g.readData() == 0xFF, "reading the write-only data channel yields FF");
        BusCycle rd;
        rd.type = Cycle::IoRead;
        rd.addr = 0x03;
        CHECK(!g.lpt->decodes(rd),
              "...and that FF is the BUS's: the card does not claim the read at all");
    }

    SECTION("88-C700 -- a character written to the data port lands on the line");
    {
        Rig g;
        g.send('H');
        g.send('i');
        CHECK(g.prn->out() == "Hi", "the bytes reached the printer, in order");
    }

    SECTION("88-C700 -- the line is RAW: control codes pass through untouched");
    {
        // A printer's CR/LF/SO/DEL are DATA to this card -- it is a controller, not a
        // print mechanism, and it does not turn a CR into a page advance. Nothing on
        // the line rewrites a byte (DESIGN.md 7.2).
        Rig g;
        const char* line = "AB\r\n\x0E\x7F";  // A B CR LF SO DEL
        for (const char* p = line; *p; ++p) g.send((uint8_t)*p);
        CHECK(g.prn->out() == std::string(line), "every byte, control codes and all, verbatim");
    }

    SECTION("88-C700 -- the control channel: PRIME (D0) and interrupt-enable (D1)");
    {
        Rig g;
        CHECK((g.status() & kIntEnable) == 0, "out of reset: interrupts disabled");

        g.control(0x03);  // D1 = 1 (enable), D0 = 1 (no prime)
        CHECK((g.status() & kIntEnable) != 0, "D1 arms the interrupt structure -- bit 6 SET");

        g.control(0x00);  // D1 = 0 (disable), D0 = 0 (prime)
        CHECK((g.status() & kIntEnable) == 0, "D1 clear disarms it again");

        // PRIME (D0 low) resets the printer -- for a byte-sink there is nothing to
        // reset, so it is a flush, and the guest can issue it harmlessly at any time.
        g.send('X');
        g.control(0x00);  // prime
        CHECK(g.prn->out() == "X", "PRIME does not eat characters already sent");

        // Enabling interrupts with nothing outstanding raises NO request: no character
        // has been acknowledged, so the flip-flop is clear and pin 73 is up.
        g.control(0x03);
        CHECK((g.status() & kIntRequest) == 0, "arming alone requests nothing");
        CHECK(!g.m.bus.intPending(), "and no interrupt wire is pulled");
    }

    SECTION("88-C700 -- the interrupt: ACKNOWLEDGE requests, a data write dismisses it");
    {
        Rig g;
        // Default strap is pin 73 (`int`), per-character mode. Arm the structure.
        g.control(0x03);  // D1 = 1 enable, D0 = 1 no prime

        // A data byte goes out. The request drops immediately (the write dismisses it)
        // and the ACKNOWLEDGE that raises it lands only after the handshake elapses.
        g.send('A');
        CHECK((g.status() & kIntRequest) == 0, "right after the OUT: no request yet");
        CHECK(!g.m.bus.intPending(), "...and the wire is still up (the ISR hasn't been re-entered)");

        settle(g);
        CHECK((g.status() & kIntRequest) != 0, "the ACKNOWLEDGE landed: bit 7 SET");
        CHECK(g.m.bus.intPending(), "and the printer pulls pin 73");

        // The ISR outputs the next byte; that dismisses the request and re-arms it.
        g.send('B');
        CHECK((g.status() & kIntRequest) == 0, "writing the next byte clears the request");
        CHECK(!g.m.bus.intPending(), "and drops the wire");
        settle(g);
        CHECK(g.m.bus.intPending(), "the next ACKNOWLEDGE raises it again");

        CHECK(g.prn->out() == "AB", "and the bytes reached the printer");
    }

    SECTION("88-C700 -- disabling the structure, PRIME, and RESET all drop a request");
    {
        Rig g;
        g.control(0x03);
        g.send('A');
        settle(g);
        CHECK(g.m.bus.intPending(), "a request is pending");
        g.control(0x01);  // D1 = 0 disable, D0 = 1 no prime
        CHECK(!g.m.bus.intPending(), "disabling the interrupt structure drops it");

        // ...and PRIME clears it too (resetting the printer).
        g.control(0x03);
        g.send('A');
        settle(g);
        CHECK(g.m.bus.intPending(), "pending again");
        g.control(0x02);  // D1 = 1 (still enabled), D0 = 0 PRIME
        CHECK(!g.m.bus.intPending(), "PRIME clears the pending interrupt");

        // ...and a machine RESET clears request and enable both.
        g.send('A');
        settle(g);
        CHECK(g.m.bus.intPending(), "pending once more");
        g.m.reset(Reset::Bus);
        CHECK(!g.m.bus.intPending(), "RESET drops the wire");
        CHECK((g.status() & kIntEnable) == 0, "and disarms the structure");
    }

    SECTION("88-C700 -- SW2 #4: per-CR/LF mode only fires on CR or LF");
    {
        Rig g;
        std::string err;
        CHECK(setProperty(*g.lpt, "interrupt_after", "crlf", err), "strap the granularity to CR/LF");
        g.control(0x03);

        g.send('A');  // an ordinary character
        settle(g);
        CHECK(!g.m.bus.intPending(), "a printable byte is acknowledged silently in CR/LF mode");

        g.send('\r');  // a carriage return
        settle(g);
        CHECK(g.m.bus.intPending(), "but a CR raises the interrupt");

        g.send('\n');  // dismiss + a line feed
        CHECK(!g.m.bus.intPending(), "the write dismisses it");
        settle(g);
        CHECK(g.m.bus.intPending(), "and LF raises it again");
    }

    SECTION("88-C700 -- the interrupt rides the strap: none, pin 73, or a VI line");
    {
        Rig g;
        std::string err;

        // `none` -- an unsoldered strap: the request flip-flop still sets (bit 7), but
        // it reaches no wire.
        CHECK(setProperty(*g.lpt, "interrupt", "none", err), "unsolder the strap");
        g.control(0x03);
        g.send('A');
        settle(g);
        CHECK((g.status() & kIntRequest) != 0, "the request flip-flop still sets");
        CHECK(!g.m.bus.intPending(), "but nothing is pulled -- the strap goes nowhere");

        // Move it to a VI line. With no 88-VI in this machine the VI wire goes nowhere,
        // so pin 73 stays up -- but the card is now pulling VI3, not pin 73.
        CHECK(setProperty(*g.lpt, "interrupt", "vi3", err), "solder it to VI3");
        CHECK((g.m.bus.viLines() & (1u << 3)) != 0, "the request now rides VI3");
        CHECK(!g.m.bus.intPending(), "and no longer pin 73 (no 88-VI to forward it)");
    }

    SECTION("88-C700 -- disconnect leaves a dead line, not a dangling pointer");
    {
        Rig g;
        std::string err;
        CHECK(g.lpt->disconnect("prn", err), "the printer is unplugged");
        CHECK(g.lpt->units()[0].state == "null", "and the unit reports a dead line");
        // Writes to a dead line vanish; the card still answers status (ready, because
        // a NullStream always takes a byte).
        g.send('Z');  // into the void
        CHECK((g.status() & kAcknowledge) != 0, "an unconnected printer still reads ready");

        CHECK(!g.lpt->connect("lp", "null", err), "and there is no unit but 'prn'");
        CHECK(err.find("prn") != std::string::npos, "the error names the real one");
    }

    SECTION("88-C700 -- connect round-trips the endpoint spec through the property");
    {
        Rig g;
        std::string err;
        CHECK(g.lpt->connect("prn", "null", err), "connect to null");
        CHECK(prop(*g.lpt, "connect") == "null", "and the property reads it back for CONFIG SAVE");
    }

    SECTION("88-C700 -- INTERRUPT-DRIVEN printing, end to end, on a real 8080");
    {
        // THE ISSUE #26 SYMPTOM, GONE. A driver enables the printer interrupt, primes
        // the pump with one character, and HALTs. Nothing polls; the only thing that
        // can wake the machine is the ACKNOWLEDGE deadline the card set when the byte
        // went out. Each RST 7 sends the next character until the string is done. With
        // no 88-VI in the machine the IntAck floats to FF = RST 7, exactly as the 88-SIO
        // test relies on -- nothing here chooses the vector.
        Machine m;
        std::string err;
        m.bus.setVerify(true);

        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region r;
        r.kind = RegionKind::Ram;
        r.at   = 0;
        r.size = 0x10000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);

        auto* lpt = dynamic_cast<C700Board*>(m.add("c700", "lpt0", err));
        lpt->connect("prn", "scripted", err);
        auto* prn = dynamic_cast<ScriptedStream*>(lpt->unitStream("prn"));

        m.add("8080", "cpu0", err);
        m.power();

        // The message the printer receives, NUL-terminated, at 0x0080.
        uint16_t a = 0x0080;
        for (uint8_t b : {0x48, 0x45, 0x4C, 0x4C, 0x4F, 0x00})  // "HELLO\0"
            m.bus.memWrite(a++, b);

        // The ISR at RST 7 (0x0038): send the next byte, or jump to 'done' at the NUL.
        a = 0x0038;
        for (uint8_t b : {
                 0x7E,              // MOV A,M     -- next char
                 0xB7,              // ORA A       -- terminator?
                 0xCA, 0x60, 0x00,  // JZ  0060    -- yes: we are done
                 0xD3, 0x03,        // OUT 03      -- no: send it (dismiss + re-arm ACK)
                 0x23,              // INX H
                 0xFB,              // EI
                 0xC9,              // RET
             })
            m.bus.memWrite(a++, b);

        // 'done' at 0x0060: a real driver disarms the printer when the job is finished,
        // which drops the request that woke this final ISR (only a data write or a
        // disable clears it -- §5). The test breakpoints on the HLT that follows.
        a = 0x0060;
        for (uint8_t b : {
                 0x3E, 0x01,        // MVI A,01    -- D1=0 disable, D0=1 no prime
                 0xD3, 0x02,        // OUT 02      -- disarm: drops the pending request
                 0x76,              // HLT
             })
            m.bus.memWrite(a++, b);

        // The mainline: arm the printer interrupt, prime with the first character, then
        // do nothing but wait to be interrupted.
        a = 0x0100;
        for (uint8_t b : {
                 0x31, 0x00, 0x02,  // LXI SP,0200
                 0x21, 0x80, 0x00,  // LXI H,0080  -- HL -> message
                 0x3E, 0x02,        // MVI A,02    -- D1=1 enable interrupts, D0=1 no prime
                 0xD3, 0x02,        // OUT 02      -- arm the structure
                 0x7E,              // MOV A,M     -- the first char
                 0xD3, 0x03,        // OUT 03      -- prime the pump (arms the first ACK)
                 0x23,              // INX H
                 0xFB,              // EI
                 0x76,              // HLT         -- wait for the ACKNOWLEDGE interrupt
                 0xC3, 0x0F, 0x01,  // JMP 010F    -- resume: HLT again until 'done'
             })
            m.bus.memWrite(a++, b);
        m.cpu()->setPc(0x0100);

        m.debug.add(BreakKind::Pc, 0x0064, 0x0064);  // the HLT after the driver disarms
        RunResult res = m.debug.run(200000);

        CHECK(res.why == StopReason::Breakpoint,
              "the driver finished -- every character was carried by an interrupt, "
              "and the HALTed machine really was woken each time");
        CHECK(prn->out() == "HELLO", "and the whole string reached the printer");
        CHECK(!m.bus.intPending(), "and disarming the structure dropped the last request");
    }

    SECTION("out: endpoint -- a punch (write-only host sink), 8-bit clean");
    {
        // The endpoint the C700 captures to. Exercised directly (no board): resolve
        // it, write the bytes the printer would send, and read the file back.
        const std::string path = "c700_filetest.tmp";
        const std::string spec = "out:" + path;
        std::remove(path.c_str());

        std::string err;
        auto s = resolveEndpoint(spec, err);
        CHECK(s != nullptr, "out: resolves to a stream");
        if (s) {
            CHECK(s->describe() == spec, "describe() returns the exact spec (CONFIG SAVE round-trip)");
            CHECK(s->writable(), "a punch is always writable");
            CHECK(!s->readable(), "and not readable -- no in: was given");
            CHECK(s->read(nullptr, 0) == 0, "a read yields nothing, and is not an error");

            const char* msg = "Hi\r\n\x0E";  // includes control bytes -- must survive
            s->write(reinterpret_cast<const uint8_t*>(msg), 5);
            s->flush();

            std::ifstream f(path, std::ios::binary);
            std::string got((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            CHECK(got == std::string(msg, 5), "the bytes on the wire are the bytes in the file");
        }
        std::remove(path.c_str());

        // An unopenable path is a clean refusal, not a crash and not a silent NullStream.
        std::string err2;
        auto bad = resolveEndpoint("out:/no/such/dir/deep/inside/nowhere.txt", err2);
        CHECK(bad == nullptr, "an unopenable path fails");
        CHECK(!err2.empty(), "with a reason the operator can read");
    }
}
