#include "test.h"

#include "boards/mits-88cpu.h"
#include "boards/mits-88lpc.h"
#include "boards/s100-memory.h"
#include "core/machine.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <string>

using namespace altair;

namespace {

// A machine with an 88-LPC in it and a scripted "printer" on its one line. The stream
// is bound through the REAL connect path (resolveEndpoint("scripted")), so the test
// exercises the same wiring an operator's CONNECT does -- then reads back what was
// "printed" from ScriptedStream::out().
struct Rig {
    Machine         m;
    LpcBoard*       lpt = nullptr;
    ScriptedStream* prn = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);

        m.add("memory", "mem0", err);
        lpt = dynamic_cast<LpcBoard*>(m.add("lpc", "lpt0", err));
        lpt->connect("prn", "scripted", err);
        prn = dynamic_cast<ScriptedStream*>(lpt->unitStream("prn"));

        m.add("8080", "cpu0", err);
        m.power();
    }

    uint8_t status() { return m.bus.ioRead(0x02); }         // IN  02 -- the status word
    uint8_t readData() { return m.bus.ioRead(0x03); }       // IN  03 -- write-only channel
    void    send(uint8_t b) { m.bus.ioWrite(0x03, b); }     // OUT 03 -- a 6-bit char code
    void    control(uint8_t b) { m.bus.ioWrite(0x02, b); }  // OUT 02 -- PRINT/LF/CLEAR/int
};

std::string prop(Board& b, const std::string& name) {
    for (Property& p : b.properties())
        if (p.name == name) return p.get().text(p.radix);
    return "(no such property)";
}

// The status bits, by the manual's names (reference §4). ACTIVE HIGH.
constexpr uint8_t kBufferEmpty = 0x01;  // bit 0: SET = ready for a character
constexpr uint8_t kNotPrinting = 0x02;  // bit 1: SET = idle
constexpr uint8_t kPaperOk     = 0x04;  // bit 2: SET = paper ok
constexpr uint8_t kLineFeedOk  = 0x08;  // bit 3: SET = a line feed would be accepted

// The control commands (reference §3). ACTIVE-HIGH strobes.
constexpr uint8_t kPrint     = 0x01;
constexpr uint8_t kLineFeed  = 0x02;
constexpr uint8_t kClear     = 0x04;
constexpr uint8_t kIntEnable = 0x08;

} // namespace

void test_lpc() {
    SECTION("88-LPC -- the card");
    {
        Rig g;
        CHECK(g.lpt->units().size() == 1, "one line -- a printer has one connector");
        CHECK(g.lpt->units()[0].name == "prn", "and it is called 'prn'");
        CHECK(g.lpt->units()[0].kind == UnitKind::Serial, "a serial unit");

        // Two ports and not one more: Control at 02, Data at 03.
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0x02;
        CHECK(g.lpt->decodes(c), "decodes the control/status channel (even) for IN");
        c.addr = 0x03;
        CHECK(!g.lpt->decodes(c),
              "does NOT decode an IN at the data channel -- a printer sends nothing back, "
              "so nothing drives the bus and it floats (issue #26)");
        c.type = Cycle::IoWrite;
        CHECK(g.lpt->decodes(c), "but it decodes the OUT at the data channel -- that is the card");
        c.addr = 0x02;
        CHECK(g.lpt->decodes(c), "...and the OUT at the control channel");
        c.type = Cycle::IoRead;
        c.addr = 0x04;
        CHECK(!g.lpt->decodes(c), "does NOT decode the port after them");
        c.type = Cycle::MemRead;
        c.addr = 0x02;
        CHECK(!g.lpt->decodes(c), "decodes no MEMORY -- it is an I/O card");

        // A0 picks the channel, so an odd base is not a card you could build.
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

    SECTION("88-LPC -- status is ACTIVE HIGH, and a byte-sink is always ready");
    {
        Rig g;
        uint8_t s = g.status();
        CHECK((s & kBufferEmpty) != 0, "idle: bit 0 SET means the buffer WILL accept a char");
        CHECK((s & kNotPrinting) != 0, "bit 1 SET means not printing (instant here)");
        CHECK((s & kPaperOk) != 0, "bit 2 SET means paper is fine (a file cannot jam)");
        CHECK((s & kLineFeedOk) != 0, "bit 3 SET means a line feed would be accepted");

        // The DATA channel is write-only: reading it drives nothing, so the bus floats.
        // Check the PROVENANCE, not just the byte (issue #26).
        CHECK(g.readData() == 0xFF, "reading the write-only data channel yields FF");
        BusCycle rd;
        rd.type = Cycle::IoRead;
        rd.addr = 0x03;
        CHECK(!g.lpt->decodes(rd),
              "...and that FF is the BUS's: the card does not claim the read at all");
    }

    SECTION("88-LPC -- the data channel decodes a 6-bit code to its glyph");
    {
        // 64-char set 0x20..0x5F, packed with bit 6 = complement of bit 5 (reference §5):
        // 0x01->'A', 0x20->' ', 0x00->'@', 0x1F->'_', 0x1A->'Z'. Only 6 bits matter.
        Rig g;
        g.send(0x01);        // 'A'
        g.send(0x20);        // ' '
        g.send(0x00);        // '@'
        g.send(0x1F);        // '_'
        g.send(0x1A);        // 'Z'
        g.send(0xC1);        // high bits ignored -> 0x01 -> 'A'
        CHECK(g.prn->out().empty(), "nothing is printed until a line commits -- it buffers");
        g.control(kPrint);
        CHECK(g.prn->out() == "A @_ZA\n", "PRINT commits the decoded line, then advances the paper");
    }

    SECTION("88-LPC -- a line commits on PRINT or when the buffer fills to 80");
    {
        Rig g;
        // PRINT a short line.
        g.send(0x08);  // 'H'
        g.send(0x09);  // 'I'
        CHECK(g.prn->out().empty(), "a short line waits for PRINT");
        g.control(kPrint);
        CHECK(g.prn->out() == "HI\n", "PRINT commits it, with a trailing newline");

        // Fill the buffer to 80: it auto-prints with no PRINT command.
        Rig g2;
        for (int i = 0; i < 80; ++i) g2.send(0x01);  // eighty 'A's
        std::string expect(80, 'A');
        expect += '\n';
        CHECK(g2.prn->out() == expect, "the 80th character auto-prints the line");
        // And the buffer is empty again -- an 81st char starts a fresh line.
        g2.send(0x02);  // 'B'
        g2.control(kPrint);
        CHECK(g2.prn->out() == expect + "B\n", "the buffer cleared after the auto-print");
    }

    SECTION("88-LPC -- LINE FEED advances the paper; CLEAR discards the buffer");
    {
        Rig g;
        g.control(kLineFeed);
        CHECK(g.prn->out() == "\n", "LINE FEED emits a bare newline, printing nothing");

        Rig g2;
        g2.send(0x01);  // 'A'
        g2.send(0x02);  // 'B'
        g2.control(kClear);
        g2.send(0x03);  // 'C'
        g2.control(kPrint);
        CHECK(g2.prn->out() == "C\n", "CLEAR dropped the pending 'AB'; only 'C' printed");
    }

    SECTION("88-LPC -- the interrupt structure is polled: no request, no wire");
    {
        Rig g;
        // Enabling interrupts (D3) raises NO request and pulls no wire -- the polled
        // contract. The enable bit is stored for a future interrupt model, but the LPC
        // status word stops at bit 3, so it is not reported there.
        g.control(kIntEnable);
        CHECK(!g.m.bus.intPending(), "enabling interrupts pulls no interrupt wire");
        CHECK((g.status() & 0xF0) == 0, "and nothing lights the undefined status bits 4..7");

        // A printed line does not raise one either.
        g.send(0x01);
        g.control(kPrint | kIntEnable);
        CHECK(!g.m.bus.intPending(), "and a printed line raises none");
    }

    SECTION("88-LPC -- disconnect leaves a dead line, not a dangling pointer");
    {
        Rig g;
        std::string err;
        CHECK(g.lpt->disconnect("prn", err), "the printer is unplugged");
        CHECK(g.lpt->units()[0].state == "null", "and the unit reports a dead line");
        // Writes to a dead line vanish; the card still answers status (ready, because a
        // NullStream always takes a byte).
        g.send(0x01);
        g.control(kPrint);  // into the void
        CHECK((g.status() & kBufferEmpty) != 0, "an unconnected printer still reads ready");

        CHECK(!g.lpt->connect("lp", "null", err), "and there is no unit but 'prn'");
        CHECK(err.find("prn") != std::string::npos, "the error names the real one");
    }

    SECTION("88-LPC -- connect round-trips the endpoint spec through the property");
    {
        Rig g;
        std::string err;
        CHECK(g.lpt->connect("prn", "null", err), "connect to null");
        CHECK(prop(*g.lpt, "connect") == "null", "and the property reads it back for CONFIG SAVE");
    }
}
