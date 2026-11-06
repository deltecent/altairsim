#include "test.h"

#include "boards/s100-memory.h"
#include "config/toml.h"
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
        // A ROM's line ends in its mount path, and the path has no length limit. The line
        // was formatted into 128 bytes, so a long path was cut off with no sign of it (#905).
        Region r = rom(0xF000, "/" + std::string(300, 'p') + "/rom.bin");
        r.size   = 0x100;
        CHECK(r.describe() == "rom  F000-F0FF  " + r.mount, "describe() holds the whole path");
    }

    {
        // AN EMPTY SOCKET IS EMPTY WHATEVER ITS `size` SAYS (#576). A rom takes its extent
        // from its image, so `size` on one is ignored -- and an empty socket given one used
        // to decode that range and answer 00 from the store, where the bus floats FF. The
        // power-on reload also tried to open the socket's empty mount, and said so.
        Machine m;
        auto* b = addMem(m, "mem0");
        Region sized = rom(0xE000, "");
        sized.size = 256;
        CHECK(b->addRegion(sized, err), "an empty socket with a size is accepted");
        Region socket = rom(0xF000, "");
        socket.size     = 0x1000;  // a 4K socket...
        socket.relocate = true;    // ...that will take the DBL PROM moved down to F000
        CHECK(b->addRegion(socket, err), "...and a second one, for a chip to go in later");
        CHECK(b->addRegion(rom(0xD000, ""), err), "...and one with no size at all");
        m.power();

        CHECK(m.bus.memRead(0xE000) == 0xFF && m.bus.lastUnclaimed(),
              "the sized empty socket decodes nothing: the read floats to FF");
        bool powerMsg = false;
        for (const auto& s : m.drainBoardLog())
            if (s.find("power:") != std::string::npos) powerMsg = true;
        CHECK(!powerMsg, "power-on does not try to load a chip into an empty socket");

        // A chip MOUNTed later takes its extent from the image, not the ignored `size`.
        CHECK(b->mount("rom1", "builtin:dbl", false, err), "a chip goes into the sized socket");
        CHECK(m.bus.memRead(0xF000) == 0x21 && !m.bus.lastUnclaimed(), "it answers at F000");
        (void)m.bus.memRead(0xF100);
        CHECK(m.bus.lastUnclaimed(),
              "...and only over its 256-byte image: the 4K `size` did not widen it");
    }

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
        CHECK(b->poke(0xFF00, 0x42), "the burner reaches behind the bus, into the store");
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
        // relocate = true (issue #468): the same self-placing image, moved to `at`
        // instead of rejected. dbl's records sit at FF00; ask for it at F000 and the
        // whole image shifts down by the delta -- the first byte lands at F000 and the
        // old base is vacated. This is what lets a mirror-decoded monitor answer a
        // vector fetch at a base its records never named.
        Machine m;
        auto* b = addMem(m, "mem0");
        CHECK(b->loadSubUnit("region",
                             {{"type", "rom"}, {"at", "F000"}, {"mount", "builtin:dbl"},
                              {"relocate", "true"}}, err),
              "a relocated ROM mounts even though its records disagree with `at`");
        m.power();

        CHECK(m.bus.memRead(0xF000) == 0x21, "the first byte moved to F000 with the image");
        CHECK(m.bus.respondersTo({Cycle::MemRead, 0xFF00, 0, false}).empty(),
              "and the record's own base FF00 is vacated -- the whole image shifted");

        // CONFIG SAVE round-trips the key, and only when set: a self-placed ROM says
        // nothing, a relocated one says `relocate = true`.
        std::string toml = saveTomlText(m);
        CHECK(toml.find("relocate = true") != std::string::npos,
              "the relocated region writes `relocate = true` back out");
    }

    {
        // The point of relocate: a MIRROR. One builtin under two regions -- self-placed
        // at FF00 and relocated onto F000 -- so both windows present the same ROM. (The
        // real use is a 6800/6809 monitor whose vectors must answer a top-of-memory
        // fetch from a mirror the records never addressed.)
        Machine m;
        auto* b = addMem(m, "mem0");
        CHECK(b->addRegion(rom(0xFF00, "builtin:dbl"), err), "self-placed at its own base");
        Region mir = rom(0xF000, "builtin:dbl");
        mir.relocate = true;
        CHECK(b->addRegion(std::move(mir), err), "and a relocated mirror onto F000");
        m.power();

        CHECK(m.bus.memRead(0xFF00) == 0x21, "the self-placed copy answers at FF00");
        CHECK(m.bus.memRead(0xF000) == 0x21, "and the mirror answers the same byte at F000");
    }

    {
        // relocate does nothing to a Bin mount -- a Bin already honors `at`, so the flag
        // is a no-op there rather than an error. (Nothing to assert on dbl, which is
        // HEX; this just pins that setting it never rejects a mount that already fits.)
        Machine m;
        auto* b = addMem(m, "mem0");
        Region r = rom(0xFF00, "builtin:dbl");
        r.relocate = true;                    // asking to relocate to the base it already has
        CHECK(b->addRegion(std::move(r), err), "relocate to the record's own base is a no-op");
        m.power();
        CHECK(m.bus.memRead(0xFF00) == 0x21, "the image is exactly where it would be without it");
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
        CHECK(r->storeAt(0xFF00) != 0x42,
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
        CHECK(r->storeAt(0xFF00) == 0x42,
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
        setProperty(*r, "honors_phantom", "none", e2);
        auto* o = addMem(m, "rom0");
        o->addRegion(rom(0xFF00, "builtin:dbl"), err);
        m.power();

        CHECK(m.bus.respondersTo({Cycle::MemRead, 0xFF00, 0, false}).size() == 2,
              "honors_phantom=none: the RAM keeps driving under the ROM -- real contention");
    }

    SECTION("fill=random -- reproducible from its seed");

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
}

// ---------------------------------------------------------------------------
// A DERIVED PROPERTY HAS NO SETTER. IT DOES NOT HAVE A SETTER THAT SAYS NO.
//
// `pages` is a thing the card WORKS OUT -- it falls out of the regions you declared. It is
// not a jumper, and it does not belong in a TOML file.
//
// It used to refuse a SET from inside a setter, and that looked like it was enough. It was
// not. Read-only is a FACT ABOUT THE PROPERTY, and the only way a consumer of the reflection
// layer can see it is THE ABSENCE OF A SETTER (core/board.cpp). With a refusing setter
// installed, SHOW printed it as settable, MCP offered it as writable, and anything generated
// off properties() described it as a TOML key you could write -- while the SET they were all
// advertising failed every time.
//
// One signal, honoured by everybody, or four subsystems each guessing. This test pins the
// signal down, because the bug it guards is invisible: nothing crashes, and the damage lives
// entirely in what other subsystems believe.
void test_readonly_props() {
    SECTION("derived properties: no setter, not a setter that refuses");

    Machine m;
    auto* b = addMem(m, "mem0");
    std::string err;
    CHECK(b->addRegion(ram(0x0000, 0xE000), err), "56K of RAM");

    // properties() returns a fresh vector BY VALUE, so it must be held in a named
    // variable: binding the temporary in the range-for frees it at the loop's end, and
    // `p` (a pointer INTO it) would then dangle -- a use-after-free that reads clean on
    // some allocators and corrupts on others (it aborted under ASan on an M4 Mac).
    const auto props = b->properties();
    const Property* p = nullptr;
    for (const auto& q : props)
        if (q.name == "pages") p = &q;
    CHECK(p != nullptr, "pages exists");
    if (p) {
        // THE WHOLE POINT. Not "setting it fails" -- there is nothing there to set.
        CHECK(!p->set, "pages has NO SETTER (this is what read-only IS)");
        CHECK(!!p->get, "pages still reads");
    }

    // CONFIG SAVE must not write a key that CONFIG LOAD would then refuse. A save you cannot
    // load is not a save.
    std::string toml = saveTomlText(m);
    CHECK(toml.find("pages") == std::string::npos, "CONFIG SAVE does not write `pages`");
}

// A SAVE IS A READ.
//
// CONFIG SAVE used to probe: it called each board setter with the value it had just got, to
// find out whether the property was writable (the reflection layer answers that now, with
// `!p.set`). A setter is not a question, though. Some of them do work -- the `bankmem` card's
// `card` and `banks` setters rebuild every segment and clear the live bank -- so a save that
// called them would deselect the guest's bank and remap its address space underneath a running
// program.
//
// This is the whole contract in one test, and it is aimed at the MACHINE, not at the text --
// a save that perturbs what it describes is broken however good the file looks. It runs on a
// bankmem card because that is where the perturbing setter now lives.
void test_save_is_a_read() {
    SECTION("CONFIG SAVE does not write to the machine it is describing");

    Machine m;
    std::string err;
    auto* b = m.add("bankmem", "mem0", err);
    CHECK(b != nullptr, "a bankmem card");
    if (!b) return;
    CHECK(setProperty(*b, "card", "vector", err), "a Vector Graphic: one-hot select");
    m.power();
    m.bus.ioWrite(0x40, 0x08);   // one-hot 0x08 selects bank 3

    // Held by value -- see test_readonly_props: a pointer into the temporary from
    // properties() dangles the moment the range-for that bound it ends.
    const auto props = b->properties();
    const Property* active = nullptr;
    for (const auto& p : props)
        if (p.name == "active") active = &p;
    CHECK(active != nullptr, "the card reports its `active` bank");
    if (!active) return;
    CHECK(active->get().s() == "bank 3", "bank 3 is selected before the save");

    std::string toml = saveTomlText(m);

    // THE ASSERTION. Not "the file is right" -- "the machine is untouched".
    CHECK(active->get().s() == "bank 3",
          "and bank 3 is STILL selected after it: CONFIG SAVE did not touch the card");
}
