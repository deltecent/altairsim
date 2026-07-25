// The 88-UIO (docs/boards/mits-88uio.md).
//
// TWO CARDS ON ONE CARD, and this suite's job is to prove BOTH halves are real, are at
// their own ports, and do not cross-talk -- plus the two things the UIO adds over the
// plain 88-ACR it derives from: MOTOR CONTROL and a SW-1-SELECTED MODULATION.
//
//   * The cassette half is an 88-ACR verbatim (inherited). Its own suite
//     (test_88acr.cpp) pins the tape machinery; here we check only that it still
//     answers at 0x06/0x07 with the inverted status word, so a refactor that broke the
//     inheritance would go red here too.
//   * The serial half is a live, independent 6850 at 0x10/0x11 -- a byte fed to it
//     arrives THERE and the cassette status does not budge.
//   * MOTOR CONTROL: OUT to 0x06 latches the relay from D6/D7 and must never corrupt the
//     data path underneath it.
//   * SW-1: the modem reads exactly the modulation the switch selects, and REFUSES the
//     other -- Kansas City under `kansas`, MITS FSK under `mits`.
//
// No filesystem: MemoryMedia through setMediaResolver; a ScriptedStream for the serial line.

#include "test.h"

#include "boards/mits-88uio.h"
#include "chips/sio2port.h"
#include "core/clock.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/endpoint.h"
#include "host/media.h"
#include "host/stream.h"
#include "host/tape.h"
#include "host/tapemodem.h"
#include "host/wav.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace altair;

namespace {

uint8_t in(UioBoard& b, uint8_t port) {
    BusCycle c;
    c.type = Cycle::IoRead;
    c.addr = port;
    return b.read(c);
}
void out(UioBoard& b, uint8_t port, uint8_t v) {
    BusCycle c;
    c.type = Cycle::IoWrite;
    c.addr = port;
    c.data = v;
    b.write(c);
}
bool decodesIo(UioBoard& b, uint8_t port) {
    BusCycle c;
    c.type = Cycle::IoRead;
    c.addr = port;
    return b.decodes(c);
}

MemoryMedia* g_media = nullptr;
void withTape(const std::string& contents, bool ro = false) {
    setMediaResolver([contents, ro](const std::string& path, bool wantRo, std::string&) {
        auto m = std::make_unique<MemoryMedia>(
            path, std::vector<uint8_t>(contents.begin(), contents.end()), ro || wantRo);
        g_media = m.get();
        return m;
    });
}
std::string tapeBytes() {
    if (!g_media) return "";
    const auto& v = g_media->bytes();
    return std::string(v.begin(), v.end());
}

struct Rig {
    Machine    m;
    UioBoard*  uio = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);  // the card drives pin 73 from BOTH sections; it must say so
        uio = dynamic_cast<UioBoard*>(m.add("uio", "uio0", err));
        m.power();
    }
    bool mount(const std::string& p, bool ro = false) {
        std::string err;
        return uio->mount("tape", p, ro, err);
    }
    bool press(const char* mode) {
        std::string err;
        return setUnitProperty(*uio, "tape", "mode", mode, err);
    }
    bool rewind() {
        std::ostringstream o;
        std::string        err;
        return uio->runCommand("REWIND", {"REW", "uio0:tape"}, o, err);
    }
    std::string boardProp(const std::string& name) {
        for (Property& p : uio->properties())
            if (p.name == name) return p.get().s();
        return "<none>";
    }
    Value boardVal(const std::string& name) {
        for (Property& p : uio->properties())
            if (p.name == name) return p.get();
        return Value::ofStr("<none>");
    }
    // A byte off the CASSETTE, the way a loader does: poll 0x06 for DAV, read 0x07.
    bool getCassetteByte(uint8_t& b, int budget = 4) {
        for (int i = 0; i < budget; ++i) {
            if ((in(*uio, 0x06) & 0x01) == 0) {  // bit 0 LOW = data available (inverted)
                b = in(*uio, 0x07);
                return true;
            }
            m.clock.advance(m.clock.tStatesPer(30));
        }
        return false;
    }
};

// A WAV holding `n` copies of `byte`, modulated in `f` -- a tape whose TONES are f's.
std::string wavOf(const TapeFormat& f, uint8_t byte, int n) {
    const AudioBuffer a = modulate(std::vector<uint8_t>(n, byte), f, 22050, 2.0, 2.0);
    const std::vector<uint8_t> wav = buildWav(a);
    return std::string(wav.begin(), wav.end());
}

} // namespace

void test_88uio() {
    SECTION("88-UIO -- serial + cassette on one board");

    // -----------------------------------------------------------------------
    // 1. TWO SECTIONS, ONE CARD -- at the standard Altair addresses by default.
    // -----------------------------------------------------------------------
    {
        withTape("");
        Rig r;
        CHECK(r.uio, "the machine takes a uio");
        CHECK(r.uio->type() == "uio", "and it calls itself a uio, not an acr");

        // The cassette straps are the ACR's, inherited: 0x06, 300 baud, 8N1, Rev 1.
        CHECK(r.boardVal("port").i() == 0x06, "cassette port 006 -- inherited from the ACR");
        CHECK(r.boardVal("baud").i() == 300, "cassette 300 baud");
        CHECK(r.boardVal("data_bits").i() == 8 && r.boardVal("stop_bits").i() == 1, "8N1 cassette");
        // ...and the serial base, the SW-1 modulation, and the motor state are the UIO's own.
        CHECK(r.boardVal("serial_port").i() == 0x10, "serial base 0x10 -- 2SIO Port A (SW-2 off)");
        CHECK(r.boardProp("standard") == "mits", "SW-1 defaults to MITS (2400/1850)");
        CHECK(r.boardProp("motor") == "on", "the motor relay comes up closed (on) at power-up");

        // Both port ranges decode; the gaps between and after do not.
        CHECK(decodesIo(*r.uio, 0x06) && decodesIo(*r.uio, 0x07), "decodes the cassette pair 0x06/0x07");
        CHECK(decodesIo(*r.uio, 0x10) && decodesIo(*r.uio, 0x11), "decodes the serial pair 0x10/0x11");
        CHECK(!decodesIo(*r.uio, 0x08), "but NOT 0x08 -- that is the disk's");
        CHECK(!decodesIo(*r.uio, 0x12), "and NOT 0x12 -- the serial half is ONE channel, not a 2SIO");
        BusCycle mem;
        mem.type = Cycle::MemRead;
        mem.addr = 0x10;
        CHECK(!r.uio->decodes(mem), "it answers no MEMORY -- both halves are I/O");

        // Two units, of two kinds: the tape you MOUNT, the serial line you CONNECT.
        auto us = r.uio->units();
        CHECK(us.size() == 2, "two units");
        bool tape = false, serial = false;
        for (auto& u : us) {
            if (u.name == "tape" && u.kind == UnitKind::Tape) tape = true;
            if (u.name == "serial" && u.kind == UnitKind::Serial) serial = true;
        }
        CHECK(tape, "a 'tape' unit, MOUNTable");
        CHECK(serial, "a 'serial' unit, CONNECTable");
    }

    // -----------------------------------------------------------------------
    // 1b. THE TWO SECTIONS MAY NOT OVERLAP. decodes() asks the serial section FIRST, so
    //     an overlap would silently shadow the cassette -- and the bus's cross-BOARD
    //     conflict check cannot see a clash inside one card. Both base setters refuse it.
    // -----------------------------------------------------------------------
    {
        withTape("");
        Rig r;
        std::string err;

        // The serial base onto the cassette pair (0x06/0x07), and one port either side.
        CHECK(!setProperty(*r.uio, "serial_port", "06", err), "serial_port=06 (onto the cassette) is refused");
        CHECK(err.find("overlap") != std::string::npos, ("...and says why: " + err).c_str());
        CHECK(!setProperty(*r.uio, "serial_port", "07", err), "serial_port=07 (the cassette's odd half) is refused");
        CHECK(!setProperty(*r.uio, "serial_port", "05", err), "serial_port=05 (its pair reaches 0x06) is refused");

        // ...and the cassette base onto the serial pair (0x10/0x11).
        CHECK(!setProperty(*r.uio, "port", "10", err), "cassette port=10 (onto the 6850) is refused");
        CHECK(err.find("overlap") != std::string::npos, ("...and says why: " + err).c_str());

        // A clear move is fine, and the sections decode where they landed.
        CHECK(setProperty(*r.uio, "serial_port", "18", err), ("serial_port=18 clears the cassette: " + err).c_str());
        CHECK(decodesIo(*r.uio, 0x18) && decodesIo(*r.uio, 0x19), "the serial section moved to 0x18/0x19");
        CHECK(!decodesIo(*r.uio, 0x10), "...and no longer answers 0x10");
        CHECK(decodesIo(*r.uio, 0x06), "the cassette is untouched at 0x06");
    }

    // -----------------------------------------------------------------------
    // 2. THE CASSETTE HALF IS THE 88-ACR, INVERTED STATUS AND ALL -- the inheritance,
    //    guarded. (The full tape machinery lives in test_88acr.cpp.)
    // -----------------------------------------------------------------------
    {
        withTape("");
        Rig r;
        CHECK(in(*r.uio, 0x06) == 0x63, "idle cassette status is 0x63 -- the Rev 1 88-SIO's byte");
    }
    {
        withTape("AB");
        Rig r;
        CHECK(r.mount("t.tap"), "a cassette goes in");
        CHECK((in(*r.uio, 0x06) & 0x01) == 0, "INVERTED: with a byte waiting, bit 0 reads ZERO");
        uint8_t b = 0;
        CHECK(r.getCassetteByte(b) && b == 'A', "the first byte off the tape is the first on it");
        CHECK(r.getCassetteByte(b) && b == 'B', "then the second");
    }

    // -----------------------------------------------------------------------
    // 3. MOTOR CONTROL -- the thing the plain 88-ACR does not have.
    //
    // OUT to the cassette control port latches the relay from D6/D7: D7 low = ON
    // (OUT 6,127), D6 low = OFF (OUT 6,191). It must NEVER corrupt the data path -- the
    // register is shared with the interrupt-enable bits and the tape UART underneath.
    // -----------------------------------------------------------------------
    {
        withTape("AB");
        Rig r;
        r.mount("t.tap");

        CHECK(r.uio->motorOn(), "the relay is closed at power-up");

        out(*r.uio, 0x06, 191);  // 0xBF: D6 low
        CHECK(!r.uio->motorOn(), "OUT 6,191 turns the motor OFF (D6 low)");
        CHECK(r.boardProp("motor") == "off", "...and SHOW reports it off");

        out(*r.uio, 0x06, 127);  // 0x7F: D7 low
        CHECK(r.uio->motorOn(), "OUT 6,127 turns the motor ON (D7 low)");
        CHECK(r.boardProp("motor") == "on", "...and SHOW reports it on");

        // AND THE DATA PATH IS UNTOUCHED: after those OUTs to the control port, the tape
        // still reads back its bytes in order. An OUT that leaked into the UART or wrote a
        // byte to the tape would break this.
        uint8_t b = 0;
        CHECK(r.getCassetteByte(b) && b == 'A', "the tape still plays 'A' after the motor OUTs");
        CHECK(r.getCassetteByte(b) && b == 'B', "...then 'B' -- the OUTs never touched the data path");

        // The motor is READ-ONLY to the operator: the guest drives it, SET does not.
        bool motorWritable = false;
        for (Property& p : r.uio->properties())
            if (p.name == "motor") motorWritable = (bool)p.set;
        CHECK(!motorWritable, "the motor is a measurement of what the guest did, not a switch");
    }
    {
        // An OUT to the control port writes NO byte to the tape -- it is not the data port.
        withTape("OLDTAPE!");
        Rig r;
        r.mount("t.tap");
        r.press("record");
        r.rewind();
        out(*r.uio, 0x06, 127);  // motor on
        out(*r.uio, 0x06, 191);  // motor off
        r.m.clock.advance(r.m.clock.tStatesPer(30));
        CHECK(tapeBytes() == "OLDTAPE!", "motor OUTs cut nothing into the tape -- 0x06 is not the data port");
    }

    // -----------------------------------------------------------------------
    // 4. THE SERIAL HALF IS A LIVE, INDEPENDENT 6850 at 0x10/0x11.
    //
    // A byte fed to the serial line arrives THERE, in TRUE sense (bit set = ready), and
    // the cassette status word does not move a bit. Two chips, two conventions, one card.
    // -----------------------------------------------------------------------
    {
        ScriptedStream* tty = nullptr;
        Sio2Port::setResolver([&](const std::string&, std::string&) -> std::unique_ptr<ByteStream> {
            auto s = std::make_unique<ScriptedStream>();
            tty    = s.get();
            return s;
        });

        withTape("");
        Rig r;
        std::string err;

        // CONNECT routes to the serial section; the tape unit and a bogus unit do not.
        CHECK(r.uio->connect("serial", "scripted", err), ("CONNECT serial: " + err).c_str());
        CHECK(tty, "a stream landed on the serial channel");
        CHECK(!r.uio->connect("tape", "socket:2400", err), "CONNECT tape is refused (soldered to the modem)");
        CHECK(!r.uio->connect("bogus", "x", err), "CONNECT to a unit that is not there is refused");
        CHECK(err.find("uio") != std::string::npos, ("...with the uio's own message: " + err).c_str());

        // Feed a byte to the serial line and let a character time pass.
        tty->feed("Q");
        for (int i = 0; i < 8 && (in(*r.uio, 0x10) & 0x01) == 0; ++i)
            r.m.clock.advance(500);

        CHECK((in(*r.uio, 0x10) & 0x01) != 0, "serial RDRF is TRUE sense: bit 0 SET means a byte is here");
        CHECK(in(*r.uio, 0x11) == 'Q', "and it is the byte we fed -- on the SERIAL data port, 0x11");

        // The cassette status is untouched -- idle 0x63, its own inverted convention.
        CHECK(in(*r.uio, 0x06) == 0x63, "the cassette half never saw the serial byte: still idle 0x63");

        Sio2Port::setResolver(resolveEndpoint);  // put the real one back for the other suites
    }

    // -----------------------------------------------------------------------
    // 5. SW-1 -- the modem reads exactly the modulation the switch selects, and refuses
    //    the other. This is the whole reason modem() is virtual.
    // -----------------------------------------------------------------------
    {
        const std::string mitsWav   = wavOf(tapeformats::fsk300_1850(), 0x41, 40);
        const std::string kansasWav = wavOf(tapeformats::kcs300(), 0x42, 40);

        // Default (mits): the MITS tape mounts, the Kansas City tape is refused.
        {
            withTape(mitsWav);
            Rig r;
            CHECK(r.mount("m.wav"), "SW-1=mits reads a MITS-modulated tape");
        }
        {
            withTape(kansasWav);
            Rig         r;
            std::string err;
            CHECK(!r.uio->mount("tape", "k.wav", false, err),
                  "SW-1=mits REFUSES a Kansas City tape -- the modem cannot hear it");
            CHECK(err.find("cannot hear") != std::string::npos, ("...and says why: " + err).c_str());
        }

        // Flip SW-1 to Kansas City and it is the other way round.
        {
            withTape(kansasWav);
            Rig         r;
            std::string err;
            CHECK(setProperty(*r.uio, "standard", "kansas", err), "SET standard=kansas");
            CHECK(r.uio->mount("tape", "k.wav", false, err), ("SW-1=kansas reads a Kansas City tape: " + err).c_str());
        }
        {
            withTape(mitsWav);
            Rig         r;
            std::string err;
            setProperty(*r.uio, "standard", "kansas", err);
            CHECK(!r.uio->mount("tape", "m.wav", false, err),
                  "SW-1=kansas REFUSES a MITS tape -- now it is the FSK one it cannot hear");
        }
    }

    // -----------------------------------------------------------------------
    // 6. SNAPSHOT -- the motor relay is runtime state and travels; the two base ports and
    //    SW-1 are config and do not. Restore lands the motor where it was.
    // -----------------------------------------------------------------------
    {
        withTape("");
        Rig r;
        out(*r.uio, 0x06, 191);  // motor OFF -- a runtime fact the guest set
        CHECK(!r.uio->motorOn(), "motor off before the snapshot");

        StateWriter w;
        r.uio->serialize(w);

        Rig r2;
        CHECK(r2.uio->motorOn(), "a fresh board comes up with the motor on");
        StateReader rd(w.data());
        r2.uio->deserialize(rd);
        CHECK(!r2.uio->motorOn(), "...and RESTORE brings the motor back OFF");
    }
}
