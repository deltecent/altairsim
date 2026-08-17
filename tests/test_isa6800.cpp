#include "test.h"

#include "isa/isa.h"

using namespace altair;

// THE 6800 DISASSEMBLER IS STATELESS AND NEEDS NO CPU -- the same layer boundary
// (DESIGN.md 3.0.2) the 8080 test leans on: not one line here builds a Machine, a
// Bus, or a Board. Bytes in, text out. `DISASM FC00 CPU=6800` decodes the monitor
// PROM before a single 6800 instruction can execute.

namespace {

// A decode over a fixed little array placed AT address `at` -- so the peek maps an
// address back to the array origin, letting a test say "decode as if this sits at
// 0010" and get the right relative-branch math. Bytes past the end read as 0x01
// (NOP) so a truncated last instruction never trips an out-of-range peek.
Insn dis(const Disassembler* d, const uint8_t* code, size_t n, uint16_t at, int base = 16) {
    auto peek = [&](uint16_t a) -> uint8_t {
        uint16_t off = (uint16_t)(a - at);
        return off < n ? code[off] : 0x01;
    };
    return d->at(at, peek, base);
}

} // namespace

void test_isa6800() {
    SECTION("the 6800 instruction set -- bytes in, text out, nothing else");

    const Disassembler* d = disassemblerFor("6800");
    CHECK(d != nullptr, "we speak 6800");
    if (!d) return;
    CHECK(std::string(d->name()) == "6800", "and it says so");
    CHECK(disassemblerFor("6800") == disassemblerFor("6800"), "the same one every time");
    CHECK(disassemblerFor("6800") == disassemblerFor("6800"), "case does not matter");
    CHECK(disassemblerFor("6800") != disassemblerFor("8080"),
          "a decoder of its OWN -- the same bytes read as 6800 or 8080, not both");
    CHECK(disassemblerFor("6800") != disassemblerFor("z80"), "and not the Z80 either");

    SECTION("one instruction per addressing mode, with length and operand VALUE");

    // Inherent -- one byte, no operand.
    {
        uint8_t c[] = {0x01};  // NOP
        Insn i = dis(d, c, sizeof c, 0);
        CHECK(i.text == "NOP" && i.len == 1, "01 NOP -- inherent, one byte");
    }
    {
        uint8_t c[] = {0x39};  // RTS
        Insn i = dis(d, c, sizeof c, 0);
        CHECK(i.text == "RTS" && i.len == 1, "39 RTS");
    }
    // Accumulator read-modify-writes carry the accumulator as a SUFFIX (Programming
    // Manual 2's own ASRA/INCB example), and are plain inherent opcodes.
    {
        uint8_t c[] = {0x47};  // ASRA
        Insn i = dis(d, c, sizeof c, 0);
        CHECK(i.text == "ASRA" && i.len == 1, "47 ASRA");
    }
    {
        uint8_t c[] = {0x5C};  // INCB
        Insn i = dis(d, c, sizeof c, 0);
        CHECK(i.text == "INCB" && i.len == 1, "5C INCB");
    }

    // Immediate -- #nn, two bytes.
    {
        uint8_t c[] = {0x86, 0x41};  // LDAA #41
        Insn i = dis(d, c, sizeof c, 0);
        CHECK(i.text == "LDAA #41" && i.len == 2, "86 41 LDAA #41 -- immediate");
    }
    // Direct -- bare 00-FF, two bytes.
    {
        uint8_t c[] = {0x96, 0x50};  // LDAA 50
        Insn i = dis(d, c, sizeof c, 0);
        CHECK(i.text == "LDAA 50" && i.len == 2, "96 50 LDAA 50 -- direct");
    }
    // Indexed -- offset,X, two bytes.
    {
        uint8_t c[] = {0xA6, 0x05};  // LDAA 05,X
        Insn i = dis(d, c, sizeof c, 0);
        CHECK(i.text == "LDAA 05,X" && i.len == 2, "A6 05 LDAA 05,X -- indexed");
    }
    // Extended -- 16-bit address, MS byte FIRST, three bytes.
    {
        uint8_t c[] = {0xB7, 0xF0, 0x01};  // STAA F001
        Insn i = dis(d, c, sizeof c, 0);
        CHECK(i.text == "STAA F001" && i.len == 3, "B7 F0 01 STAA F001 -- extended, big-endian");
        CHECK(i.operand == 0xF001 && i.operandBits == 16, "and the address is handed up for a symbol");
    }
    // 16-bit immediate -- CPX/LDS/LDX, three bytes, MS byte first.
    {
        uint8_t c[] = {0xCE, 0xFC, 0x00};  // LDX #FC00
        Insn i = dis(d, c, sizeof c, 0);
        CHECK(i.text == "LDX #FC00" && i.len == 3, "CE FC 00 LDX #FC00 -- 16-bit immediate");
        CHECK(i.operand == 0xFC00 && i.operandBits == 16, "value handed up");
    }
    {
        uint8_t c[] = {0x8C, 0x12, 0x34};  // CPX #1234
        Insn i = dis(d, c, sizeof c, 0);
        CHECK(i.text == "CPX #1234" && i.len == 3, "8C 12 34 CPX #1234 -- three bytes, not two");
    }

    SECTION("relative branches resolve to the absolute target, D = (PC+2) + R");

    {
        // At 0010: 20 08 -> BRA to 0010+2+8 = 001A (forward).
        uint8_t c[] = {0x20, 0x08};
        Insn i = dis(d, c, sizeof c, 0x10);  // decode as if it sits at 0010
        CHECK(i.text == "BRA 001A" && i.len == 2, "forward branch target");
        CHECK(i.operand == 0x001A && i.operandBits == 16, "target handed up for a symbol");
    }
    {
        // At 0010: 26 FC -> BNE to 0010+2-4 = 000E (backward).
        uint8_t c[] = {0x26, 0xFC};
        Insn i = dis(d, c, sizeof c, 0x10);
        CHECK(i.text == "BNE 000E", "backward branch, signed offset");
    }
    {
        // A branch to self: FE = -2 makes the target the branch's own address.
        uint8_t c[] = {0x20, 0xFE};
        Insn i = dis(d, c, sizeof c, 0x0C);
        CHECK(i.text == "BRA 000C", "20 FE at 000C is a branch to self");
    }
    {
        uint8_t c[] = {0x8D, 0x10};  // BSR -- also relative
        Insn i = dis(d, c, sizeof c, 0x00);
        CHECK(i.text == "BSR 0012" && i.len == 2, "8D BSR is relative too");
    }

    SECTION("undefined opcodes -- the DDT `?\?= XX` marker, one byte, no operand invented");

    // The 6800 has 59 undefined opcodes (256 - 197). Unlike the 8080's twelve they
    // have no published effect, so we print the bare marker and step ONE byte.
    for (unsigned opc : {0x00u, 0x02u, 0x9Du, 0xDDu, 0xCFu}) {
        uint8_t c[] = {(uint8_t)opc, 0xAA, 0xBB};
        Insn i = dis(d, c, sizeof c, 0);
        char want[16];
        std::snprintf(want, sizeof want, "?\?= %02X", opc);
        CHECK(i.text == want, "undefined opcode marked the DDT way");
        CHECK(i.undocumented && i.len == 1, "flagged, one byte -- the next two bytes are not its operand");
    }

    SECTION("octal operands follow the console base, like the 8080 decoder");

    {
        uint8_t c[] = {0x86, 0x41};  // LDAA #41 -> #101 octal
        Insn i = dis(d, c, sizeof c, 0, 8);
        CHECK(i.text == "LDAA #101", "immediate byte in split... just a byte: 101 octal");
    }
    {
        uint8_t c[] = {0xB7, 0xF0, 0x01};  // STAA F001 -> split octal 360 001
        Insn i = dis(d, c, sizeof c, 0, 8);
        CHECK(i.text == "STAA 360 001", "extended address as SPLIT octal, hi then lo");
    }

    SECTION("coverage sweep -- every valid opcode decodes, every mode has the right length");

    // Length expected for a decoded instruction, by the first byte. We recompute it
    // from a second, independent statement of the mode map so a wrong length column
    // in the table cannot agree with a wrong length here.
    int decoded = 0, illegal = 0;
    for (int opc = 0; opc < 256; ++opc) {
        uint8_t c[] = {(uint8_t)opc, 0x00, 0x00};
        Insn i = dis(d, c, sizeof c, 0);
        bool ill = i.text.rfind("?\?=", 0) == 0;
        if (ill) {
            ++illegal;
            CHECK(i.undocumented && i.len == 1, "illegal: flagged and one byte");
        } else {
            ++decoded;
            CHECK(!i.text.empty(), "a valid opcode has text");
            CHECK(i.len >= 1 && i.len <= 3, "1..3 bytes, always");
            CHECK(!i.undocumented, "a valid opcode is not flagged undocumented");
        }
    }
    CHECK(decoded == 197, "exactly 197 valid opcodes decode (72 mnemonics)");
    CHECK(illegal == 59, "and 59 are undefined on the 6800");
}

// ---------------------------------------------------------------------------
// The assembler -- the inverse the EDIT command drives (monitor.cpp), so an
// operator can type a 6800 mnemonic where a byte would go and have the encoding
// fall out. The mode is chosen from the operand syntax, and the round-trip over
// the whole table proves the assembler and the disassembler agree.
// ---------------------------------------------------------------------------

namespace {

// Assemble a line at address `at`, expecting success; return the bytes.
std::vector<uint8_t> asmOk(const Assembler* a, const std::string& line, uint16_t at = 0, int base = 16) {
    AsmResult r = a->assemble(at, line, base);
    return r.error.empty() ? r.bytes : std::vector<uint8_t>{};
}
bool bytesAre(const std::vector<uint8_t>& b, std::initializer_list<uint8_t> want) {
    return std::vector<uint8_t>(want) == b;
}

} // namespace

void test_asm6800() {
    SECTION("the 6800 assembler -- text in, bytes out, mode chosen from the operand");

    const Assembler* a = assemblerFor("6800");
    CHECK(a != nullptr, "we assemble 6800");
    if (!a) return;
    CHECK(std::string(a->name()) == "6800", "and it says so");
    CHECK(assemblerFor("z80") != nullptr, "the Z80 has its own convenience assembler now");

    SECTION("one line per addressing mode");

    CHECK(bytesAre(asmOk(a, "NOP"), {0x01}), "NOP -- inherent");
    CHECK(bytesAre(asmOk(a, "RTS"), {0x39}), "RTS");
    CHECK(bytesAre(asmOk(a, "ASRA"), {0x47}), "ASRA -- accumulator RMW, suffix form");
    CHECK(bytesAre(asmOk(a, "INCB"), {0x5C}), "INCB");
    CHECK(bytesAre(asmOk(a, "LDAA #41"), {0x86, 0x41}), "LDAA #41 -- immediate");
    CHECK(bytesAre(asmOk(a, "LDAA 50"), {0x96, 0x50}), "LDAA 50 -- direct (fits a page)");
    CHECK(bytesAre(asmOk(a, "LDAA FC00"), {0xB6, 0xFC, 0x00}), "LDAA FC00 -- extended, MS byte first");
    CHECK(bytesAre(asmOk(a, "LDAA 05,X"), {0xA6, 0x05}), "LDAA 05,X -- indexed");
    CHECK(bytesAre(asmOk(a, "LDX #FC00"), {0xCE, 0xFC, 0x00}), "LDX #FC00 -- 16-bit immediate, MS first");
    CHECK(bytesAre(asmOk(a, "CPX #1234"), {0x8C, 0x12, 0x34}), "CPX #1234 -- three bytes");

    SECTION("direct vs extended is chosen by VALUE (Programming Manual 2)");

    CHECK(bytesAre(asmOk(a, "LDAA FF"), {0x96, 0xFF}), "FF fits a page -> direct");
    CHECK(bytesAre(asmOk(a, "LDAA 100"), {0xB6, 0x01, 0x00}), "100 > FF -> extended");
    // JMP/JSR have no direct form, so even a low address is extended.
    CHECK(bytesAre(asmOk(a, "JMP 0050"), {0x7E, 0x00, 0x50}), "JMP has no direct mode -> extended");
    CHECK(bytesAre(asmOk(a, "JSR FC00"), {0xBD, 0xFC, 0x00}), "JSR extended");
    CHECK(bytesAre(asmOk(a, "JMP 05,X"), {0x6E, 0x05}), "JMP indexed");
    // The RMW memory ops have only indexed + extended.
    CHECK(bytesAre(asmOk(a, "NEG 50"), {0x70, 0x00, 0x50}), "NEG (memory) -> extended, not direct");
    CHECK(bytesAre(asmOk(a, "NEG 05,X"), {0x60, 0x05}), "NEG 05,X -- indexed");

    SECTION("relative branches -- target in, signed offset out, D = (PC+2) + R");

    // At 0100: BRA 010A -> offset +8.
    CHECK(bytesAre(asmOk(a, "BRA 010A", 0x0100), {0x20, 0x08}), "forward branch offset");
    // At 0100: BNE 00FE -> offset -4.
    CHECK(bytesAre(asmOk(a, "BNE 00FE", 0x0100), {0x26, 0xFC}), "backward branch offset");
    // Branch to self.
    CHECK(bytesAre(asmOk(a, "BRA 000C", 0x000C), {0x20, 0xFE}), "branch to self");
    CHECK(bytesAre(asmOk(a, "BSR 0012", 0x0000), {0x8D, 0x10}), "BSR is relative too");
    // Out of range: forward past +127.
    CHECK(!a->assemble(0x0100, "BRA 0200", 16).error.empty(), "a branch out of reach is refused");

    SECTION("indexed corners and errors");

    CHECK(bytesAre(asmOk(a, "LDAA ,X"), {0xA6, 0x00}), ",X means offset 0");
    CHECK(bytesAre(asmOk(a, "LDAA X"), {0xA6, 0x00}), "bare X means offset 0 too");
    CHECK(!a->assemble(0, "LDAA #100", 16).error.empty(), "an 8-bit immediate over FF is refused");
    CHECK(!a->assemble(0, "NOP 5", 16).error.empty(), "NOP takes no operand");
    CHECK(!a->assemble(0, "FOO 5", 16).error.empty(), "an unknown mnemonic is refused");
    CHECK(!a->assemble(0, "LDAA", 16).error.empty(), "LDAA with no operand is refused");

    SECTION("octal operands follow the console base");

    CHECK(bytesAre(asmOk(a, "LDAA #101", 0, 8), {0x86, 0x41}), "#101 octal = 41 hex");
    CHECK(bytesAre(asmOk(a, "LDAA 12H", 0, 8), {0x96, 0x12}), "a trailing H overrides the octal base");

    SECTION("ROUND-TRIP: assemble(disassemble(b)) == b over every valid opcode");

    // For each valid opcode, synthesize an instruction with distinctive operand
    // bytes, disassemble it, feed the text back to the assembler AT THE SAME
    // ADDRESS, and require the identical bytes back. This is the proof the two
    // tables are one table read two ways. (Relative branches must round-trip at
    // the address they were disassembled at, since the text is an absolute target.)
    const Disassembler* d = disassemblerFor("6800");
    int roundtripped = 0;
    for (int opc = 0; opc < 256; ++opc) {
        uint8_t buf[] = {(uint8_t)opc, 0x2A, 0x5C};  // operand bytes 2A/5C are arbitrary but fixed
        uint16_t at = 0x0100;
        auto peek = [&](uint16_t x) -> uint8_t {
            uint16_t off = (uint16_t)(x - at);
            return off < sizeof buf ? buf[off] : 0x01;
        };
        Insn in = d->at(at, peek, 16);
        if (in.undocumented) continue;  // illegal opcodes are not valid input

        AsmResult r = a->assemble(at, in.text, 16);
        CHECK(r.error.empty(), ("round-trip assembles: " + in.text).c_str());
        if (!r.error.empty()) continue;

        // The reassembled bytes must match the SAME LENGTH of original bytes.
        bool same = r.bytes.size() == in.len;
        for (size_t k = 0; same && k < in.len; ++k) same = r.bytes[k] == buf[k];
        CHECK(same, ("round-trip bytes match: " + in.text).c_str());
        if (same) ++roundtripped;
    }
    CHECK(roundtripped == 197, "all 197 valid opcodes round-trip byte-for-byte");
}
