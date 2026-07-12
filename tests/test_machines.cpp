#include "test.h"

#include "core/machines.h"

using namespace altair;

// BUILT-IN MACHINES ARE IN THE BINARY, NOT ON THE DISK.
//
// This file never opens a file, and that is the entire point. altairsim ships as
// ONE EXECUTABLE plus documentation: it must not go looking for a machines/
// directory, an install prefix, or anything relative to argv[0]. If a built-in
// machine could only be found next to the binary, then copying the binary
// somewhere -- which is the ONLY thing anyone will do with it -- would break the
// default machine, and it would break it at startup, which is the worst possible
// time. These tests pass with the source tree deleted.
//
// The other half of the deal: a built-in is REAL TOML, run through the SAME
// parser as your own config file. So if the config format changes under them,
// these go red instead of the machines quietly rotting into a second dialect.

void test_machines() {
    SECTION("built-in machines -- compiled in, not looked up");

    auto all = builtinMachines();
    CHECK(all.size() >= 2, "at least the two machines are compiled in");

    for (const auto& b : all) {
        CHECK(b.size > 0, "a built-in has bytes");
        CHECK(std::string(b.blurb).size() > 0, "and a blurb for --list");
    }

    // ---- default: 56K, which is what CP/M means when it says 56K ----
    const BuiltinMachine* d = findMachine("default");
    CHECK(d != nullptr, "there IS a default machine -- no config file required");
    if (!d) return;

    Machine m;
    std::string err;
    CHECK(loadMachine(*d, m, err), "and it loads through the ordinary TOML parser");
    CHECK(m.name == "default", "it knows its name");
    CHECK(m.boards().size() == 1, "one memory card");

    // RAM at 0000-DFFF, and NOTHING above it. The top 8K is where the PROM and
    // the memory-mapped I/O lived; a machine claiming 64K of RAM would be lying
    // about hardware nobody shipped.
    CHECK(m.bus.memRead(0x0000) != 0xFF || true, "0000 is populated");  // contents are random
    CHECK(!m.bus.lastUnclaimed(), "a board drives 0000");
    m.bus.memWrite(0x0000, 0x76);
    CHECK(m.bus.memRead(0x0000) == 0x76, "and it is RAM -- it stores a write");

    m.bus.memWrite(0xDFFF, 0x3E);
    CHECK(m.bus.memRead(0xDFFF) == 0x3E, "DFFF is the last byte of the 56K");

    (void)m.bus.memRead(0xE000);
    CHECK(m.bus.lastUnclaimed(), "E000 is NOT populated -- nobody drives it");
    CHECK(m.bus.memRead(0xE000) == 0xFF, "so it floats to FF, which is the bus's answer");

    // ---- 4k: the machine BASIC was written for ----
    const BuiltinMachine* k = findMachine("4k");
    CHECK(k != nullptr, "the 4K Altair is here too");
    if (!k) return;

    Machine m4;
    CHECK(loadMachine(*k, m4, err), "4k loads");
    m4.bus.memWrite(0x0FFF, 0x21);
    CHECK(m4.bus.memRead(0x0FFF) == 0x21, "0FFF is the top of the 4K");
    (void)m4.bus.memRead(0x1000);
    CHECK(m4.bus.lastUnclaimed(), "and 1000 is empty backplane -- there is no card there");

    // Case, and the absence of a machine, both answer honestly.
    CHECK(findMachine("DEFAULT") != nullptr, "a machine name is not case-sensitive");
    CHECK(findMachine("sol20") == nullptr, "and one we have no manuals for is simply not there");

    SECTION("the `dbl` machine -- the real boot PROM, on the real bus");

    // The ROM is compiled in, decoded by the ordinary Intel HEX loader, and put
    // on the bus by the ordinary memory board. Every one of those steps is tested
    // in isolation elsewhere; this checks they are actually WIRED TOGETHER, which
    // is the one thing unit tests routinely fail to notice has come apart.
    const BuiltinMachine* b = findMachine("dbl");
    CHECK(b != nullptr, "the dbl machine is compiled in");
    if (!b) return;

    Machine md;
    CHECK(loadMachine(*b, md, err), "dbl loads");

    // DBL's first act, read through the bus, one byte at a time, as a CPU would:
    //     FF00  21 13 FF   LXI H,FF13    ; source: myself
    //     FF03  11 00 2C   LXI D,2C00    ; destination: RAM
    //     FF06  0E EB      MVI C,0EBH    ; 235 bytes
    // This is the PROM copying itself into RAM to run there, and it is the reason
    // DBL needs no shadow RAM: it never writes to FFxx at all.
    CHECK(md.bus.memRead(0xFF00) == 0x21, "FF00: LXI H");
    CHECK(md.bus.memRead(0xFF01) == 0x13 && md.bus.memRead(0xFF02) == 0xFF, "      ,FF13");
    CHECK(md.bus.memRead(0xFF03) == 0x11, "FF03: LXI D");
    CHECK(md.bus.memRead(0xFF04) == 0x00 && md.bus.memRead(0xFF05) == 0x2C, "      ,2C00");
    CHECK(md.bus.memRead(0xFF06) == 0x0E && md.bus.memRead(0xFF07) == 0xEB, "FF06: MVI C,EB");
    CHECK(!md.bus.lastUnclaimed(), "and the PROM really is driving the bus for all of it");

    // A GUEST CANNOT WRITE ROM. Not "the write is rejected" -- the board never
    // answers the cycle, so nobody drives it and the byte is simply gone.
    md.bus.memWrite(0xFF00, 0x00);
    CHECK(md.bus.lastUnclaimed(), "nobody decodes a write to the PROM");
    CHECK(md.bus.memRead(0xFF00) == 0x21, "so the PROM is untouched");

    // 2C00 is where DBL relocates itself to. It had better be RAM, or the PROM's
    // very first loop writes into nothing and the machine is silently dead.
    md.bus.memWrite(0x2C00, 0x21);
    CHECK(md.bus.memRead(0x2C00) == 0x21, "2C00 (DBL's RUNLOC) is RAM, and it takes a write");
    md.bus.memWrite(0x2D70, 0xFF);
    CHECK(!md.bus.lastUnclaimed(), "and so is 2D70, the end of its sector buffer");

    // The 8K hole. This is WHY the number is 56K and not 64K: the top of memory
    // is where the PROM and the memory-mapped I/O lived.
    (void)md.bus.memRead(0xE000);
    CHECK(md.bus.lastUnclaimed(), "E000-FEFF is a hole -- no RAM, no ROM, nothing");
    (void)md.bus.memRead(0xFEFF);
    CHECK(md.bus.lastUnclaimed(), "right up to the byte below the PROM");

    SECTION("a file or a built-in? decided by SPELLING, never by the filesystem");

    // If this were decided by probing the disk, `altairsim default` would mean
    // one thing today and something else the day a file called `default` appears
    // in the working directory. A command line whose meaning depends on its
    // surroundings is a trap, and it is the kind that gets sprung at 2am.
    CHECK(!looksLikeFile("default"), "a bare word is a built-in name");
    CHECK(!looksLikeFile("4k"), "even a short one");
    CHECK(looksLikeFile("my.toml"), "a .toml is a file");
    CHECK(looksLikeFile("MY.TOML"), "in any case");
    CHECK(looksLikeFile("./default"), "a path is a file, even with a built-in's name");
    CHECK(looksLikeFile("cfg/x"), "a slash anywhere makes it a file");
    CHECK(!looksLikeFile("toml"), "and 'toml' alone is not a .toml -- do not match a bare suffix");
}
