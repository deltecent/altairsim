#include "test.h"

#include "boards/mits-frontpanel.h"
#include "boards/proctech-sol.h"
#include "boards/proctech-vdm1.h"
#include "boards/s100-memory.h"
#include "core/machine.h"
#include "core/roms.h"
#include "host/display_null.h"
#include "host/stream.h"

#include <memory>
#include <string>

using namespace altair;

namespace {

// The Sol-PC I/O card on a bus, with a crystal (an 8080 supplies Machine::clock) and,
// optionally, the VDM-1 it forwards its scroll writes to. Built by hand rather than
// from a .toml so a config bug cannot make these go red. The ports are driven straight
// over the bus; the CPU is here only for its clock (the serial UART's deadlines).
struct Rig {
    Machine     m;
    NullDisplay disp;
    SolBoard*   sol = nullptr;
    VdmBoard*   vdm = nullptr;

    explicit Rig(bool withVdm = false) {
        std::string err;
        m.bus.setVerify(true);
        if (withVdm) {
            vdm = dynamic_cast<VdmBoard*>(m.add("vdm1", "vdm0", err));
            VdmBoard::setDisplay(&disp);
        }
        sol = dynamic_cast<SolBoard*>(m.add("sol", "sol0", err));
        m.add("8080", "cpu0", err);
        m.power();
    }

    // CONNECT a unit to a fresh scripted terminal, through the real resolver path, and
    // hand back the pointer so a test can feed()/read out() of it.
    ScriptedStream* script(const std::string& unit) {
        std::string err;
        sol->connect(unit, "scripted", err);
        return dynamic_cast<ScriptedStream*>(sol->unitStream(unit));
    }

    uint8_t in(uint8_t p) { return m.bus.ioRead(p); }
    void    out(uint8_t p, uint8_t v) { m.bus.ioWrite(p, v); }
    void    lineTime() { m.clock.advance(5000); }  // one 9600-baud char, comfortably
};

// The Sol-PC ports, by SOLOS's names (reference/Sol-20.md).
constexpr uint8_t SERST = 0xF8, SDATA = 0xF9, STAPT = 0xFA;
constexpr uint8_t KDATA = 0xFC, DSTAT = 0xFE;

// FA (general status) bit masks.
constexpr uint8_t KDR = 0x01;   // keyboard data ready -- ACTIVE LOW
constexpr uint8_t TTBE = 0x80;  // tape transmitter empty -- active high
// F8 (serial status) bit masks.
constexpr uint8_t SDR = 0x40;   // serial data ready -- active high
constexpr uint8_t STBE = 0x80;  // serial transmitter empty -- active high

// A machine that actually boots SOLOS: RAM under the ROM, SOLOS at C000, the VDM at
// CC00, the fp and the sol card. Runs the 8080 until the '>' prompt appears in the
// VDM's screen RAM, or a generous step budget is spent.
struct BootRig {
    Machine     m;
    NullDisplay disp;

    BootRig() {
        std::string err;

        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region r;
        r.kind = RegionKind::Ram;
        r.at   = 0x0000;
        r.size = 0xCC00;  // 0000-CBFF: RAM under the VDM (which owns CC00-CFFF)
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);

        m.add("vdm1", "vdm0", err);
        VdmBoard::setDisplay(&disp);
        m.add("sol", "sol0", err);
        auto* fp = dynamic_cast<FrontPanelBoard*>(m.add("fp", "fp0", err));
        setProperty(*fp, "sense", "0", err);
        m.add("8080", "cpu0", err);
        m.power();

        // Load SOLOS where the ROM would be and start the CPU at its cold-start entry.
        const BuiltinRom* rom = findRom("solos");
        Image img;
        std::string rerr;
        decodeRom(*rom, 0, img, rerr);
        auto flat = img.flat();
        for (size_t i = 0; i < flat.size(); ++i)
            m.bus.memWrite((uint16_t)(img.lo() + i), flat[i]);
        m.cpu()->setPc(0xC000);
    }

    // Is character `ch` anywhere in the 1 KB VDM screen (ignoring the D7 cursor bit)?
    bool screenHas(uint8_t ch) {
        for (uint16_t off = 0; off < 1024; ++off)
            if ((m.bus.memRead(0xCC00 + off) & 0x7F) == ch) return true;
        return false;
    }

    bool runToPrompt(uint64_t budget) {
        for (uint64_t i = 0; i < budget; ++i) {
            StepResult s = m.master()->step(m.bus);
            m.clock.advance(s.tStates);
            if ((i & 0x3FFF) == 0 && screenHas('>')) return true;
        }
        return screenHas('>');
    }
};

}  // namespace

void test_sol() {
    SECTION("Sol I/O -- decodes the seven Sol-PC ports F8..FE, and nothing else");
    {
        Rig g;
        for (uint16_t p = 0xF8; p <= 0xFE; ++p) {
            BusCycle c;
            c.type = Cycle::IoRead;
            c.addr = p;
            CHECK(g.sol->decodes(c), "decodes an F8..FE port");
        }
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0xF7;
        CHECK(!g.sol->decodes(c), "not F7 (below the block)");
        c.addr = 0xFF;
        CHECK(!g.sol->decodes(c), "not FF (the sense switches -- that is the fp board)");
        c.type = Cycle::MemRead;
        c.addr = 0xF8;
        CHECK(!g.sol->decodes(c), "and it is an I/O card -- it decodes no memory");
    }

    SECTION("Sol I/O -- the keyboard: a key waiting reads KDR active-low, FC consumes it");
    {
        Rig g;
        ScriptedStream* kbd = g.script("keyboard");

        CHECK((g.in(STAPT) & KDR) != 0, "idle: KDR is 1 (no key -- active low)");
        CHECK(!g.sol->keyboardReady(), "and the holding register is empty");

        kbd->feed("A");
        g.sol->pump();  // the host turn latches one character
        CHECK((g.in(STAPT) & KDR) == 0, "a key waiting: KDR falls to 0");
        CHECK(g.sol->keyboardReady(), "the holding register is full");

        CHECK(g.in(KDATA) == 'A', "IN 0FCH returns the keystroke");
        CHECK((g.in(STAPT) & KDR) != 0, "and reading it clears KDR back to 1");

        // Two keys, one per host turn (one strobe at a time, like the hardware).
        kbd->feed("BC");
        g.sol->pump();
        CHECK(g.in(KDATA) == 'B', "first of two");
        g.sol->pump();
        CHECK(g.in(KDATA) == 'C', "then the second");
    }

    SECTION("Sol I/O -- the serial port round-trips on F8/F9, active-high status");
    {
        Rig g;
        ScriptedStream* ser = g.script("serial");

        CHECK((g.in(SERST) & STBE) != 0, "idle: the transmitter is empty (STBE=1)");
        CHECK((g.in(SERST) & SDR) == 0, "and no receive data yet (SDR=0)");

        ser->feed("Z");
        g.lineTime();     // let the character finish arriving on the line
        g.sol->pump();    // the receiver runs on the host turn
        CHECK((g.in(SERST) & SDR) != 0, "a byte arrived: SDR=1");
        CHECK(g.in(SDATA) == 'Z', "IN 0F9H returns it");
        CHECK((g.in(SERST) & SDR) == 0, "and reading it clears SDR");

        g.out(SDATA, 'Q');  // OUT 0F9H -- the character goes out the line
        CHECK(ser->out() == "Q", "the transmitted byte reached the endpoint");
    }

    SECTION("Sol I/O -- FA idle bits: no parallel input, tape idle");
    {
        Rig g;
        uint8_t s = g.in(STAPT);
        CHECK((s & 0x02) != 0, "PDR=1: no parallel input source (active low)");
        CHECK((s & TTBE) != 0, "TTBE=1: the (deferred) tape transmitter reads idle");
        CHECK((s & 0x40) == 0, "TDR=0: no tape byte is ever ready");
    }

    SECTION("Sol I/O -- OUT 0FEH forwards the display scroll to the VDM-1");
    {
        Rig g(/*withVdm=*/true);
        CHECK(g.vdm->scroll() == 0, "the VDM starts unscrolled");
        g.out(DSTAT, 0x05);
        CHECK(g.vdm->scroll() == 5, "OUT 0FEH sets the VDM's scroll (low nibble)");
        g.out(DSTAT, 0x1A);
        CHECK(g.vdm->scroll() == 0x0A, "only the low four bits are the row");
    }

    SECTION("Sol I/O -- four purpose-named units, each connectable");
    {
        Rig g;
        auto u = g.sol->units();
        CHECK(u.size() == 4, "serial, printer, tape, keyboard");
        bool haveKbd = false, haveSer = false, havePrn = false, haveTape = false;
        for (auto& d : u) {
            haveKbd |= d.name == "keyboard";
            haveSer |= d.name == "serial";
            havePrn |= d.name == "printer";
            haveTape |= d.name == "tape";
        }
        CHECK(haveKbd && haveSer && havePrn && haveTape, "all four are present by name");

        std::string err;
        CHECK(g.sol->connect("printer", "null", err), "CONNECT printer null");
        CHECK(!g.sol->connect("floppy", "null", err), "an unknown unit is refused");

        // The `connect` unit-property round-trips (this is what CONFIG SAVE writes).
        bool sawConnect = false;
        for (Property& p : g.sol->unitProperties("keyboard"))
            if (p.name == "connect") sawConnect = true;
        CHECK(sawConnect, "keyboard exposes a `connect` property");
        CHECK(g.sol->unitProperties("serial").size() >= 2, "serial also exposes baud/data_bits");
    }

    SECTION("Sol-20 -- SOLOS cold-starts, paints its '>' prompt, and rests on the keyboard");
    {
        BootRig b;
        CHECK(!b.screenHas('>'), "before running, the screen is blank");
        CHECK(b.runToPrompt(4'000'000), "SOLOS reaches its prompt and writes '>' to the VDM");

        // The keyboard is a NullStream here (never ready), so SOLOS must SETTLE at the
        // prompt polling for a key -- not loop re-erasing. Run on and confirm the '>'
        // is still there: a resting prompt, not a flicker caught mid-redraw.
        for (int i = 0; i < 500'000; ++i) {
            StepResult s = b.m.master()->step(b.m.bus);
            b.m.clock.advance(s.tStates);
        }
        CHECK(b.screenHas('>'), "the prompt is still there -- SOLOS is resting, not looping");
    }
}
