#include "test.h"

#include "isa/isa.h"

using namespace altair;

// THE DISASSEMBLER IS STATELESS AND NEEDS NO CPU. Not one line of this file
// builds a Machine, a Bus, or a Board -- it hands the decoder a lambda over an
// array. That is not a testing trick; it is the layer boundary (DESIGN.md 3.0.2)
// being real, and it is why `DISASM FF00 CPU=8080` worked against the DBL PROM in
// milestone 1a, before anything could execute.

void test_isa() {
    SECTION("the 8080 instruction set -- bytes in, text out, nothing else");

    const Disassembler* d = disassemblerFor("8080");
    CHECK(d != nullptr, "we speak 8080");
    if (!d) return;
    CHECK(std::string(d->name()) == "8080", "and it says so");
    CHECK(disassemblerFor("8080") == disassemblerFor("8080"), "the same one every time");
    CHECK(disassemblerFor("Z80") != nullptr,
          "and we NOW speak Z80 -- a decoder of its own, not the 8080 with a costume on");
    CHECK(disassemblerFor("Z80") != disassemblerFor("8080"),
          "and it is a DIFFERENT decoder -- the same bytes read as CB-prefix or NOP, not both");
    CHECK(disassemblerFor("8080") == disassemblerFor("8080"), "case does not matter");

    // DBL's first three instructions, which are the reason the PROM needs no
    // shadow RAM: it copies itself into RAM at 2C00 and runs there.
    uint8_t code[] = {0x21, 0x13, 0xFF,   // LXI H,FF13
                      0x11, 0x00, 0x2C,   // LXI D,2C00
                      0x0E, 0xEB};        // MVI C,EB
    auto peek = [&](uint16_t a) -> uint8_t { return a < sizeof code ? code[a] : 0xFF; };

    Insn i0 = d->at(0, peek);
    CHECK(i0.text == "LXI H,FF13", "LXI H, with the operand read LOW BYTE FIRST");
    CHECK(i0.len == 3, "and it is three bytes long");

    Insn i1 = d->at(3, peek);
    CHECK(i1.text == "LXI D,2C00", "LXI D,2C00 -- the destination, in RAM");

    Insn i2 = d->at(6, peek);
    CHECK(i2.text == "MVI C,EB", "MVI C,EB -- 235 bytes to copy");
    CHECK(i2.len == 2, "two bytes");

    SECTION("the ten undocumented opcodes -- real silicon runs them, so we print them");

    // A disassembler that prints `???` here is LYING about what the chip does. Code
    // in the wild hits these. They are marked with a `*` so that a RUN of them --
    // which nearly always means you are looking at data, or at a Z80 binary -- is
    // visible at a glance instead of being mistaken for a working program.
    uint8_t undoc[] = {0x08, 0xCB, 0x00, 0x10, 0xD9, 0xDD, 0x00, 0x20};
    auto up = [&](uint16_t a) -> uint8_t { return a < sizeof undoc ? undoc[a] : 0x00; };

    Insn u0 = d->at(0, up);
    CHECK(u0.text == "*NOP", "08 is a NOP on real silicon, and it is marked");
    CHECK(u0.undocumented, "and flagged");
    CHECK(u0.len == 1, "one byte");

    Insn u1 = d->at(1, up);
    CHECK(u1.text == "*JMP 1000", "CB is a JMP -- three bytes, and it really does jump");
    CHECK(u1.len == 3, "so a disassembler that called it a 1-byte ??? would desynchronise");

    Insn u2 = d->at(4, up);
    CHECK(u2.text == "*RET", "D9 is a RET");

    Insn ok = d->at(2, up);
    CHECK(ok.text == "NOP" && !ok.undocumented, "a documented NOP carries no star");

    SECTION("the awkward corners of the opcode map");

    // 76 is the hole punched in the middle of the MOV block. `MOV M,M` would be
    // the one meaningless MOV, so Intel spent the slot on HLT.
    uint8_t misc[] = {0x76, 0x7F, 0x46, 0x70, 0xC7, 0xFF, 0xDB, 0x10, 0xD3, 0x11, 0xE3};
    auto mp = [&](uint16_t a) -> uint8_t { return a < sizeof misc ? misc[a] : 0x00; };

    CHECK(d->at(0, mp).text == "HLT", "76 is HLT, not MOV M,M -- the hole in the MOV block");
    CHECK(d->at(1, mp).text == "MOV A,A", "7F is the identity move, and it is legal");
    CHECK(d->at(2, mp).text == "MOV B,M", "46 reads through HL");
    CHECK(d->at(3, mp).text == "MOV M,B", "70 writes through it");
    CHECK(d->at(4, mp).text == "RST 0", "C7 is RST 0");
    CHECK(d->at(5, mp).text == "RST 7", "FF is RST 7 -- which is what a FLOATING BUS decodes to");
    CHECK(d->at(6, mp).text == "IN 10", "IN takes a port, and a port is hex");
    CHECK(d->at(8, mp).text == "OUT 11", "so does OUT");
    CHECK(d->at(10, mp).text == "XTHL", "and XTHL is XTHL");

    // Reading past the end of memory must not crash or wrap into a lie. The peek
    // wraps at 16 bits, exactly as the 8080's address bus does.
    auto ff = [](uint16_t) -> uint8_t { return 0xC3; };  // JMP everywhere
    Insn top = d->at(0xFFFF, ff);
    CHECK(top.text == "JMP C3C3", "an instruction at FFFF reads its operand from 0000 -- it WRAPS");
}
