// The Altair 680b KCACR (reference/Altair 680b KCACR.md).
//
// The KCACR is an 88-ACR (AcrBoard) that grew a motor and moved to the 6800's
// memory map. This suite proves the three things it CHANGES from the plain ACR --
// everything else (the tape MOUNT/WIND/REWIND, the WAV codec, SNAPSHOT of the head)
// is inherited and pinned by test_88acr.cpp:
//
//   * MEMORY-MAPPED, not ported: F010/F011 answer MemRead/MemWrite, no I/O ports.
//   * ACTIVE-LOW: an asserted status/control bit is 0 ("True = Logic 0").
//   * MOTOR CONTROL + INTERRUPTS: control D7/D6 drive the relay, D0/D1 the two
//     interrupt enables that pull the 6800 IRQ, auto-clearing on register access.
//   * ...and its one modem is Kansas City -- it reads a KC tape and refuses MITS FSK.
//
// No filesystem: MemoryMedia through setMediaResolver; the KC WAV is built in memory.

#include "test.h"

#include "boards/mits-680kcacr.h"
#include "core/clock.h"
#include "core/machine.h"
#include "core/statefile.h"
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

uint8_t rd(KcacrBoard& b, uint16_t addr) {
    BusCycle c;
    c.type = Cycle::MemRead;
    c.addr = addr;
    return b.read(c);
}
void wr(KcacrBoard& b, uint16_t addr, uint8_t v) {
    BusCycle c;
    c.type = Cycle::MemWrite;
    c.addr = addr;
    c.data = v;
    b.write(c);
}
bool decodesMem(KcacrBoard& b, uint16_t addr, Cycle type = Cycle::MemRead) {
    BusCycle c;
    c.type = type;
    c.addr = addr;
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

// A WAV holding `n` copies of `byte`, modulated in `f` -- a tape whose TONES are f's.
std::string wavOf(const TapeFormat& f, uint8_t byte, int n) {
    const AudioBuffer          a   = modulate(std::vector<uint8_t>(n, byte), f, 22050, 2.0, 2.0);
    const std::vector<uint8_t> wav = buildWav(a);
    return std::string(wav.begin(), wav.end());
}

struct Rig {
    Machine     m;
    KcacrBoard* kc = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);
        kc = dynamic_cast<KcacrBoard*>(m.add("680kcacr", "kc0", err));
        m.power();
    }
    bool mount(const std::string& p, bool ro = false) {
        std::string err;
        return kc->mount("tape", p, ro, err);
    }
    std::string boardProp(const std::string& name) {
        for (Property& p : kc->properties())
            if (p.name == name) return p.get().s();
        return "<none>";
    }
    bool hasProp(const std::string& name) {
        for (Property& p : kc->properties())
            if (p.name == name) return true;
        return false;
    }
    long long boardInt(const std::string& name) {
        for (Property& p : kc->properties())
            if (p.name == name) return p.get().i();
        return -1;
    }
    bool hasUnitProp(const std::string& unit, const std::string& name) {
        for (Property& p : kc->unitProperties(unit))
            if (p.name == name) return true;
        return false;
    }
    // A byte off the cassette, the way a 6800 loader does: poll F010 for RDA (bit 0
    // LOW = data available, active-low), then LDA F011.
    bool getByte(uint8_t& b, int budget = 6) {
        for (int i = 0; i < budget; ++i) {
            if ((rd(*kc, 0xF010) & 0x01) == 0) {  // D0 LOW = Read Data Available
                b = rd(*kc, 0xF011);
                return true;
            }
            m.clock.advance(m.clock.tStatesPer(30));
        }
        return false;
    }
};

} // namespace

void test_680kcacr() {
    SECTION("680kcacr -- identity and the memory-mapped decode (F010/F011)");
    {
        withTape("");
        Rig r;
        CHECK(r.kc, "the machine takes a 680kcacr");
        CHECK(r.kc->type() == "680kcacr", "and it calls itself a 680kcacr, not an acr");

        // Both registers decode, read and write, in the MEMORY space.
        CHECK(decodesMem(*r.kc, 0xF010, Cycle::MemRead), "F010 status (read) is ours");
        CHECK(decodesMem(*r.kc, 0xF010, Cycle::MemWrite), "F010 control (write) is ours");
        CHECK(decodesMem(*r.kc, 0xF011, Cycle::MemRead), "F011 read data is ours");
        CHECK(decodesMem(*r.kc, 0xF011, Cycle::MemWrite), "F011 write data is ours");

        // Nothing else, and NOT the I/O space -- the 6800 has none.
        CHECK(!decodesMem(*r.kc, 0xF00F), "F00F is not ours");
        CHECK(!decodesMem(*r.kc, 0xF012), "F012 is not ours -- the KCACR is two bytes");
        CHECK(!decodesMem(*r.kc, 0xF000), "F000 belongs to the onboard console, not us");
        BusCycle io;
        io.type = Cycle::IoRead;
        io.addr = 0x10;
        CHECK(!r.kc->decodes(io), "it answers no I/O PORT -- the 6800 has no IN/OUT space");

        // One unit, a tape you MOUNT.
        auto us = r.kc->units();
        CHECK(us.size() == 1 && us[0].name == "tape" && us[0].kind == UnitKind::Tape,
              "one unit, a MOUNTable 'tape'");
    }

    SECTION("680kcacr -- the active-low status word (True = Logic 0)");
    {
        withTape("");
        Rig r;
        // Fresh: transmit buffer empty (D7 asserted = 0), no received byte (D0 = 1),
        // unused D1-D6 float high. So the idle byte is 0x7F, not the ACR's 0x63.
        CHECK(rd(*r.kc, 0xF010) == 0x7F, "idle status is 0x7F -- D7 (TBE) low, D0 (RDA) high, rest 1");
    }
    {
        withTape("AB");
        Rig r;
        CHECK(r.mount("t.tap"), "a cassette goes in");
        CHECK((rd(*r.kc, 0xF010) & 0x01) == 0, "ACTIVE-LOW: with a byte waiting, D0 (RDA) reads ZERO");
        uint8_t b = 0;
        CHECK(r.getByte(b) && b == 'A', "the first byte off the tape is the first on it");
        CHECK(r.getByte(b) && b == 'B', "then the second, over the memory-mapped data register");
    }

    SECTION("680kcacr -- motor control (control D7 on / D6 off), active-low");
    {
        withTape("AB");
        Rig r;
        r.mount("t.tap");
        CHECK(r.kc->motorOn(), "the relay is closed at power-up");

        wr(*r.kc, 0xF010, 0xBF);  // D6 low
        CHECK(!r.kc->motorOn(), "STA F010,#BF turns the motor OFF (D6 low)");
        CHECK(r.boardProp("motor") == "off", "...and SHOW reports it off");

        wr(*r.kc, 0xF010, 0x7F);  // D7 low
        CHECK(r.kc->motorOn(), "STA F010,#7F turns the motor ON (D7 low)");
        CHECK(r.boardProp("motor") == "on", "...and SHOW reports it on");

        // The control writes never touched the data path: the tape still plays in order.
        uint8_t b = 0;
        CHECK(r.getByte(b) && b == 'A', "the tape still plays 'A' after the motor writes");
        CHECK(r.getByte(b) && b == 'B', "...then 'B' -- F010 is the control port, not the data port");

        bool motorWritable = false;
        for (Property& p : r.kc->properties())
            if (p.name == "motor") motorWritable = (bool)p.set;
        CHECK(!motorWritable, "the motor is read-only -- the guest drives it, SET does not");
    }

    SECTION("680kcacr -- interrupts pull the 6800 IRQ, active-low enables, auto-clear");
    {
        withTape("");
        Rig r;
        CHECK(!r.kc->assertsInt(), "freshly powered, nothing asks for an interrupt");

        // Write Interrupt Enable is D1 low (store FD). The transmit buffer is empty on a
        // fresh UART, so an enabled Transmit interrupt fires at once.
        wr(*r.kc, 0xF010, 0xFD);
        CHECK(r.kc->assertsInt(), "STA F010,#FD (write-int enable) + TBE empty pulls IRQ");

        // Reading a register acknowledges: the enable latches reset and IRQ drops.
        (void)rd(*r.kc, 0xF010);
        CHECK(!r.kc->assertsInt(), "reading status resets the interrupt-enable latches (IRQ drops)");

        // Motor-Off (BF) also clears the enables -- reference section 4.
        wr(*r.kc, 0xF010, 0xFD);
        CHECK(r.kc->assertsInt(), "re-enabled");
        wr(*r.kc, 0xF010, 0xBF);
        CHECK(!r.kc->assertsInt(), "Motor-Off (#BF) resets the interrupts too");
    }
    {
        // The Read-Data interrupt (D0 low, store FE): a received byte off the tape, with
        // the read interrupt enabled, pulls IRQ; reading the data drops it.
        withTape("Z");
        Rig r;
        r.mount("t.tap");
        r.kc->pump();  // let the tape deliver a byte into the receiver
        wr(*r.kc, 0xF010, 0xFE);
        CHECK(r.kc->assertsInt(), "STA F010,#FE (read-int enable) + a byte waiting pulls IRQ");
        CHECK(rd(*r.kc, 0xF011) == 'Z', "LDA F011 hands over the byte");
        CHECK(!r.kc->assertsInt(), "...and reading the data drops IRQ (RDA and the enable both clear)");
    }

    SECTION("680kcacr -- the one modem is Kansas City: reads KC, refuses MITS FSK");
    {
        const std::string kansasWav = wavOf(tapeformats::kcs300(), 0x4B, 40);
        const std::string mitsWav   = wavOf(tapeformats::fsk300_1850(), 0x4D, 40);

        {
            withTape(kansasWav);
            Rig         r;
            std::string err;
            CHECK(r.kc->mount("tape", "k.wav", false, err),
                  ("the KCACR reads a Kansas City tape: " + err).c_str());
        }
        {
            withTape(mitsWav);
            Rig         r;
            std::string err;
            CHECK(!r.kc->mount("tape", "m.wav", false, err),
                  "and REFUSES a MITS-FSK tape -- its modem cannot hear it");
            CHECK(err.find("cannot hear") != std::string::npos, ("...and says why: " + err).c_str());
        }
    }

    SECTION("680kcacr -- reflection: the SIO's electrical straps are gone, motor is added");
    {
        withTape("");
        Rig r;
        // The memory-mapped 6800 card has no port base, no Rev0/Rev1 status switch, and
        // no S-100 VI interrupt straps -- those are dropped from the inherited set.
        CHECK(!r.hasProp("port"), "no 'port' -- the KCACR is memory-mapped at a fixed pair");
        CHECK(!r.hasProp("rev"), "no 'rev' -- that is the 88-SIO's status-word switch");
        CHECK(!r.hasProp("in_int") && !r.hasProp("out_int"), "no VI straps -- IRQ goes straight to the 6800");
        // ...but the tape machinery's own unit properties and the motor view remain.
        CHECK(r.hasUnitProp("tape", "mode") && r.hasUnitProp("tape", "rate"),
              "the tape's mode/rate survive (inherited unit properties)");
        CHECK(r.hasProp("motor"), "and the motor relay is exposed, read-only");
        CHECK(r.boardInt("stop_bits") == 2, "the UART is strapped 2 stop bits (reference section 5)");
    }

    SECTION("680kcacr -- snapshot carries the motor relay and the head position");
    {
        withTape("HELLO");
        Rig r;
        r.mount("t.tap");
        uint8_t b = 0;
        r.getByte(b);              // advance the head off the start
        wr(*r.kc, 0xF010, 0xBF);   // motor off -- a runtime latch

        StateWriter w;
        r.kc->serialize(w);

        // A fresh board restores the same motor state (the head position needs a tape,
        // which a bare board has not mounted -- test_88acr covers head restore).
        KcacrBoard fresh;
        StateReader rd2(w.data());
        fresh.deserialize(rd2);
        CHECK(!fresh.motorOn(), "the motor-off relay survives serialize/deserialize");
    }
}
