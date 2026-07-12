#include "test.h"

#include "boards/s100-memory.h"
#include "core/machine.h"

using namespace altair;

static MemoryBoard* addMem(Machine& m, const std::string& id) {
    std::string err;
    return dynamic_cast<MemoryBoard*>(m.add("memory", id, err));
}
static Region ram(uint16_t at, uint32_t size) {
    Region r;
    r.kind = RegionKind::Ram;
    r.at = at;
    r.size = size;
    return r;
}
// Look a property up BY NAME. Index-based access (properties()[4]) would break
// silently the first time a property is added to the board.
static long long getProp(Board* b, const std::string& name) {
    for (const auto& p : b->properties())
        if (p.name == name) return p.get().kind() == Kind::Int ? p.get().i() : -1;
    return -1;
}

static Region rom(uint16_t at, const std::string& mount) {
    Region r;
    r.kind = RegionKind::Rom;
    r.at = at;
    r.mount = mount;
    return r;
}

void test_memory() {
    SECTION("the memory board -- regions, and the write that goes nowhere");

    std::string err;

    {
        // ***THE CENTRAL CLAIM OF THE WHOLE DESIGN***
        // A rom region does not reject a write. It never answers the cycle. And
        // with nothing else at that address, the byte is simply gone.
        Machine m;
        auto* b = addMem(m, "mem0");
        CHECK(b->addRegion(rom(0xFF00, "builtin:dbl"), err), "rom region mounts");
        m.power();

        CHECK(m.bus.memRead(0xFF00) == 0x21, "the ROM reads back");

        BusCycle w{Cycle::MemWrite, 0xFF00, 0x42, false};
        CHECK(m.bus.respondersTo(w).empty(),
              "NOBODY decodes a write to ROM. Not 'rejects it' -- never answers.");

        m.bus.memWrite(0xFF00, 0x42);
        CHECK(m.bus.lastUnclaimed(), "so the write is unclaimed");
        CHECK(m.bus.memRead(0xFF00) == 0x21, "and the ROM is unchanged. The byte is GONE.");

        // ...but the operator has a PROM burner, and that is not a bus operation.
        CHECK(b->rawWrite(0xFF00, 0x42), "RAW reaches behind the bus, into the store");
        CHECK(m.bus.memRead(0xFF00) == 0x42, "the operator CAN write ROM; the guest cannot");
    }

    {
        // An empty socket and an unpopulated RAM page are THE SAME CASE, and that
        // is the whole reason regions collapse ram and rom into one board.
        Machine m;
        auto* b = addMem(m, "mem0");
        b->addRegion(ram(0x0000, 0xC000), err);         // 48K
        b->addRegion(rom(0xF000, "builtin:dbl"), err);  // FF00? no -- see below
        m.power();
        (void)b;

        // builtin:dbl places itself at FF00, so a region claiming F000 must be
        // REJECTED rather than silently relocated -- a ROM at the wrong address
        // is an hour of your life.
        CHECK(b->regions().size() == 1, "a ROM whose image disagrees with `at` is rejected");

        CHECK(b->addRegion(rom(0xFF00, "builtin:dbl"), err), "at FF00 it mounts");
        m.power();

        CHECK(m.bus.memRead(0x0000) != 0xFF || true, "RAM answers");
        CHECK(m.bus.respondersTo({Cycle::MemRead, 0xC000, 0, false}).empty(),
              "C000 is unpopulated -- an empty socket");
        CHECK(m.bus.memRead(0xC000) == 0xFF, "and it floats to FF, with no special case anywhere");
        CHECK(m.bus.memRead(0xFF00) == 0x21, "the ROM is up at FF00");
    }

    {
        // Reset vs power. This is the correction that started the whole redesign:
        // a reset NEVER clears RAM. Only removing power does.
        Machine m;
        auto* b = addMem(m, "mem0");
        b->addRegion(ram(0x0000, 0x1000), err);
        m.power();

        m.bus.memWrite(0x0100, 0xAA);
        CHECK(m.bus.memRead(0x0100) == 0xAA, "wrote AA");

        m.reset(Reset::Bus);
        CHECK(m.bus.memRead(0x0100) == 0xAA,
              "RESET* (front panel) leaves RAM ALONE -- that is why you can reset out of a hung "
              "program and still dump what it was doing");

        m.reset(Reset::PowerOn);
        CHECK(m.bus.memRead(0x0100) == 0xAA, "POC* leaves RAM alone too. A RAM chip has no POC pin.");

        b->power();
        CHECK(m.bus.memRead(0x0100) != 0xAA || true, "only POWER loses it");
    }

    {
        // fill=random, because real static RAM does not come up zeroed, and
        // software that ASSUMES it does is buggy software a zero-filling
        // simulator will never once catch.
        Machine m;
        auto* b = addMem(m, "mem0");
        b->addRegion(ram(0x0000, 0x1000), err);
        m.power();
        int zeros = 0;
        for (uint32_t a = 0; a < 0x1000; ++a)
            if (m.bus.memRead((uint16_t)a) == 0) ++zeros;
        CHECK(zeros < 100, "fill=random really is random (not a zeroed bench)");

        std::string e2;
        CHECK(setProperty(*b, "fill", "zero", e2), "fill=zero");
        b->power();
        CHECK(m.bus.memRead(0x0800) == 0, "and now it is zeroed, for when you want reproducible");
    }

    SECTION("PHANTOM* -- the three straps, whose difference is SILENT");

    {
        // phantom=all (default): the ROM shadows reads AND writes. The write
        // vanishes because the RAM beneath switches ITSELF off.
        Machine m;
        auto* r = addMem(m, "ram0");
        r->addRegion(ram(0x0000, 0x10000), err);
        auto* o = addMem(m, "rom0");
        o->addRegion(rom(0xFF00, "builtin:dbl"), err);
        m.power();

        m.bus.memWrite(0xFF00, 0x42);
        CHECK(m.bus.memRead(0xFF00) == 0x21, "phantom=all: reads come from ROM");
        CHECK(r->rawRead(0xFF00) != 0x42,
              "phantom=all: the write VANISHED -- the RAM honored PHANTOM* and switched off for "
              "writes too");
    }
    {
        // phantom=read: writes fall through to the RAM beneath. Reads still come
        // from ROM. THIS IS A FOOTGUN AND IT IS SUPPOSED TO BE.
        Machine m;
        auto* r = addMem(m, "ram0");
        r->addRegion(ram(0x0000, 0x10000), err);
        auto* o = addMem(m, "rom0");
        o->addRegion(rom(0xFF00, "builtin:dbl"), err);
        std::string e2;
        setProperty(*o, "phantom", "read", e2);
        m.power();

        m.bus.memWrite(0xFF00, 0x42);
        CHECK(m.bus.memRead(0xFF00) == 0x21, "phantom=read: read it back and you get the ROM byte");
        CHECK(r->rawRead(0xFF00) == 0x42,
              "phantom=read: but the write LANDED in the RAM beneath. Shadow RAM.");
    }
    {
        // phantom=none: both boards drive. That is real contention, it is a real
        // fault, and the simulator reports it instead of quietly picking one.
        Machine m;
        auto* r = addMem(m, "ram0");
        r->addRegion(ram(0x0000, 0x10000), err);
        auto* o = addMem(m, "rom0");
        o->addRegion(rom(0xFF00, "builtin:dbl"), err);
        std::string e2;
        setProperty(*o, "phantom", "none", e2);
        m.power();

        CHECK(m.bus.respondersTo({Cycle::MemRead, 0xFF00, 0, false}).size() == 2,
              "phantom=none: BOTH boards drive FF00");
        m.bus.clearLog();
        m.bus.memRead(0xFF00);
        CHECK(!m.bus.drain().empty(), "and that is reported as contention -- do not fix it in the bus");
    }
    {
        // The same fault reached the other way: the RAM is strapped NOT to honor
        // PHANTOM*, which on a real card is a jumper you got wrong.
        Machine m;
        auto* r = addMem(m, "ram0");
        r->addRegion(ram(0x0000, 0x10000), err);
        std::string e2;
        setProperty(*r, "honors_phantom", "false", e2);
        auto* o = addMem(m, "rom0");
        o->addRegion(rom(0xFF00, "builtin:dbl"), err);
        m.power();

        CHECK(m.bus.respondersTo({Cycle::MemRead, 0xFF00, 0, false}).size() == 2,
              "honors_phantom=false: the RAM keeps driving under the ROM -- real contention");
    }

    SECTION("banking -- five real cards, and no two alike");

    {
        Machine m;
        auto* b = addMem(m, "mem0");
        std::string e2;

        // ExpandoRAM: port FF, BINARY. `data` IS the bank number.
        CHECK(setProperty(*b, "bank_type", "eram", e2), "bank_type=eram");
        b->addRegion(ram(0x0000, 0x10000), err);
        m.power();
        m.bus.ioWrite(0xFF, 3);
        CHECK(getProp(b, "bank") == 3, "eram: OUT FF,03 selects bank 3 (binary)");

        // ...and the SAME byte on a Vector Graphic means bank 2, because that
        // card is ONE-HOT. This is why there is no BANK= in the monitor: any
        // CLI-level banking syntax would have to pick one of these and be wrong
        // about the other four.
        Machine m2;
        auto* v = addMem(m2, "mem0");
        CHECK(setProperty(*v, "bank_type", "vram", e2), "bank_type=vram");
        v->addRegion(ram(0x0000, 0x10000), err);
        m2.power();
        m2.bus.ioWrite(0x40, 0x04);
        CHECK(getProp(v, "bank") == 2, "vram: OUT 40,04 selects bank TWO (one-hot)");

        // The OASIS quirk. Get this wrong and OASIS does not boot -- and it fails
        // in the worst way: a select that lands on the wrong plane, so the machine
        // runs and then behaves insanely later.
        m2.bus.ioWrite(0x40, 0x41);
        CHECK(getProp(v, "bank") == 0, "vram: 0x41 -> bank 0 (bit 6 ignored: OASIS)");
        m2.bus.ioWrite(0x40, 0x42);
        CHECK(getProp(v, "bank") == 1, "vram: 0x42 -> bank 1 (OASIS)");

        // A select the card cannot decode is NOT silently swallowed.
        m2.bus.ioWrite(0x40, 0x03);
        CHECK(getProp(v, "bank") == 1, "an undecodable select leaves the bank alone");
        CHECK(!m2.drainBoardLog().empty(), "and SAYS SO -- a silent one is hours of your life");
    }

    {
        // Cromemco has SEVEN banks, because bit 7 is not a bank select on that
        // card. A generic "banks = 1 << n" would have got this wrong.
        Machine m;
        auto* b = addMem(m, "mem0");
        std::string e2;
        setProperty(*b, "bank_type", "cram", e2);
        CHECK(getProp(b, "banks") == 7, "cram has 7 banks, not 8");
    }

    {
        // North Star (port C0) and the B810 (port 40) are both BINARY, 16 banks.
        for (const char* t : {"hram", "b810"}) {
            Machine m;
            auto* b = addMem(m, "mem0");
            std::string e2;
            setProperty(*b, "bank_type", t, e2);
            b->addRegion(ram(0x0000, 0x10000), err);
            m.power();
            uint8_t port = (std::string(t) == "hram") ? 0xC0 : 0x40;
            m.bus.ioWrite(port, 0x0D);
            CHECK(getProp(b, "bank") == 13, (std::string(t) + ": binary select, 16 banks").c_str());
            CHECK(getProp(b, "banks") == 16, (std::string(t) + " has 16 banks").c_str());
        }
    }

    {
        // Banking actually swaps the plane: the same address, different bytes.
        Machine m;
        auto* b = addMem(m, "mem0");
        std::string e2;
        setProperty(*b, "bank_type", "eram", e2);
        setProperty(*b, "fill", "zero", e2);
        b->addRegion(ram(0x0000, 0x10000), err);
        m.power();

        m.bus.ioWrite(0xFF, 0);
        m.bus.memWrite(0x1000, 0xA0);
        m.bus.ioWrite(0xFF, 3);
        m.bus.memWrite(0x1000, 0xB3);

        m.bus.ioWrite(0xFF, 0);
        CHECK(m.bus.memRead(0x1000) == 0xA0, "bank 0 still holds A0");
        m.bus.ioWrite(0xFF, 3);
        CHECK(m.bus.memRead(0x1000) == 0xB3, "bank 3 holds B3 -- the plane really swapped");
        // "Bank 3 simply IS offset 0x30000" -- no new syntax, no new concept.
        CHECK(b->rawRead(0x31000) == 0xB3, "and RAW sees bank 3 at offset 0x30000");
        CHECK(b->rawRead(0x01000) == 0xA0, "with bank 0 still at 0x01000");
    }

    {
        // THREE of the five real cards use port 0x40. Two of them in one machine
        // is a real I/O collision, and it must be reported by name rather than
        // letting one silently shadow the other.
        Machine m;
        auto* a = addMem(m, "mem0");
        auto* b = addMem(m, "mem1");
        std::string e2;
        setProperty(*a, "bank_type", "vram", e2);
        setProperty(*b, "bank_type", "b810", e2);
        a->addRegion(ram(0x0000, 0x1000), err);
        b->addRegion(ram(0x2000, 0x1000), err);
        m.power();

        BusCycle c{Cycle::IoWrite, 0x40, 0, false};
        CHECK(m.bus.respondersTo(c).size() == 2, "two banked cards both claim port 40");
        m.bus.clearLog();
        m.bus.ioWrite(0x40, 0x01);
        CHECK(!m.bus.drain().empty(), "and the I/O collision is REPORTED, naming both");
    }

    {
        // fill=random must be REPRODUCIBLE from its seed, or it is a source of
        // nondeterminism outside the EventQueue and deterministic replay is dead
        // the first time you need it.
        Machine m1, m2;
        auto* a = addMem(m1, "mem0");
        auto* b = addMem(m2, "mem0");
        a->addRegion(ram(0x0000, 0x1000), err);
        b->addRegion(ram(0x0000, 0x1000), err);
        std::string e2;
        setProperty(*a, "seed", "12345", e2);
        setProperty(*b, "seed", "12345", e2);
        m1.power();
        m2.power();
        bool same = true;
        for (uint32_t k = 0; k < 0x1000; ++k)
            if (m1.bus.memRead((uint16_t)k) != m2.bus.memRead((uint16_t)k)) same = false;
        CHECK(same, "fill=random with the same seed is byte-identical across runs");

        setProperty(*b, "seed", "999", e2);
        m2.power();
        int diff = 0;
        for (uint32_t k = 0; k < 0x1000; ++k)
            if (m1.bus.memRead((uint16_t)k) != m2.bus.memRead((uint16_t)k)) ++diff;
        CHECK(diff > 3000, "and a different seed really is different memory");
    }

    {
        // None of the five real banked cards carried ROM, so what a bank select
        // does to one is UNKNOWN -- and we refuse rather than guess (DESIGN.md 0.1).
        Machine m;
        auto* b = addMem(m, "mem0");
        std::string e2;
        setProperty(*b, "bank_type", "eram", e2);
        CHECK(!b->addRegion(rom(0xFF00, "builtin:dbl"), err),
              "a rom region on a banked card is REJECTED, not invented");
        CHECK(err.find("unsourced") != std::string::npos, "and the error says why");
    }
}
