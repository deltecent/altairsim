#include "test.h"

#include "core/crc32.h"
#include "core/roms.h"

using namespace altair;

// DESIGN.md 0.1 applied to binaries. A ROM image is a HARDWARE FACT: if a bit is
// wrong, every piece of software above it gets debugged against the wrong ground
// truth, and it looks like a software bug for a very long time. So the CRC of
// every built-in is checked here, against the value recorded in docs/roms.md --
// and someone's editor mangling a binary becomes a failing test instead of a
// mystery.
void test_roms() {
    SECTION("built-in ROMs -- provenance (docs/roms.md)");

    CHECK(!builtinRoms().empty(), "at least one ROM is compiled in");

    const BuiltinRom* dbl = findRom("dbl");
    CHECK(dbl != nullptr, "builtin:dbl exists");
    if (!dbl) return;

    Image img;
    std::string err;
    CHECK(decodeRom(*dbl, 0, img, err), "DBL.HEX decodes (every record checksums)");

    // Altair Disk Boot Loader 4.1, disassembled by Martin Eberhard from an EPROM
    // labeled "DBL 4.1" found socketed in a MITS Turnkey board.
    CHECK(img.size() == 256, "DBL is exactly 256 bytes -- a complete part, not truncated");
    CHECK(img.lo() == 0xFF00, "DBL starts at FF00");
    CHECK(img.hi() == 0xFFFF, "DBL ends at FFFF");
    CHECK(img.contiguous(), "DBL has no gaps");

    auto flat = img.flat();
    CHECK(crc32(flat) == 0x8E658905u, "DBL CRC32 == 8E658905 (docs/roms.md)");

    // The PROM's first act: copy ITSELF from FF13 into RAM at 2C00 and jump
    // there, because the EPROM was too slow to execute from. Checked here
    // because it is the fact that disproves the "DBL needs shadow RAM" claim
    // that was fabricated in an earlier draft -- DBL never writes to FFxx at all.
    CHECK(flat[0] == 0x21 && flat[1] == 0x13 && flat[2] == 0xFF, "FF00: LXI H,FF13");
    CHECK(flat[3] == 0x11 && flat[4] == 0x00 && flat[5] == 0x2C, "FF03: LXI D,2C00");
    CHECK(flat[6] == 0x0E && flat[7] == 0xEB, "FF06: MVI C,EB  (235 bytes to copy)");
    CHECK(flat[0x10] == 0xC3 && flat[0x11] == 0x00 && flat[0x12] == 0x2C, "FF10: JMP 2C00");

    // ---- MDBL: the MINIdisk boot loader, and it is NOT the same PROM ----------------
    //
    // Same address, same size, same self-relocating trick -- and it will not read an 8"
    // floppy any more than DBL will read a minidisk. The two are one instruction apart at
    // FF03 and a whole controller apart in fact, which is exactly why they both get a CRC
    // here: put the wrong one at FF00 and nothing announces it. The machine just hangs.
    const BuiltinRom* mdbl = findRom("mdbl");
    CHECK(mdbl != nullptr, "builtin:mdbl exists");
    if (!mdbl) return;

    Image m;
    CHECK(decodeRom(*mdbl, 0, m, err), "MDBL.HEX decodes (every record checksums)");
    CHECK(m.size() == 256, "MDBL is exactly 256 bytes");
    CHECK(m.lo() == 0xFF00 && m.hi() == 0xFFFF, "MDBL lives at FF00-FFFF, same socket as DBL");
    CHECK(m.contiguous(), "MDBL has no gaps");

    auto mf = m.flat();
    CHECK(crc32(mf) == 0x3BC20ADDu, "MDBL CRC32 == 3BC20ADD (docs/roms.md)");

    // The same opening move as DBL -- copy myself into RAM, because a 1702A is too slow to
    // run from -- but to 4C00, not 2C00, and 227 bytes, not 235. THAT is the whole tell.
    CHECK(mf[0] == 0x21 && mf[1] == 0x13 && mf[2] == 0xFF, "FF00: LXI H,FF13  (same as DBL)");
    CHECK(mf[3] == 0x11 && mf[4] == 0x00 && mf[5] == 0x4C, "FF03: LXI D,4C00  -- and DBL says 2C00");
    CHECK(mf[6] == 0x0E && mf[7] == 0xE3, "FF06: MVI C,E3  (227 bytes -- DBL copies 235)");
    CHECK(mf != flat, "so they are DIFFERENT PROMS, and the CRCs above are what keep them apart");
}
