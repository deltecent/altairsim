#include "test.h"

#include "boards/bankmem.h"
#include "core/board.h"
#include "core/machine.h"
#include "core/statefile.h"

using namespace altair;

static MemBankBoard* addBank(Machine& m, const std::string& id, const char* card) {
    std::string err;
    auto* b = dynamic_cast<MemBankBoard*>(m.add("bankmem", id, err));
    std::string e2;
    setProperty(*b, "card", card, e2);
    return b;
}

// Bit i set for each live bank -- see MemBankBoard::activeMask().
static uint32_t active(MemBankBoard* b) { return b->activeMask(); }

void test_bankmem() {
    SECTION("bankmem -- one board, four decoders, each owning its own decode");

    {
        // Vector Graphic: port 40, ONE-HOT select-one. 0x04 -> bank 2, not bank 4.
        Machine m;
        auto* b = addBank(m, "mem0", "vector");
        m.power();
        CHECK(active(b) == 0x01, "vector: reset forces bank 0 live (POC latch clear ~ 0x01)");
        m.bus.ioWrite(0x40, 0x04);
        CHECK(active(b) == 0x04, "vector: OUT 40,04 selects bank TWO (one-hot), nothing else live");
        m.bus.ioWrite(0x40, 0x80);
        CHECK(active(b) == 0x80, "vector: OUT 40,80 selects bank 7");

        // The OASIS quirk: bit 6 is ignored, so 0x41/0x42 land on banks 0/1.
        m.bus.ioWrite(0x40, 0x41);
        CHECK(active(b) == 0x01, "vector: 0x41 -> bank 0 (bit 6 ignored: OASIS)");
        m.bus.ioWrite(0x40, 0x42);
        CHECK(active(b) == 0x02, "vector: 0x42 -> bank 1 (OASIS)");

        // A select the card cannot decode is not silently swallowed.
        m.drainBoardLog();
        m.bus.ioWrite(0x40, 0x03);
        CHECK(active(b) == 0x02, "vector: an undecodable (non-one-hot) select leaves the bank alone");
        CHECK(!m.drainBoardLog().empty(), "vector: ...and SAYS SO");
    }

    {
        // Vector: the plane really swaps -- same address, different bytes.
        Machine m;
        auto* b = addBank(m, "mem0", "vector");
        std::string e2;
        setProperty(*b, "fill", "zero", e2);
        m.power();
        m.bus.ioWrite(0x40, 0x01);          // bank 0
        m.bus.memWrite(0x1000, 0xA0);
        m.bus.ioWrite(0x40, 0x08);          // bank 3
        m.bus.memWrite(0x1000, 0xB3);
        m.bus.ioWrite(0x40, 0x01);
        CHECK(m.bus.memRead(0x1000) == 0xA0, "vector: bank 0 still holds A0");
        m.bus.ioWrite(0x40, 0x08);
        CHECK(m.bus.memRead(0x1000) == 0xB3, "vector: bank 3 holds B3 -- the plane swapped");
        (void)b;
    }

    {
        // Cromemco 64KZ: port 40, 8-bit MASK. Several banks live AT ONCE -- this is the
        // mechanism the SIMH-derived model got wrong (it thought Cromemco was select-one).
        Machine m;
        auto* b = addBank(m, "mem0", "cromemco64kz");
        m.power();
        m.bus.ioWrite(0x40, 0x28);
        CHECK(active(b) == 0x28, "cromemco: OUT 40,28 activates banks 3 AND 5 at once");
        m.bus.ioWrite(0x40, 0x48);
        CHECK(active(b) == 0x48, "cromemco: OUT 40,48 activates banks 3 and 6");
        m.bus.ioWrite(0x40, 0x20);
        CHECK(active(b) == 0x20, "cromemco: OUT 40,20 activates bank 5 only");
        m.bus.ioWrite(0x40, 0x00);
        CHECK(active(b) == 0x00, "cromemco: OUT 40,00 deactivates every bank");

        // Two banks live over the same 64K window is a bus fight, and it is reported.
        m.drainBoardLog();
        m.bus.ioWrite(0x40, 0x28);
        (void)m.bus.memRead(0x1000);
        CHECK(!m.drainBoardLog().empty(),
              "cromemco: overlapping live banks are reported as a bus fight");
    }

    {
        // North Star HRAM: port C0. The byte is NOT a bank number -- bit 0 is an
        // on/off command and bits 1-7 are a one-hot address of the bank to toggle.
        Machine m;
        auto* b = addBank(m, "mem0", "northstar");
        m.power();
        CHECK(b->banks() == 6, "northstar: <=6 usable banks (one of bits 5/6/7 is parity)");
        CHECK(active(b) == 0x01, "northstar: reset leaves the first bank on (JP1 default)");

        // Old off, then new on -- the documented protocol.
        m.bus.ioWrite(0xC0, 0x03);          // bit1 set, bit0=1 -> turn OFF bank at bit1
        CHECK(active(b) == 0x00, "northstar: OUT C0,03 turns the bit-1 bank OFF");
        m.bus.ioWrite(0xC0, 0x04);          // bit2 set, bit0=0 -> turn ON bank at bit2
        CHECK(active(b) == 0x02, "northstar: OUT C0,04 turns the bit-2 bank ON");

        // Skip the "old off" step and two banks are on at once -- the trap the manual warns of.
        m.bus.ioWrite(0xC0, 0x02);          // turn the bit-1 bank on WITHOUT turning bit-2 off
        CHECK(active(b) == 0x03, "northstar: banks toggle individually (both on if you skip old-off)");

        // A byte with no single bank bit, and a bank the board does not carry, both complain.
        m.drainBoardLog();
        m.bus.ioWrite(0xC0, 0x06);          // bits 1 and 2 -> not one-hot
        CHECK(!m.drainBoardLog().empty(), "northstar: a non-one-hot byte is reported");
        m.bus.ioWrite(0xC0, 0x80);          // bit 7 -> a bank this 6-bank board lacks
        CHECK(!m.drainBoardLog().empty(), "northstar: a bank the board does not carry is reported");
    }

    {
        // ExpandoRAM II: port FF, binary page select (our documented approximation of the
        // real 82S130 PROM decode).
        Machine m;
        auto* b = addBank(m, "mem0", "expandoram2");
        std::string e2;
        setProperty(*b, "fill", "zero", e2);
        m.power();
        m.bus.ioWrite(0xFF, 0x00);
        m.bus.memWrite(0x1000, 0xA0);
        m.bus.ioWrite(0xFF, 0x03);
        CHECK(active(b) == 0x08, "expandoram2: OUT FF,03 selects page 3 (binary)");
        m.bus.memWrite(0x1000, 0xB3);
        m.bus.ioWrite(0xFF, 0x00);
        CHECK(m.bus.memRead(0x1000) == 0xA0, "expandoram2: page 0 still holds A0");
        m.bus.ioWrite(0xFF, 0x03);
        CHECK(m.bus.memRead(0x1000) == 0xB3, "expandoram2: page 3 holds B3 -- the page swapped");
    }

    {
        // ExpandoRAM II with a common-memory partition (EX-48): the top 16K is shared by
        // every bank -- what a banked CP/M's resident OS and ?bank routine live in -- and
        // only 0000-BFFF swaps. This is what lets SD Systems banked CP/M 3 (bank number
        // straight to port FF, common at C000) run.
        Machine m;
        auto* b = addBank(m, "mem0", "expandoram2");
        std::string e2;
        setProperty(*b, "partition", "ex48", e2);
        setProperty(*b, "ram", "256", e2);      // 16K common + 5x48K banked
        setProperty(*b, "fill", "zero", e2);
        m.power();
        CHECK(e2.empty(), "eram2/ex48: partition+ram set without error");
        CHECK(b->banks() == 5, "eram2/ex48: 256K -> 5 banks of 48K (plus 16K common)");

        // The banked window swaps between pages...
        m.bus.ioWrite(0xFF, 0x00);
        m.bus.memWrite(0x1000, 0xA0);           // banked region, page 0
        m.bus.ioWrite(0xFF, 0x03);
        m.bus.memWrite(0x1000, 0xB3);           // banked region, page 3
        m.bus.ioWrite(0xFF, 0x00);
        CHECK(m.bus.memRead(0x1000) == 0xA0, "eram2/ex48: page 0 keeps its own banked byte");
        m.bus.ioWrite(0xFF, 0x03);
        CHECK(m.bus.memRead(0x1000) == 0xB3, "eram2/ex48: page 3 keeps its own banked byte");

        // ...but the common region (C000-FFFF) is the SAME bytes in every bank.
        m.bus.ioWrite(0xFF, 0x00);
        m.bus.memWrite(0xE000, 0x5A);           // write common while page 0 is in
        m.bus.ioWrite(0xFF, 0x04);
        CHECK(m.bus.memRead(0xE000) == 0x5A, "eram2/ex48: common survives a bank switch (page 4 sees it)");
        m.bus.memWrite(0xE000, 0x99);           // change it from page 4
        m.bus.ioWrite(0xFF, 0x01);
        CHECK(m.bus.memRead(0xE000) == 0x99, "eram2/ex48: common is one shared region, not per-bank");
    }

    {
        // partition is an ExpandoRAM II concept; the other cards are whole-64K planes.
        Machine m;
        auto* b = addBank(m, "mem0", "vector");
        std::string err;
        bool ok = setProperty(*b, "partition", "ex48", err);
        CHECK(!ok && !err.empty(), "partition is refused on non-expandoram2 cards");

        // ram in KB derives the bank count; whole-plane (none) -> 64K per bank.
        setProperty(*b, "ram", "256", err);
        CHECK(b->banks() == 4, "vector: ram=256K -> 4 whole-64K planes");
    }

    {
        // The select port is per-card, and two banked cards on the same port are a real
        // I/O collision the bus reports by name -- exactly the old vram/b810 test, now
        // between two bankmem boards.
        Machine m;
        auto* a = addBank(m, "mem0", "vector");
        auto* c = addBank(m, "mem1", "cromemco64kz");
        m.power();
        BusCycle cyc{Cycle::IoWrite, 0x40, 0, false};
        CHECK(m.bus.respondersTo(cyc).size() == 2, "two banked cards both claim port 40");
        m.bus.clearLog();
        m.bus.ioWrite(0x40, 0x01);
        CHECK(!m.bus.drain().empty(), "and the I/O collision is REPORTED, naming both");
        (void)a; (void)c;
    }

    {
        // SNAPSHOT / RESTORE: the store and the live enables travel; the geometry is config.
        MemBankBoard src, dst;
        src.id = "mem0";
        dst.id = "mem0";
        std::string e2;
        setProperty(src, "card", "cromemco64kz", e2);
        setProperty(src, "fill", "zero", e2);
        setProperty(dst, "card", "cromemco64kz", e2);
        setProperty(dst, "fill", "zero", e2);
        src.power();
        dst.power();

        src.write(BusCycle{Cycle::IoWrite, 0x40, 0x08, false});   // bank 3 live
        src.write(BusCycle{Cycle::MemWrite, 0x2000, 0x5A, false});
        CHECK(active(&src) == 0x08, "snapshot: source has bank 3 live");

        StateWriter w;
        src.serialize(w);
        StateReader r(w.data());
        dst.deserialize(r);
        CHECK(r.ok(), "snapshot: restore consumed the blob cleanly");
        CHECK(active(&dst) == 0x08, "snapshot: the live bank travelled");
        uint8_t v = 0;
        CHECK(dst.peek(0x2000, v) && v == 0x5A, "snapshot: the stored byte travelled");
    }
}
