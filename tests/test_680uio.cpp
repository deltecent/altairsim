#include "test.h"

#include "boards/mits-680uio.h"
#include "boards/s100-memory.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <string>

using namespace altair;

namespace {

// A machine with the 680b Universal I/O in it, its serial line and its first PIA
// section (p1a) each wired to a scripted stream through the REAL connect path. A
// `memory` board carries a scrap of RAM low down; it does not decode the F0xx
// page, so the 680uio is the only thing that answers there.
struct Rig {
    Machine         m;
    Uio680Board*    uio = nullptr;
    ScriptedStream* ser = nullptr;   // the serial 'serial' line
    ScriptedStream* par = nullptr;   // the PIA section 'p1a'

    Rig() {
        std::string err;
        m.bus.setVerify(true);

        MemoryBoard* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region       ram;
        ram.kind = RegionKind::Ram;
        ram.at   = 0x0000;
        ram.size = 0x400;  // 1K -- well clear of the F0xx window
        mem->addRegion(ram, err);

        uio = dynamic_cast<Uio680Board*>(m.add("680uio", "uio0", err));
        uio->connect("serial", "scripted", err);
        uio->connect("p1a", "scripted", err);
        ser = dynamic_cast<ScriptedStream*>(uio->unitStream("serial"));
        par = dynamic_cast<ScriptedStream*>(uio->unitStream("p1a"));

        m.add("6800", "cpu0", err);
        m.power();
    }
};

bool setProp(Uio680Board& b, const char* name, const char* val) {
    std::string err;
    return setProperty(b, name, val, err);
}

int unitCount(const Uio680Board& b) { return (int)b.units().size(); }

} // namespace

void test_680uio() {
    SECTION("680uio -- the default S9 window decode (base F000)");
    {
        Rig g;
        BusCycle c;
        c.type = Cycle::MemRead;

        c.addr = 0xF006;
        CHECK(g.uio->decodes(c), "F006 (serial control/status) is ours");
        c.addr = 0xF007;
        CHECK(g.uio->decodes(c), "F007 (serial Rx/Tx data) is ours");
        c.addr = 0xF008;
        CHECK(g.uio->decodes(c), "F008 (PIA-C section A) is ours");
        c.addr = 0xF00B;
        CHECK(g.uio->decodes(c), "F00B (PIA-C section B data) is ours");
        c.addr = 0xF003;
        CHECK(g.uio->decodes(c), "F003 (switch inputs) is ours on a READ");
        c.addr = 0xF010;
        CHECK(g.uio->decodes(c), "F010 (non-latched output) is ours by default");
        c.addr = 0xF013;
        CHECK(g.uio->decodes(c), "F013 (non-latched output Drive 2) is ours");

        // Not ours: the onboard console window, window offsets 0-5, and -- with one
        // PIA populated -- the second PIA block.
        c.addr = 0xF000;
        CHECK(!g.uio->decodes(c), "F000 belongs to the onboard console, not the UI/O");
        c.addr = 0xF005;
        CHECK(!g.uio->decodes(c), "F005 (window offset 5) is not decoded");
        c.addr = 0xF00C;
        CHECK(!g.uio->decodes(c), "F00C (PIA-B) is NOT ours with a single PIA populated");

        // F003 is a read-only tri-state; a write there is not ours.
        c.type = Cycle::MemWrite;
        c.addr = 0xF003;
        CHECK(!g.uio->decodes(c), "F003 does not decode a write -- the switches are read-only");
    }

    SECTION("680uio -- the serial 6850 transmits and receives (F006/F007)");
    {
        Rig g;

        // Program the ACIA: master reset (3), then divide-16 8N2 (D1), as a monitor would.
        g.m.bus.memWrite(0xF006, 0x03);
        g.m.bus.memWrite(0xF006, 0xD1);

        g.m.bus.memWrite(0xF007, 'A');
        g.uio->pump();
        CHECK(g.ser->out().find('A') != std::string::npos, "a byte written to F007 reaches the wire");

        g.ser->feed("Z");
        g.uio->pump();
        CHECK((g.m.bus.memRead(0xF006) & 0x01) != 0, "RDRF (status bit 0) sets when a byte arrives");
        CHECK(g.m.bus.memRead(0xF007) == 'Z', "F007 hands the guest the received byte");
        CHECK((g.m.bus.memRead(0xF006) & 0x01) == 0, "reading the data clears RDRF");
    }

    SECTION("680uio -- the 6820 PIA-C parallel port (F008/F009)");
    {
        Rig g;

        // Select the data register (control bit 2 = 1), then drive a byte out on
        // section A -- it appears on the connected line immediately.
        g.m.bus.memWrite(0xF008, 0x04);
        g.m.bus.memWrite(0xF009, 'P');
        CHECK(g.par->out().find('P') != std::string::npos, "a byte written to F009 (data) drives the line");

        // Receive: a byte on the line latches, raises status bit 7, and comes back at
        // the data address, clearing the flag.
        g.par->feed("Q");
        g.uio->pump();
        CHECK((g.m.bus.memRead(0xF008) & 0x80) != 0, "status bit 7 sets when a byte is latched");
        CHECK(g.m.bus.memRead(0xF009) == 'Q', "F009 (data) hands over the latched byte");
        CHECK((g.m.bus.memRead(0xF008) & 0x80) == 0, "reading the data register clears the flag");

        // With control bit 2 = 0 the data address reaches the DDR, not the data reg.
        g.m.bus.memWrite(0xF008, 0x00);   // clear DDR-select
        g.m.bus.memWrite(0xF009, 0xFF);   // program all lines as outputs
        CHECK(g.m.bus.memRead(0xF009) == 0xFF, "with bit 2 = 0 the data address is the DDR");
    }

    SECTION("680uio -- the fixed switch inputs at F003");
    {
        Rig g;
        CHECK(g.m.bus.memRead(0xF003) == 0x00, "default sense is 0x00");
        CHECK(setProp(*g.uio, "sense", "5A"), "sense is a settable board strap");
        CHECK(g.m.bus.memRead(0xF003) == 0x5A, "and F003 reads it straight back");

        // Read-only: a store to F003 changes nothing.
        g.m.bus.memWrite(0xF003, 0x00);
        CHECK(g.m.bus.memRead(0xF003) == 0x5A, "a write to F003 is ignored -- switch inputs");
    }

    SECTION("680uio -- the non-latched output and the KCACR-collision jumper (nlout)");
    {
        Rig g;
        g.m.bus.memWrite(0xF011, 0xC3);   // drive 1 data
        CHECK(g.m.bus.memRead(0xF011) == 0xC3, "the last byte driven on Drive 1 reads back at F011");

        // `nlout` off models "remove IC A1" so a KCACR can own F010/F011.
        CHECK(setProp(*g.uio, "nlout", "off"), "nlout can be turned off");
        BusCycle c;
        c.type = Cycle::MemRead;
        c.addr = 0xF010;
        CHECK(!g.uio->decodes(c), "with nlout off, F010 is no longer decoded (KCACR owns it)");
        c.addr = 0xF008;
        CHECK(g.uio->decodes(c), "but the PIA still answers -- only the output decode moved");
    }

    SECTION("680uio -- S9 relocates the serial+PIA window, not the fixed addresses");
    {
        Rig g;
        CHECK(setProp(*g.uio, "base", "F020"), "the S9 base moves to F020");

        BusCycle c;
        c.type = Cycle::MemRead;
        c.addr = 0xF026;
        CHECK(g.uio->decodes(c), "serial now answers at F026");
        c.addr = 0xF028;
        CHECK(g.uio->decodes(c), "PIA-C now answers at F028");
        c.addr = 0xF006;
        CHECK(!g.uio->decodes(c), "and nothing at the old F006");
        c.addr = 0xF003;
        CHECK(g.uio->decodes(c), "but F003 (switch inputs) does not move");
        c.addr = 0xF010;
        CHECK(g.uio->decodes(c), "and neither does the F010 output");

        // A window that is not a multiple of 16 in the F0 page is refused.
        CHECK(!setProp(*g.uio, "base", "F008"), "a non-16-aligned base is rejected");
    }

    SECTION("680uio -- a second PIA (pias=2) lights up PIA-B and its units");
    {
        Rig g;
        CHECK(unitCount(*g.uio) == 3, "with one PIA: serial + p1a + p1b");

        CHECK(setProp(*g.uio, "pias", "2"), "populate the second 6820");
        CHECK(unitCount(*g.uio) == 5, "with two PIAs: serial + p1a/p1b + p2a/p2b");

        BusCycle c;
        c.type = Cycle::MemRead;
        c.addr = 0xF00C;
        CHECK(g.uio->decodes(c), "F00C (PIA-B) now decodes");
        c.addr = 0xF00F;
        CHECK(g.uio->decodes(c), "through F00F");
    }

    SECTION("680uio -- interrupts: idle quiet, a PIA C1 interrupt reaches the 6800");
    {
        Rig g;
        CHECK(!g.uio->assertsInt(), "freshly powered, nothing is asking for an interrupt");

        // Enable the section-A C1 interrupt (control bit 0), then a received byte
        // raises the request (reference 6).
        g.m.bus.memWrite(0xF008, 0x05);   // bit2 (data select) + bit0 (C1 int enable)
        g.par->feed("!");
        g.uio->pump();
        CHECK(g.uio->assertsInt(), "a latched byte with C1 int enabled pulls IRQ");
        g.m.bus.memRead(0xF009);          // reading the data clears the flag
        CHECK(!g.uio->assertsInt(), "and reading the data register drops the request");
    }

    SECTION("680uio -- snapshot carries the live chip and strap state");
    {
        Rig g;
        setProp(*g.uio, "sense", "3C");
        g.m.bus.memWrite(0xF011, 0x99);   // a driven output byte
        g.par->feed("K");                 // a latched PIA input
        g.uio->pump();

        StateWriter w;
        g.uio->serialize(w);

        Uio680Board fresh;
        StateReader rd(w.data());
        fresh.deserialize(rd);

        // The strap and the driven byte survive; F003/F011 read them back.
        BusCycle c;
        c.type = Cycle::MemRead;
        c.addr = 0xF003;
        CHECK(fresh.read(c) == 0x3C, "the sense strap survives serialize/deserialize");
        c.addr = 0xF011;
        CHECK(fresh.read(c) == 0x99, "the last non-latched output byte survives too");
        // The latched PIA input survives: status bit 7 set, data reads 'K'.
        c.addr = 0xF008;
        CHECK((fresh.read(c) & 0x80) != 0, "the PIA input-full flag survives");
    }
}
