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

    SECTION("the twelve undocumented opcodes -- DDT's `?\?= <byte>`, one byte, bare mnemonic");

    // Real DDT/SID print `??= <byte>` for a byte outside the published set and step
    // ONE byte over it -- the following bytes are not its operand. We keep that: the
    // honest marker plus the BARE mnemonic of what the byte would do if executed
    // (`*JMP`, `*CALL`), len 1, no address invented. The byte follows the console
    // base (hex here).
    uint8_t undoc[] = {0x08, 0xCB, 0x00, 0x10, 0xD9, 0xDD, 0x00, 0x20};
    auto up = [&](uint16_t a) -> uint8_t { return a < sizeof undoc ? undoc[a] : 0x00; };

    Insn u0 = d->at(0, up);
    CHECK(u0.text == "?\?= 08  *NOP", "08 is a NOP on real silicon, marked the DDT way");
    CHECK(u0.undocumented, "and flagged");
    CHECK(u0.len == 1, "one byte");

    Insn u1 = d->at(1, up);
    CHECK(u1.text == "?\?= CB  *JMP", "CB would jump, but as data it is one byte -- no address read");
    CHECK(u1.len == 1, "ONE byte, like DDT -- not the 3-byte JMP the CPU would run");

    Insn u2 = d->at(4, up);
    CHECK(u2.text == "?\?= D9  *RET", "D9 is a RET");
    CHECK(u2.len == 1, "one byte");

    Insn u3 = d->at(5, up);
    CHECK(u3.text == "?\?= DD  *CALL", "DD is one of the three undocumented CALLs -- bare, one byte");
    CHECK(u3.len == 1, "one byte -- the two that follow are not its target");

    Insn ok = d->at(2, up);
    CHECK(ok.text == "NOP" && !ok.undocumented, "a documented NOP carries no marker");

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

    // -----------------------------------------------------------------------
    // THE ASSEMBLER -- the disassembler read the other way. Same layer, same
    // statelessness: text in, bytes out, no CPU.
    // -----------------------------------------------------------------------
    SECTION("the assembler round-trips the whole opcode map -- disassemble, reassemble, same bytes");

    const Assembler* a = assemblerFor("8080");
    CHECK(a != nullptr, "we assemble 8080");
    if (!a) return;
    CHECK(std::string(a->name()) == "8080", "and it says so");
    CHECK(assemblerFor("8080") == assemblerFor("8080"), "the same one every time");
    CHECK(assemblerFor("Z80") == nullptr, "but NOT Z80 yet -- assemblerFor returns null, EDIT falls back to bytes");
    CHECK(assemblerFor("") == nullptr, "and not the empty ISA of a CPU-less machine");

    // For every one of the 256 opcodes: lay it down with NON-PALINDROMIC operand
    // bytes (34 12, so a low/high transposition can't hide), disassemble it, feed
    // that text back to the assembler, and demand the exact same bytes. A word
    // operand must come back 34 12 -- low byte first -- or the round-trip fails.
    for (int b = 0; b < 256; ++b) {
        uint8_t win[3] = {(uint8_t)b, 0x34, 0x12};
        auto wp = [&](uint16_t x) -> uint8_t { return x < 3 ? win[x] : 0x00; };
        Insn in = d->at(0, wp);

        AsmResult r = a->assemble(0, in.text);
        if (in.undocumented) {
            // The `??= XX *MNEM` text a disassembler prints for an undocumented byte
            // is never valid input -- the assembler must reject it, not resurrect it.
            CHECK(!r.error.empty() && r.bytes.empty(),
                  "an undocumented opcode's disassembly does not assemble back");
            continue;
        }
        bool okbytes = r.error.empty() && r.bytes.size() == in.len && r.bytes[0] == (uint8_t)b;
        if (okbytes && in.len >= 2) okbytes = r.bytes[1] == win[1];
        if (okbytes && in.len == 3) okbytes = r.bytes[2] == win[2];
        CHECK(okbytes, ("round-trips: " + in.text).c_str());
    }

    SECTION("the assembler -- spot encodings, leniency, octal, and honest errors");

    auto asmBytes = [&](const std::string& line, int base = 16) {
        return a->assemble(0, line, base).bytes;
    };
    auto eq = [](const std::vector<uint8_t>& got, std::vector<uint8_t> want) { return got == want; };

    CHECK(eq(asmBytes("LXI H,FF13"), {0x21, 0x13, 0xFF}), "LXI H,FF13 -> 21 13 FF, operand LOW BYTE FIRST");
    CHECK(eq(asmBytes("IN 10"), {0xDB, 0x10}), "IN 10 -> DB 10");
    CHECK(eq(asmBytes("MVI C,EB"), {0x0E, 0xEB}), "MVI C,EB -> 0E EB");
    CHECK(eq(asmBytes("MOV A,B"), {0x78}), "MOV A,B -> 78, a one-byte register move");
    CHECK(eq(asmBytes("RST 7"), {0xFF}), "RST 7 -> FF, exact-mapped, its digit not parsed as a number");
    CHECK(eq(asmBytes("XTHL"), {0xE3}), "XTHL -> E3");
    CHECK(eq(asmBytes("JMP C3C3"), {0xC3, 0xC3, 0xC3}), "JMP C3C3 -> C3 C3 C3");
    CHECK(eq(asmBytes("PUSH PSW"), {0xF5}), "PUSH PSW -> F5, no operand despite the space");

    // Leniency: case, spacing and an explicit suffix all reach the same bytes.
    CHECK(eq(asmBytes("jmp 0100"), {0xC3, 0x00, 0x01}), "lowercase assembles");
    CHECK(eq(asmBytes("MVI  C, EB"), {0x0E, 0xEB}), "extra spaces and a space after the comma are forgiven");
    CHECK(eq(asmBytes("IN 10H"), {0xDB, 0x10}), "a trailing H suffix is honored");
    CHECK(eq(asmBytes("IN 0x10"), {0xDB, 0x10}), "and a 0x prefix");

    // Octal: base 8 changes ONLY how a bare operand is read. 377q and an explicit
    // suffix agree with base-8 parsing.
    CHECK(eq(asmBytes("MVI C,377", 8), {0x0E, 0xFF}), "base 8: 377 is FF");
    CHECK(eq(asmBytes("MVI C,377Q", 16), {0x0E, 0xFF}), "a Q suffix forces octal even in hex mode");

    // Errors: empty bytes AND a non-empty message, every time.
    auto errs = [&](const std::string& line, int base = 16) {
        AsmResult r = a->assemble(0, line, base);
        return r.bytes.empty() && !r.error.empty();
    };
    CHECK(errs("FOO 1"), "unknown mnemonic errors");
    CHECK(errs("JMP"), "a word instruction with no operand errors");
    CHECK(errs("MVI C,1FF"), "a byte operand over FF errors");
    CHECK(errs("MVI C,100", 16), "MVI C,100 in hex is 0x100 -- too large for a byte");
    CHECK(errs("MVI C,8", 8), "an octal digit out of range errors");
    CHECK(errs(""), "an empty line errors");
    CHECK(errs("?\?= 08  *NOP"), "and the disassembler's undocumented marker is not valid input");
}
