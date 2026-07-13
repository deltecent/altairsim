#include "test.h"

#include "boards/mits-88cpu.h"
#include "boards/mits-frontpanel.h"
#include "boards/s100-memory.h"
#include "config/toml.h"
#include "core/machine.h"

#include <string>

using namespace altair;

namespace {

// A machine with a front panel in it. By hand, not from a .toml, so a config bug
// cannot make these go red -- and then ONE test at the bottom goes the other way and
// checks the .toml, on purpose.
struct Rig {
    Machine          m;
    FrontPanelBoard* fp  = nullptr;
    MemoryBoard*     mem = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);  // re-derive the decode and the int wire every cycle

        mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region r;
        r.kind = RegionKind::Ram;
        r.at   = 0;
        r.size = 0x10000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);

        fp = dynamic_cast<FrontPanelBoard*>(m.add("fp", "fp0", err));
        m.add("8080", "cpu0", err);
        m.power();
    }

    void run(int steps) {
        for (int i = 0; i < steps; ++i) {
            StepResult s = m.master()->step(m.bus);
            m.clock.advance(s.tStates);
        }
    }

    void load(std::initializer_list<uint8_t> code, uint16_t at = 0) {
        uint16_t a = at;
        for (uint8_t b : code) m.bus.memWrite(a++, b);
        m.cpu()->setPc(at);
    }

    // The accumulator, read through the reflection layer -- the same way REG and the
    // MCP server read it, so this test cannot see a register the debugger cannot.
    uint32_t reg(const std::string& name) {
        for (const RegDef& rd : m.cpu()->registers())
            if (rd.name == name) return rd.get();
        return 0xFFFFFFFF;
    }
};

// Through the reflection layer, the way SHOW reads it -- not by reaching into the
// board. If `sense` is not gettable, that is a bug in the card.
std::string prop(Board& b, const std::string& name) {
    for (Property& p : b.properties())
        if (p.name == name) return p.get().text(p.radix);
    return "(no such property)";
}

} // namespace

void test_frontpanel() {
    SECTION("the front panel -- the SENSE switches, and the lamps");
    {
        Rig r;
        std::string err;

        // ---- The port. 0xFF, read, and nothing else. ----
        r.fp->setSense(0xA5);
        CHECK(r.m.bus.ioRead(0xFF) == 0xA5, "IN 0FFH reads the SENSE switches");

        CHECK(setProperty(*r.fp, "sense", "5A", err), "SET fp0 SENSE=5A -- and it is HEX");
        CHECK(r.m.bus.ioRead(0xFF) == 0x5A, "...and the guest sees the new setting");
        CHECK(prop(*r.fp, "sense") == "0x5A", "...and SHOW agrees with the guest");

        // THE SWITCHES ARE THE TOP HALF OF ONE ROW OF SIXTEEN. Not a separate
        // register -- SA8..SA15 -- which is why setting one does not disturb the
        // other and why there is only one place either can be wrong.
        setProperty(*r.fp, "data", "3C", err);
        CHECK(r.fp->switches() == 0x5A3C, "sense is the HIGH byte of the switch row");
        CHECK(r.m.bus.ioRead(0xFF) == 0x5A, "...and the DATA switches are not on the port");

        // ---- OUT 0FFH IS NOT OURS. The buffer enable is gated with sINP; there is
        // no sOUT anywhere near it (schematic 880-106). The byte is discarded by the
        // backplane, and the switches do not move -- which is what a toggle does when
        // you write to it, i.e. nothing.
        r.m.bus.ioWrite(0xFF, 0x00);
        CHECK(r.m.bus.lastUnclaimed(), "OUT 0FFH is unclaimed -- the panel does not latch it");
        CHECK(r.m.bus.ioRead(0xFF) == 0x5A, "...and it did not move a switch");
    }

    {
        // ---- THE REGRESSION THAT MOTIVATED THE WHOLE CARD. ----
        //
        // With no panel in the machine, port FF must still FLOAT. `Machine::sense`
        // used to be a byte that nothing put on the bus, so this was the ONLY
        // behavior the guest ever saw -- 0xFF, whatever the config said. An empty
        // backplane is still allowed to be empty; what is not allowed is a machine
        // that has no panel and pretends to.
        Machine m;
        std::string err;
        m.bus.setVerify(true);
        m.add("memory", "mem0", err);
        m.power();

        CHECK(m.bus.ioRead(0xFF) == 0xFF, "no panel -> port FF floats, and that is honest");
        CHECK(m.bus.lastUnclaimed(), "...because nobody drove it");
    }

    {
        // ---- A TOGGLE IS A TOGGLE. Neither reset is a finger. ----
        Rig r;
        r.fp->setSense(0x3C);

        r.m.reset(Reset::Bus);  // the RESET button on the panel
        CHECK(r.fp->sense() == 0x3C, "RESET does not move a switch");

        r.m.power();  // POC*, the thing that loses RAM
        CHECK(r.fp->sense() == 0x3C, "and neither does POWER -- nothing moves a toggle but a hand");

        // ...but there is no light without power.
        CHECK(r.fp->addressLamps() == 0, "POWER puts the address lamps out");
        CHECK(r.fp->dataLamps() == 0, "...and the data lamps");
        CHECK(r.fp->statusLamps() == 0, "...and the status lamps");
    }

    {
        // ---- THE LAMPS ARE WIRED TO THE BACKPLANE. ----
        //
        // They show the LAST CYCLE THAT WENT BY, including cycles this card had
        // nothing to do with -- which is the entire reason it snoops. The Operator's
        // Manual is candid about what that looks like at speed: "While running a
        // program, however, LEDs may appear to give erroneous indications."
        Rig r;

        r.m.bus.memWrite(0x1234, 0x99);
        CHECK(r.fp->addressLamps() == 0x1234, "a memory WRITE lights the address it went to");
        CHECK(r.fp->dataLamps() == 0x99, "...and the byte that went there");
        CHECK(r.fp->statusLamps() == LampWo, "...and WO*, which is lit on a write");

        // A READ lights the data lamps too, and THAT is the part that needed a bus
        // fix: BusCycle::data is 0 while a read is in flight (nobody has driven the
        // bus yet when read() is called), so Bus::settle() back-fills it with the
        // byte that came back before the snoopers see it. Without that, the data
        // lamps would be dark on every read -- three quarters of all cycles.
        uint8_t v = r.m.bus.memRead(0x1234);
        CHECK(v == 0x99, "the byte reads back");
        CHECK(r.fp->addressLamps() == 0x1234, "a memory READ lights the address");
        CHECK(r.fp->dataLamps() == 0x99, "...and the byte that came BACK -- see Bus::settle()");
        CHECK(r.fp->statusLamps() == LampMemR, "...and MEMR");

        // The floating bus is a byte too. It is 0xFF because nothing drove it, and
        // eight LEDs wired to eight pulled-up lines will happily show you that.
        r.m.bus.ioRead(0x42);
        CHECK(r.fp->dataLamps() == 0xFF, "an unclaimed port floats, and the lamps show FF");
        CHECK(r.fp->statusLamps() == LampInp, "...on an INP cycle");

        r.m.bus.ioWrite(0x42, 0x07);
        CHECK(r.fp->dataLamps() == 0x07, "an OUT lights the byte");
        CHECK(r.fp->statusLamps() == (LampOut | LampWo), "...with OUT and WO* together");
    }

    {
        // ---- AND THE GUEST'S OWN IN, WHICH IS THE POINT OF THE CARD. ----
        //
        // This is what DBL does at FF22, reduced to four bytes. It is also the one
        // cycle where the panel is BOTH the card being read and the card watching:
        // the lamps show its own byte, because on the real machine those buffers are
        // driving the very lines the LEDs hang off.
        Rig r;
        r.fp->setSense(0x10);
        r.load({0xDB, 0xFF,   // IN 0FFH
                0xE6, 0x10,   // ANI 10H     -- DBL's stop-bit test
                0x76});       // HLT
        r.run(2);

        CHECK(r.reg("A") == 0x10, "the guest's IN 0FFH lands the switches in A");
        CHECK(r.fp->dataLamps() == 0x10, "and the panel's own byte lit its own data lamps");
    }

    {
        // ---- THE CONFIG. This is the one that goes through the TOML parser. ----
        Machine m;
        std::string err;
        const char* toml =
            "[machine]\n"
            "name = \"t\"\n"
            "[[board]]\n"
            "type  = \"fp\"\n"
            "id    = \"fp0\"\n"
            "sense = 0x12\n";
        CHECK(loadTomlText(toml, "test", m, err), "a panel loads from a [[board]] table");
        Board* b = m.find("fp0");
        CHECK(b != nullptr, "and it is in the backplane");
        if (b) {
            CHECK(prop(*b, "sense") == "0x12", "with the switches set, and read back as HEX");
            CHECK(m.bus.ioRead(0xFF) == 0x12, "...and the guest can read them");
        }
    }

    {
        // ---- THE OLD KEY IS REFUSED, AND IT SAYS WHY. ----
        //
        // `[machine] sense` parsed for months into a byte that nothing put on the
        // bus. A config that LOOKS like it set the switches and did not is worse than
        // one that will not load, so this is an ERROR with a sentence -- the same
        // treatment `clock_hz` got when the crystal moved onto the CPU card.
        Machine m;
        std::string err;
        const char* toml =
            "[machine]\n"
            "name  = \"t\"\n"
            "sense = 0x10\n";
        CHECK(!loadTomlText(toml, "test", m, err), "[machine] sense no longer loads");
        CHECK(err.find("FRONT PANEL") != std::string::npos,
              "...and the error says where the switches went");
        CHECK(err.find("type  = \"fp\"") != std::string::npos,
              "...and hands you the two lines that replace it");
    }
}
