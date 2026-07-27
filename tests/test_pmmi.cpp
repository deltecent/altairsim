#include "test.h"

#include "boards/mits-88cpu.h"
#include "boards/pmmi-mm103.h"
#include "boards/s100-memory.h"
#include "cli/monitor.h"
#include "core/machine.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace altair;

namespace {

// A machine with a PMMI MM-103 at its default base (0xC0) and a scripted "phone
// line" on its one serial unit. The stream is bound through the REAL connect path
// (resolveEndpoint("scripted")), so the test drives the same wiring an operator's
// CONNECT does -- then types into feed() and reads what the guest sent out of out().
struct Rig {
    Machine         m;
    PmmiBoard*      pmmi = nullptr;
    ScriptedStream* line = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);

        m.add("memory", "mem0", err);
        pmmi = dynamic_cast<PmmiBoard*>(m.add("pmmi", "pmmi0", err));
        pmmi->connect("line", "scripted", err);
        line = dynamic_cast<ScriptedStream*>(pmmi->unitStream("line"));

        m.add("8080", "cpu0", err);
        m.power();
    }

    // The four ports, BASE = 0xC0. Read and write mean different registers.
    uint8_t status() { return m.bus.ioRead(0xC0); }        // IN  BA+0 -- UART status
    uint8_t recv() { return m.bus.ioRead(0xC1); }          // IN  BA+1 -- receive data
    uint8_t modem() { return m.bus.ioRead(0xC2); }         // IN  BA+2 -- modem status
    uint8_t strobe() { return m.bus.ioRead(0xC3); }        // IN  BA+3 -- strobe, floats FF
    void    control(uint8_t b) { m.bus.ioWrite(0xC0, b); } // OUT BA+0 -- format / SH,RI / int
    void    send(uint8_t b) { m.bus.ioWrite(0xC1, b); }    // OUT BA+1 -- transmit data
    void    rate(uint8_t b) { m.bus.ioWrite(0xC2, b); }    // OUT BA+2 -- rate / mask staging
    void    modemctl(uint8_t b) { m.bus.ioWrite(0xC3, b); }// OUT BA+3 -- 6860 modem control
};

std::string prop(Board& b, const std::string& name) {
    for (Property& p : b.properties())
        if (p.name == name) return p.get().text(p.radix);
    return "(no such property)";
}

// UART status bits (reference §5, IN BA+0). ALL ACTIVE HIGH.
constexpr uint8_t kTbmt = 0x01;  // bit 0: transmit buffer empty
constexpr uint8_t kDav  = 0x02;  // bit 1: received char available
constexpr uint8_t kTeoc = 0x04;  // bit 2: transmitter serializer done

} // namespace

void test_pmmi() {
    SECTION("PMMI MM-103 -- the card: four ports, one serial line");
    {
        Rig g;
        CHECK(g.pmmi->units().size() == 1, "one line -- a modem has one connector");
        CHECK(g.pmmi->units()[0].name == "line", "and it is called 'line'");
        CHECK(g.pmmi->units()[0].kind == UnitKind::Serial, "a serial unit");

        // Four consecutive ports at BASE..BASE+3, read AND write, and nothing else.
        BusCycle c;
        for (int off = 0; off < 4; ++off) {
            c.type = Cycle::IoRead;
            c.addr = 0xC0 + off;
            CHECK(g.pmmi->decodes(c), "decodes the IN at BASE+off");
            c.type = Cycle::IoWrite;
            CHECK(g.pmmi->decodes(c), "and the OUT at BASE+off");
        }
        c.type = Cycle::IoRead;
        c.addr = 0xC4;
        CHECK(!g.pmmi->decodes(c), "does NOT decode the port after them");
        c.type = Cycle::MemRead;
        c.addr = 0xC0;
        CHECK(!g.pmmi->decodes(c), "decodes no MEMORY -- it is an I/O card");
    }

    SECTION("PMMI MM-103 -- the base sits on a 4-port boundary");
    {
        Rig         g;
        std::string err;
        CHECK(!setProperty(*g.pmmi, "port", "C2", err), "an off-boundary base is refused");
        CHECK(err.find("multiple of 4") != std::string::npos, "and it says why, in words");
        CHECK(setProperty(*g.pmmi, "port", "E0", err), "the North Star alternate E0 is taken");
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0xE3;
        CHECK(g.pmmi->decodes(c), "and the card MOVED -- the decode followed the base");
        c.addr = 0xC0;
        CHECK(!g.pmmi->decodes(c), "and no longer answers where it was");
    }

    SECTION("PMMI MM-103 -- read != write: the control registers are write-only");
    {
        Rig g;
        // Writing a control register and reading the SAME port yields the OTHER
        // register entirely -- the write is shadowed, never read back here.
        g.control(0x5C);  // 8N2, SH/RI clear -- a real §8 program value
        CHECK(g.status() != 0x5C, "IN BA+0 reads UART status, not the format just written");

        g.rate(0x34);  // divisor 52 = 300 baud
        CHECK(g.modem() != 0x34, "IN BA+2 reads modem status, not the divisor just written");

        g.modemctl(0x7F);  // 6860 enabled/idle
        CHECK(g.strobe() == 0xFF, "IN BA+3 is an undriven strobe -- the bus floats 0xFF");
    }

    SECTION("PMMI MM-103 -- IN BA+2 is the fixed 'ready' modem-status stub");
    {
        Rig g;
        // This milestone does not model the handshake: modem status is a constant
        // meaning connected / clear-to-send / off-hook (reference §5).
        CHECK(g.modem() == 0x43, "modem status reads the documented 0x43 constant");
    }

    SECTION("PMMI MM-103 -- UART status is active high; a byte-sink starts ready");
    {
        Rig g;
        uint8_t s = g.status();
        CHECK((s & kTbmt) != 0, "out of reset: TBMT set -- the transmitter is ready");
        CHECK((s & kTeoc) != 0, "and TEOC set -- nothing is on the wire");
        CHECK((s & kDav) == 0, "DAV clear -- nothing has arrived yet");
    }

    SECTION("PMMI MM-103 -- a character written to the data port lands on the line");
    {
        Rig g;
        g.send('H');
        g.send('i');
        CHECK(g.line->out() == "Hi", "the bytes reached the far end, in order");
    }

    SECTION("PMMI MM-103 -- a character on the line is received in order");
    {
        Rig g;
        g.line->feed("Hi");

        CHECK((g.status() & kDav) != 0, "DAV set once the first character arrives");
        CHECK(g.recv() == 'H', "the data port yields it");

        g.m.clock.advance(50000);  // past one character time so the next byte can land
        CHECK((g.status() & kDav) != 0, "DAV set again for the second");
        CHECK(g.recv() == 'i', "and it is the next byte, in order");
    }

    SECTION("PMMI MM-103 -- OUT BA+0 programs the UART frame, OUT BA+2 the baud");
    {
        Rig g;
        CHECK(prop(*g.pmmi, "frame") == "8N1", "the default frame is 8N1");

        // 7 data bits (NB=10), even parity (NP=0, EPS=1), 2 stop bits (TSB=1).
        g.control(0x68);
        CHECK(prop(*g.pmmi, "frame") == "7E2", "OUT BA+0 bits 2-6 reprogram the frame");

        g.rate(52);  // 250000 / (16 * 52) = 300
        CHECK(prop(*g.pmmi, "baud") == "300", "OUT BA+2 divisor 52 gives 300 baud");
    }

    SECTION("PMMI MM-103 -- SHOW renders configuration and live read-only status");
    {
        Rig                g;
        Monitor            mon(g.m);
        std::ostringstream out;

        mon.exec("SHOW pmmi0", out);
        std::string s = out.str();
        CHECK(s.find("port") != std::string::npos, "SHOW names the base-address knob");
        CHECK(s.find("frame") != std::string::npos, "and the read-only frame");
        CHECK(s.find("baud") != std::string::npos, "and the read-only baud");
        CHECK(s.find("lines") != std::string::npos, "and the decoded modem lines");
        CHECK(s.find("(read-only)") != std::string::npos, "status fields render as read-only");
        CHECK(s.find("scripted") != std::string::npos, "and the connection shows the endpoint");

        // The SH/RI shadow is visible in the decoded 'lines' string.
        g.control(0x01);  // SH = 1 (off-hook / originate)
        CHECK(prop(*g.pmmi, "lines").find("SH") != std::string::npos,
              "asserting SH shows an off-hook switch-hook in the lines string");
    }

    SECTION("PMMI MM-103 -- CONNECT to out:/in: files moves real bytes");
    {
        // OUT: a punch. Connect the line to a host file, transmit, read it back.
        const std::string opath = "pmmi_out.tmp";
        std::remove(opath.c_str());
        {
            Rig         g;
            std::string err;
            CHECK(g.pmmi->connect("line", "out:" + opath, err), "connect the line to an out: file");
            g.send('O');
            g.send('K');
            std::ifstream f(opath, std::ios::binary);
            std::string   got((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            CHECK(got == "OK", "the transmitted bytes are in the file");
        }
        std::remove(opath.c_str());

        // IN: a reader. Prime a host file, connect the line to it, receive it back.
        const std::string ipath = "pmmi_in.tmp";
        {
            std::ofstream f(ipath, std::ios::binary);
            f << "AB";
        }
        {
            Rig         g;
            std::string err;
            CHECK(g.pmmi->connect("line", "in:" + ipath, err), "connect the line to an in: file");
            CHECK((g.status() & kDav) != 0, "the reader delivers its first byte");
            CHECK(g.recv() == 'A', "and it is the first byte of the file");
            g.m.clock.advance(50000);
            CHECK((g.status() & kDav) != 0, "and the next");
            CHECK(g.recv() == 'B', "in order");
        }
        std::remove(ipath.c_str());
    }

    SECTION("PMMI MM-103 -- disconnect leaves a dead line, not a dangling pointer");
    {
        Rig         g;
        std::string err;
        CHECK(g.pmmi->disconnect("line", err), "the line is unplugged");
        CHECK(g.pmmi->units()[0].state == "null", "and the unit reports a dead line");
        CHECK((g.status() & kTbmt) != 0, "an unconnected line still reads ready to send");
        g.send('Z');  // into the void -- a NullStream takes it, and nothing crashes

        CHECK(!g.pmmi->connect("tty", "null", err), "and there is no unit but 'line'");
        CHECK(err.find("line") != std::string::npos, "the error names the real one");
    }
}
