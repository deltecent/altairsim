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

    // ---- Martin Eberhard's improved boot loaders and monitors -----------------------
    //
    // Same rule as DBL/MDBL: a ROM image is a hardware fact, so every built-in gets its
    // CRC32 checked against the value recorded in docs/roms.md. A corrupted embed then
    // fails the BUILD, not a user chasing a phantom software bug. Provenance and the
    // per-ROM listing each was verified against are in docs/roms.md and roms/<NAME>/.
    struct Case {
        const char* name;
        uint16_t lo, hi;
        size_t size;
        uint32_t crc;
        bool contiguous;
    };
    // CDBL 3.00, HDBL 2.00, ACUTER 1.0 and CUTER 1.3 place themselves contiguously; AMON
    // 3.0 is a 4K monitor image with gaps (a vector at F000, the bulk from F800), so flat()
    // spans F000-FFFE (4095 bytes) with the 288 unprogrammed bytes read back as FF -- the
    // CRC below is over that FF-filled span, exactly what SHOW ROMS and a mounted region see.
    // CUTER 1.3 is the original Processor Technology part (VDM console at CC00, port C8),
    // distinct from ACUTER, which is Douglas's serial-console Altair port of the same source.
    // SOLOS 1.3 is CUTER's Sol-20 sibling -- the same era and vendor, but the Sol-PC's own OS,
    // driving its integrated I/O at the fixed hardware ports F8-FF (a complete 2048-byte part).
    const Case cases[] = {
        {"cdbl",   0xFF00, 0xFFF4,  245, 0x0558293Eu, true},
        {"hdbl",   0xFC00, 0xFCFE,  255, 0x796FCA9Bu, true},
        {"amon",   0xF000, 0xFFFE, 4095, 0xC00DC413u, false},
        {"acuter", 0xF000, 0xF7FF, 2048, 0x4A4E608Du, true},
        {"cuter",  0xC000, 0xC7FA, 2043, 0xB0106ED2u, true},
        {"solos",  0xC000, 0xC7FF, 2048, 0x4D0AF383u, true},
    };
    for (const auto& c : cases) {
        std::string tag = std::string("builtin:") + c.name;
        const BuiltinRom* r = findRom(c.name);
        CHECK(r != nullptr, (tag + " exists").c_str());
        if (!r) continue;

        Image ri;
        std::string rerr;
        CHECK(decodeRom(*r, 0, ri, rerr), (tag + " decodes (every record checksums)").c_str());
        CHECK(ri.lo() == c.lo && ri.hi() == c.hi,
              (tag + " occupies the range docs/roms.md records").c_str());
        CHECK(ri.flat().size() == c.size, (tag + " decodes to its recorded size").c_str());
        CHECK(ri.contiguous() == c.contiguous,
              (tag + " gap structure matches docs/roms.md").c_str());
        CHECK(crc32(ri.flat()) == c.crc, (tag + " CRC32 matches docs/roms.md").c_str());
    }
}
