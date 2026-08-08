#include "test.h"

#include "boards/mits-680io.h"
#include "boards/s100-memory.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <string>

using namespace altair;

namespace {

// A machine with the 680b onboard I/O in it and a scripted terminal on its one line,
// bound through the REAL connect path so the test drives the same wiring an operator's
// CONNECT does. A `memory` board carries a scrap of RAM low down; it does not decode the
// F00x window, so the 680io is the only thing that answers there.
struct Rig {
    Machine         m;
    Io680Board*     io  = nullptr;
    ScriptedStream* tty = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);

        MemoryBoard* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region       ram;
        ram.kind = RegionKind::Ram;
        ram.at   = 0x0000;
        ram.size = 0x400;  // 1K -- well clear of the F00x window
        mem->addRegion(ram, err);

        io = dynamic_cast<Io680Board*>(m.add("680io", "io0", err));
        io->connect("tty", "scripted", err);
        tty = dynamic_cast<ScriptedStream*>(io->unitStream("tty"));

        m.add("6800", "cpu0", err);
        m.power();
    }
};

int straps(Io680Board& b) {
    for (Property& p : b.properties())
        if (p.name == "straps") return (int)p.get().i();
    return -1;
}

} // namespace

void test_680io() {
    SECTION("680io -- the decode is three fixed addresses in page F0");
    {
        Rig g;
        BusCycle c;

        c.type = Cycle::MemRead;
        c.addr = 0xF000;
        CHECK(g.io->decodes(c), "F000 (ACIA status/control) is ours");
        c.addr = 0xF001;
        CHECK(g.io->decodes(c), "F001 (ACIA Rx/Tx data) is ours");
        c.addr = 0xF002;
        CHECK(g.io->decodes(c), "F002 (config straps) is ours on a READ");
        c.addr = 0xF003;
        CHECK(!g.io->decodes(c), "F003 is NOT ours -- three addresses, not the page");
        c.addr = 0xEFFF;
        CHECK(!g.io->decodes(c), "and nothing just below the window");

        // F002 is a strap buffer -- a tri-state that only enables on a read. A WRITE to it
        // is not ours, so it does not shadow whatever else might answer there.
        c.type = Cycle::MemWrite;
        c.addr = 0xF002;
        CHECK(!g.io->decodes(c), "F002 does NOT decode a write -- the straps are read-only");
        c.addr = 0xF000;
        CHECK(g.io->decodes(c), "but F000 decodes a write (the ACIA control register)");
    }

    SECTION("680io -- the config straps read at F002 (Operator's Manual 3)");
    {
        Rig g;
        CHECK(g.m.bus.memRead(0xF002) == 0x00, "default straps 0x00: terminal present, 2 stop bits");
        CHECK(straps(*g.io) == 0x00, "and the property reports it");

        std::string err;
        CHECK(setProperty(*g.io, "straps", "84", err),
              "bit7 (No-Terminal) + bit2 (1 stop bit) is a legal strap byte");
        CHECK(g.m.bus.memRead(0xF002) == 0x84, "and F002 reads it straight back");

        // Read-only: a store to F002 changes nothing.
        g.m.bus.memWrite(0xF002, 0x00);
        CHECK(g.m.bus.memRead(0xF002) == 0x84, "a write to F002 is ignored -- it is jumper straps");
    }

    SECTION("680io -- F000/F001 route to the onboard 6850");
    {
        Rig g;

        // Program the ACIA the way MON680 does: master reset (3), then divide-16 8N2 (D1).
        g.m.bus.memWrite(0xF000, 0x03);
        g.m.bus.memWrite(0xF000, 0xD1);

        // Transmit: a byte written to F001 (TxData) leaves on the connected line.
        g.m.bus.memWrite(0xF001, 'A');
        g.io->pump();
        CHECK(g.tty->out().find('A') != std::string::npos, "a byte written to F001 reaches the wire");

        // Receive: a byte on the line shows up as RDRF in the F000 status and comes back
        // at F001 (RxData).
        g.tty->feed("Z");
        g.io->pump();
        CHECK((g.m.bus.memRead(0xF000) & 0x01) != 0, "RDRF (status bit 0) sets when a byte arrives");
        CHECK(g.m.bus.memRead(0xF001) == 'Z', "and F001 hands the guest the byte");
        CHECK((g.m.bus.memRead(0xF000) & 0x01) == 0, "reading the data clears RDRF");
    }

    SECTION("680io -- idle, the ACIA is not asking for an interrupt");
    {
        Rig g;
        CHECK(!g.io->assertsInt(), "a freshly powered 6850 with no int-enable is quiet");
    }

    SECTION("680io -- snapshot carries the straps");
    {
        Rig g;
        std::string err;
        setProperty(*g.io, "straps", "84", err);

        StateWriter w;
        g.io->serialize(w);

        Io680Board  fresh;
        StateReader rd(w.data());
        fresh.deserialize(rd);
        CHECK(straps(fresh) == 0x84, "the strap byte survives serialize/deserialize");
    }
}
