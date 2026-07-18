#include "test.h"

#include "boards/mits-88c700.h"
#include "boards/mits-88cpu.h"
#include "boards/s100-memory.h"
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

        // POLLED CARD: enabling interrupts raises NO request and pulls no wire. bit 7
        // (INTERRUPT REQUEST) stays clear, and the backplane sees nothing on pin 73.
        g.control(0x03);
        CHECK((g.status() & 0x80) == 0, "the interrupt REQUEST bit is not modeled -- stays clear");
        CHECK(!g.m.bus.intPending(), "and no interrupt wire is pulled");
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

    SECTION("file: endpoint -- a write-only host sink, 8-bit clean");
    {
        // The endpoint the C700 captures to. Exercised directly (no board): resolve
        // it, write the bytes the printer would send, and read the file back.
        const std::string path = "c700_filetest.tmp";
        const std::string spec = "file:" + path;
        std::remove(path.c_str());

        std::string err;
        auto s = resolveEndpoint(spec, err);
        CHECK(s != nullptr, "file: resolves to a stream");
        if (s) {
            CHECK(s->describe() == spec, "describe() returns the exact spec (CONFIG SAVE round-trip)");
            CHECK(s->writable(), "a file sink is always writable");
            CHECK(!s->readable(), "and never readable -- nothing comes back off paper");
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
        auto bad = resolveEndpoint("file:/no/such/dir/deep/inside/nowhere.txt", err2);
        CHECK(bad == nullptr, "an unopenable path fails");
        CHECK(!err2.empty(), "with a reason the operator can read");
    }
}
