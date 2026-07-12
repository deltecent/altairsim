// The bus/board boundary (DESIGN.md 4, 4.6.1).
//
// PATRICK'S RULE, 2026-07-11:
//   "The only thing the bus should do is provide FF if no RAM or IO is processed
//    by a board. There should be minimal bus/board overlap."
//
// So 0xFF is the BUS's, and it means exactly one thing: NOBODY DROVE THIS CYCLE.
// A board must never manufacture 0xFF, because if it does, the one signal the bus
// has stops being a signal. Uninitialized RAM and an empty socket are completely
// different faults, and they must not look the same.
//
// The failure this catches is a quiet one. Seed a board's store with 0xFF -- it is
// the obvious "uninitialized" filler, and it is what this code did until Patrick
// caught it -- and DUMP shows FF for a card whose RAM is fine, FF for a card whose
// RAM was never filled, and FF for a card that is not there. One symptom, three
// causes, and you will chase the wrong one.

#include "boards/memory.h"
#include "core/bus.h"
#include "test.h"

using namespace altair;

void test_boundary() {
    SECTION("the bus/board boundary -- 0xFF is the BUS's word, and only the bus's");

    Bus bus;
    MemoryBoard mem;
    mem.id = "mem0";
    bus.attach(&mem);

    std::string err;
    CHECK(setProperty(mem, "fill", "zero", err), "fill=zero");

    Region r;
    r.kind = RegionKind::Ram;
    r.at = 0x0000;
    r.size = 0x0400;
    CHECK(mem.addRegion(r, err), "1K of RAM at 0000");

    // ---- The board's side: RAM the board owns is the board's to fill. ----
    // NOTE: no power() call. The card was populated while the machine was up, and
    // the chips still have to contain SOMETHING -- and what they contain is this
    // board's business (`fill`), decided by this board, with no help from the bus.
    bool anyFF = false;
    for (uint32_t a = 0; a < 0x400; ++a)
        if (bus.memRead((uint16_t)a) != 0x00) anyFF = true;
    CHECK(!anyFF, "populated RAM reads its OWN fill (00), never the bus's 0xFF");

    // ---- The bus's side: nobody drove it, so it floats. ----
    CHECK(bus.memRead(0x0400) == 0xFF, "the very next page is empty -- the BUS floats it to FF");
    CHECK(bus.lastUnclaimed(), "and the bus knows nobody answered");

    CHECK(bus.memRead(0x0000) == 0x00, "while 0000 is answered by a board");
    CHECK(!bus.lastUnclaimed(), "and the bus knows somebody did");

    // The same one rule, everywhere the bus can be left undriven. None of these is
    // a special case, and no board is consulted about any of them.
    CHECK(bus.ioRead(0x10) == 0xFF, "an unclaimed IN floats to FF");
    CHECK(bus.intAck() == 0xFF, "an unvectored INTA floats to FF -- which is RST 7");

    // ---- The distinction that seeding a store with 0xFF would destroy ----
    MemoryBoard bad;
    bad.id = "mem1";
    std::string e2;
    CHECK(setProperty(bad, "fill", "random", e2), "fill=random");
    Region r2;
    r2.kind = RegionKind::Ram;
    r2.at = 0x8000;
    r2.size = 0x0100;
    CHECK(bad.addRegion(r2, e2), "a page of random RAM at 8000");

    int ff = 0;
    for (size_t i = 0x8000; i < 0x8100; ++i)
        if (bad.rawRead(i) == 0xFF) ++ff;
    // ~1 byte in 256 is legitimately 0xFF. A store seeded with 0xFF gives 256.
    CHECK(ff < 16, "random RAM is random -- a store full of FF means it was never filled");

    // An offset with no chip behind it is a CALLER bug (bounds-check with rawSize),
    // and it must not answer 0xFF either -- that would be the board impersonating
    // the bus in the one place the bus cannot see.
    CHECK(bad.rawRead(bad.rawSize() + 1) != 0xFF, "rawRead past the store does not fake a float");
}
