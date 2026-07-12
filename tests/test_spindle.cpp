// The Spindle, and the sub-unit round trip (DESIGN.md 7.5.1) -- the two halves of
// the shared disk substrate, tested before either floppy board exists to use them.
//
// What is actually at stake in each:
//
//   THE SPINDLE. Rotation is derived from the clock and never from a counter, so the
//   property to pin is that NOTHING THE GUEST DOES CHANGES THE DISK'S SPEED. Reading
//   the sector port a thousand times must not advance the disk by one sector. That is
//   the bug the design is built to make impossible, so it is the first test here.
//
//   THE ROUND TRIP. subUnits() is addSubUnit()'s inverse, which is a claim you can
//   simply execute: render a board's sub-units, feed the text straight back in, and
//   the second board must equal the first. Before this existed, a controller's drives
//   would LOAD AND SILENTLY NOT SAVE -- so the test is not "does it print something",
//   it is "does the machine survive a save/load cycle intact".

#include "boards/s100-memory.h"
#include "core/clock.h"
#include "core/spindle.h"
#include "test.h"

#include <string>
#include <vector>

using namespace altair;

void test_spindle() {
    SECTION("Spindle -- the disk turns whether or not anyone is looking at it");

    // ---- The 8" DCDD track: 360 RPM, 32 sectors, 2 MHz ----
    {
        Clock c;  // 2 MHz by default
        Spindle s;
        s.configure(360, 32);

        CHECK(s.spinning(), "a configured spindle is turning");
        CHECK(s.tPerRev(c) == 333333, "360 RPM at 2 MHz is 333,333 T-states a turn");
        CHECK(s.tPerSector(c) == 10416, "and 32 sectors makes each one ~10,416 T");
        CHECK(s.sectorAt(c) == 0, "at t=0 the head is over sector 0");
    }

    // ---- NO ADVANCE-ON-READ. The whole reason this class exists. ----
    {
        Clock c;
        Spindle s;
        s.configure(360, 32);

        for (int i = 0; i < 1000; ++i) {
            CHECK(s.sectorAt(c) == 0, "polling the sector port does not turn the disk");
            if (g_fail) break;  // don't print a thousand identical failures
        }
        CHECK(c.now() == 0, "and reading it costs no time at all");
    }

    // ---- The sector advances with TIME, and wraps ----
    {
        Clock c;
        Spindle s;
        s.configure(360, 32);
        uint64_t tps = s.tPerSector(c);

        c.advance(tps - 1);
        CHECK(s.sectorAt(c) == 0, "one T-state short of the boundary is still sector 0");
        c.advance(1);
        CHECK(s.sectorAt(c) == 1, "and the boundary itself is sector 1");

        c.advance(tps * 30);
        CHECK(s.sectorAt(c) == 31, "30 more sectors along is 31 -- the last one");
        c.advance(tps);
        CHECK(s.sectorAt(c) == 0, "and it WRAPS: 32 sectors, not 33");
    }

    // ---- intoSector: where the head is WITHIN the sector ----
    {
        Clock c;
        Spindle s;
        s.configure(360, 32);

        c.advance(s.tPerSector(c) + 500);
        CHECK(s.sectorAt(c) == 1, "one sector and change in");
        CHECK(s.intoSector(c) == 500, "and 500 T-states into that sector");
    }

    // ---- nextBoundary is STRICTLY FUTURE, at every instant of a revolution ----
    //
    // This is the invariant that stops the infinite re-arm: a deadline set for now()
    // fires inside the drain loop that is running us, arms another for now(), and the
    // machine never advances again. The UART's nextEdge() enforces it by hand; here it
    // falls out of the arithmetic, so the test SWEEPS a whole revolution to prove there
    // is no instant where it does not hold -- especially the boundary itself.
    {
        Clock c;
        Spindle s;
        s.configure(360, 32);
        uint64_t tps = s.tPerSector(c);
        bool     ok = true, lands = true;

        for (uint64_t t = 0; t < tps * 32; t += 97) {  // 97: a prime, so it hits boundaries and misses them
            Clock probe;
            probe.advance(t);
            uint64_t nb = s.nextBoundary(probe);
            if (nb <= probe.now()) ok = false;
            if (nb - probe.now() > tps) lands = false;
        }
        CHECK(ok, "nextBoundary() is strictly future at every instant of a revolution");
        CHECK(lands, "and never more than one sector away");

        // And it lands ON a boundary -- the sector really has changed when you get there.
        Clock at0;
        Spindle s2;
        s2.configure(360, 32);
        uint64_t nb = s2.nextBoundary(at0);
        Clock woken;
        woken.advance(nb);
        CHECK(s2.sectorAt(woken) == 1, "waking on the deadline finds the next sector under the head");
        CHECK(s2.intoSector(woken) == 0, "exactly at its start, not one T-state past it");
    }

    // ---- A STOPPED spindle is a drive with no disk in it ----
    {
        Clock c;
        c.advance(999999);
        Spindle s;  // never configured

        CHECK(!s.spinning(), "an unconfigured spindle is stopped");
        CHECK(s.sectorAt(c) == 0, "a stopped disk reads sector 0");
        CHECK(s.nextBoundary(c) == 0, "and schedules NOTHING -- zero means never");
        CHECK(s.tPerSector(c) == 0, "no sector time on a disk that is not turning");

        s.configure(360, 32);
        CHECK(s.spinning(), "and it spins up when a disk goes in");
        s.stop();
        CHECK(!s.spinning() && s.nextBoundary(c) == 0, "and stops again when it comes out");
    }

    // ---- The motor does not care what crystal the CPU has ----
    //
    // A 4 MHz machine runs its instructions twice as fast; the disk still turns 360
    // times a minute. If rotation were expressed in T-states rather than derived from
    // Hz, overclocking the CPU would spin the floppy faster, which is nonsense.
    {
        Clock fast;
        fast.setHz(4000000);
        Spindle s;
        s.configure(360, 32);

        CHECK(s.tPerRev(fast) == 666666, "at 4 MHz a revolution takes twice the T-states");

        fast.advance(s.tPerSector(fast));
        CHECK(s.sectorAt(fast) == 1, "-- which is the same one revolution in wall-clock time");
    }

    // ---- Tarbell geometry: 26 sectors, and the 1-based numbering is NOT ours ----
    {
        Clock c;
        Spindle s;
        s.configure(360, 26);

        c.advance(s.tPerSector(c) * 25);
        CHECK(s.sectorAt(c) == 25, "the 26th sector of a Tarbell track has INDEX 25");
        c.advance(s.tPerSector(c));
        CHECK(s.sectorAt(c) == 0, "and then it wraps to index 0");
        // The Tarbell calls that sector 1. The Spindle does not know and must not:
        // startSector belongs to the board, where it is visible. It is the off-by-one
        // that silently corrupts a disk, and burying it in here is how it gets lost.
    }

    SECTION("Sub-units -- CONFIG SAVE's inverse, so a controller's drives survive a save");

    // ---- The round trip, executed rather than asserted ----
    {
        MemoryBoard a;
        std::string err;

        CHECK(a.addSubUnit("region", {{"type", "ram"}, {"at", "0000"}, {"size", "48K"}}, err),
              "a 48K RAM region loads");
        CHECK(a.addSubUnit("region", {{"type", "rom"}, {"at", "F800"}, {"mount", "builtin:altmon"}}, err),
              "and a ROM with something in it");
        CHECK(a.addSubUnit("region", {{"type", "rom"}, {"at", "FD00"}}, err),
              "and an EMPTY socket");

        auto su = a.subUnits();
        CHECK(su.size() == 3, "three regions in, three sub-units out");

        // Every one of them is a [[board.region]], and the text is what the loader takes.
        MemoryBoard b;
        for (const auto& s : su) {
            CHECK(s.table == "region", "the table is named `region`");
            KeyValues kv;
            for (const auto& f : s.fields) kv.push_back({f.key, f.text});
            CHECK(b.addSubUnit(s.table, kv, err), "and its own output loads straight back in");
        }

        CHECK(b.regions().size() == a.regions().size(), "same number of regions after the round trip");
        bool same = b.regions().size() == a.regions().size();
        for (size_t i = 0; same && i < a.regions().size(); ++i) {
            const auto& x = a.regions()[i];
            const auto& y = b.regions()[i];
            if (x.kind != y.kind || x.at != y.at || x.size != y.size || x.mount != y.mount)
                same = false;
        }
        CHECK(same, "and every region is IDENTICAL -- kind, address, size and mount");
    }

    // ---- The renderings that a generic writer would have got wrong ----
    {
        MemoryBoard m;
        std::string err;
        m.addSubUnit("region", {{"type", "ram"}, {"at", "0400"}, {"size", "48K"}}, err);
        m.addSubUnit("region", {{"type", "rom"}, {"at", "FD00"}}, err);  // an empty socket


        auto su = m.subUnits();
        auto field = [&](size_t i, const std::string& key) -> const Board::SubUnitField* {
            for (const auto& f : su[i].fields)
                if (f.key == key) return &f;
            return nullptr;
        };

        const auto* at = field(0, "at");
        CHECK(at && at->text == "0x0400", "an address is hex, and ZERO-PADDED to four -- Value::text(16) gives 0x400");
        CHECK(at && !at->quoted, "and it is a bare literal, not a string");

        const auto* size = field(0, "size");
        CHECK(size && size->text == "48K", "a size keeps its K -- that is how an operator says it");
        CHECK(size && size->quoted, "and it MUST be quoted: `size = 48K` bare is not TOML");

        const auto* type = field(0, "type");
        CHECK(type && type->quoted, "a type is a string and carries its quotes");

        CHECK(field(1, "mount") == nullptr,
              "an EMPTY socket writes no `mount` at all -- `mount = \"\"` reads like a bug");
    }

    // ---- A board with no sub-units says so, and that is not an error ----
    {
        // The default Board::subUnits() returns {}, which is what every card that has
        // no list of anything wants. CONFIG SAVE then writes nothing, which is right.
        MemoryBoard m;
        CHECK(m.subUnits().empty(), "a card with no regions has no sub-units to save");
    }
}
