#include "test.h"

#include "isa/isa.h"

using namespace altair;

// THE Z80 DECODER IS STATELESS, exactly like the 8080's (test_isa.cpp). Not one
// line here builds a Machine, a Bus, or a Board -- it hands the decoder a lambda
// over a byte array. Same layer boundary (DESIGN.md 3.0.2), same reason
// `DISASM ... CPU=z80` will work against an image in RAM with no CPU card present.

namespace {
// A peek over a fixed array, 0x00 past the end. Each test builds its own `code`.
struct Mem {
    const uint8_t* code;
    size_t n;
    uint8_t operator()(uint16_t a) const { return a < n ? code[a] : 0x00; }
};
}  // namespace

void test_z80_isa() {
    SECTION("the Z80 instruction set -- a decoder of its own");

    const Disassembler* d = disassemblerFor("z80");
    CHECK(d != nullptr, "we speak z80");
    if (!d) return;
    CHECK(std::string(d->name()) == "z80", "and it says so");

    SECTION("the main table -- Zilog mnemonics, immediates low byte first");

    // LD BC,1234 ; LD B,42 ; JP C3C3 ; RST 38 ; HALT
    uint8_t main[] = {0x01, 0x34, 0x12,  // LD BC,1234
                      0x06, 0x42,        // LD B,42
                      0xC3, 0xC3, 0xC3,  // JP C3C3
                      0xFF,              // RST 38
                      0x76};             // HALT
    Mem mm{main, sizeof main};
    auto mp = [&](uint16_t a) { return mm(a); };

    Insn i0 = d->at(0, mp);
    CHECK(i0.text == "LD BC,1234", "LD BC, with the word read LOW BYTE FIRST");
    CHECK(i0.len == 3, "three bytes");
    CHECK(d->at(3, mp).text == "LD B,42" && d->at(3, mp).len == 2, "LD B,42 -- an immediate byte");
    CHECK(d->at(5, mp).text == "JP C3C3", "JP with a word operand");
    CHECK(d->at(8, mp).text == "RST 38", "FF is RST 38 -- Zilog's hex spelling, not 8080 RST 7");
    CHECK(d->at(9, mp).text == "HALT" && d->at(9, mp).len == 1, "76 is HALT");

    SECTION("relative jumps -- the destination, computed from the byte AFTER the offset");

    // At 0000: JR 05 -> target 0000+2+05 = 0007. At 0002: DJNZ FE -> 0002+2-2 = 0002.
    uint8_t rel[] = {0x18, 0x05,   // JR 0007
                     0x10, 0xFE};  // DJNZ 0002 (to itself)
    Mem rm{rel, sizeof rel};
    auto rp = [&](uint16_t a) { return rm(a); };
    CHECK(d->at(0, rp).text == "JR 0007", "JR e resolves to the absolute target, forward");
    CHECK(d->at(0, rp).len == 2, "two bytes");
    CHECK(d->at(2, rp).text == "DJNZ 0002", "DJNZ FE loops to itself -- e is relative to the NEXT insn");

    SECTION("CB -- rotates, shifts, and bit ops, with SLL marked undocumented");

    uint8_t cb[] = {0xCB, 0x00,   // RLC B
                    0xCB, 0x16,   // RL (HL)
                    0xCB, 0x30,   // SLL B   (undocumented)
                    0xCB, 0x7E,   // BIT 7,(HL)
                    0xCB, 0xC7};  // SET 0,A
    Mem cm{cb, sizeof cb};
    auto cp = [&](uint16_t a) { return cm(a); };
    CHECK(d->at(0, cp).text == "RLC B" && d->at(0, cp).len == 2, "CB 00 is RLC B, two bytes");
    CHECK(d->at(2, cp).text == "RL (HL)", "CB 16 is RL (HL)");
    Insn sll = d->at(4, cp);
    CHECK(sll.text == "*SLL B" && sll.undocumented, "CB 30 is SLL B -- undocumented, and starred");
    CHECK(d->at(6, cp).text == "BIT 7,(HL)", "CB 7E is BIT 7,(HL)");
    CHECK(d->at(8, cp).text == "SET 0,A", "CB C7 is SET 0,A");

    SECTION("ED -- block ops, 16-bit arithmetic, and the interrupt-mode/misc grid");

    uint8_t ed[] = {0xED, 0xB0,        // LDIR
                    0xED, 0x52,        // SBC HL,DE
                    0xED, 0x4A,        // ADC HL,BC
                    0xED, 0x43, 0x00, 0x80,  // LD (8000),BC
                    0xED, 0x5E,        // IM 2
                    0xED, 0x44,        // NEG
                    0xED, 0x4D,        // RETI
                    0xED, 0x57,        // LD A,I
                    0xED, 0x00};       // undefined -> *NOP
    Mem em{ed, sizeof ed};
    auto ep = [&](uint16_t a) { return em(a); };
    CHECK(d->at(0, ep).text == "LDIR" && d->at(0, ep).len == 2, "ED B0 is LDIR, two bytes");
    CHECK(d->at(2, ep).text == "SBC HL,DE", "ED 52 is SBC HL,DE");
    CHECK(d->at(4, ep).text == "ADC HL,BC", "ED 4A is ADC HL,BC");
    Insn ld = d->at(6, ep);
    CHECK(ld.text == "LD (8000),BC" && ld.len == 4, "ED 43 is LD (nn),BC -- four bytes, word operand");
    CHECK(d->at(10, ep).text == "IM 2", "ED 5E is IM 2");
    CHECK(d->at(12, ep).text == "NEG", "ED 44 is NEG (the documented one)");
    CHECK(d->at(14, ep).text == "RETI", "ED 4D is RETI");
    CHECK(d->at(16, ep).text == "LD A,I", "ED 57 is LD A,I");
    Insn ednop = d->at(18, ep);
    CHECK(ednop.text == "*NOP" && ednop.undocumented && ednop.len == 2,
          "an undefined ED opcode is a marked 2-byte NOP -- real silicon runs it as one");

    SECTION("the undocumented ED duplicates and aliases are marked");

    uint8_t eddup[] = {0xED, 0x4C,   // NEG (undoc dup of 44)
                       0xED, 0x63, 0x00, 0x90,  // LD (9000),HL (undoc dup of 22)
                       0xED, 0x70,   // IN (C)  (flags only, undoc)
                       0xED, 0x71};  // OUT (C),0 (undoc)
    Mem edm{eddup, sizeof eddup};
    auto edp = [&](uint16_t a) { return edm(a); };
    CHECK(d->at(0, edp).text == "*NEG" && d->at(0, edp).undocumented, "ED 4C is a starred NEG dup");
    CHECK(d->at(2, edp).text == "*LD (9000),HL" && d->at(2, edp).len == 4,
          "ED 63 duplicates LD (nn),HL and is marked");
    CHECK(d->at(6, edp).text == "*IN (C)", "ED 70 is the flags-only IN (C), undocumented");
    CHECK(d->at(8, edp).text == "*OUT (C),0", "ED 71 outputs zero, undocumented");

    SECTION("DD/FD -- HL becomes IX/IY, and (HL) grows a signed displacement");

    uint8_t ix[] = {0xDD, 0x21, 0x00, 0x40,  // LD IX,4000
                    0xDD, 0x7E, 0x05,        // LD A,(IX+05)
                    0xDD, 0x70, 0xFB,        // LD (IX-05),B
                    0xDD, 0x36, 0x02, 0x99,  // LD (IX+02),99
                    0xDD, 0x09,              // ADD IX,BC
                    0xDD, 0xE1};             // POP IX
    Mem im{ix, sizeof ix};
    auto ip = [&](uint16_t a) { return im(a); };
    Insn ldix = d->at(0, ip);
    CHECK(ldix.text == "LD IX,4000" && ldix.len == 4, "DD 21 is LD IX,nn -- four bytes");
    Insn pos = d->at(4, ip);
    CHECK(pos.text == "LD A,(IX+05)" && pos.len == 3, "a positive displacement prints +05");
    Insn neg = d->at(7, ip);
    CHECK(neg.text == "LD (IX-05),B", "FB is -5, and it prints as -05");
    Insn ldn = d->at(10, ip);
    CHECK(ldn.text == "LD (IX+02),99" && ldn.len == 4,
          "LD (IX+d),n carries BOTH a displacement AND an immediate -- the whole reason for a %-loop");
    CHECK(d->at(14, ip).text == "ADD IX,BC", "DD 09 is ADD IX,BC -- the 16-bit pair, documented");
    CHECK(d->at(16, ip).text == "POP IX", "DD E1 is POP IX");

    SECTION("FD is the same machinery aimed at IY");
    uint8_t iy[] = {0xFD, 0x34, 0x03};  // INC (IY+03)
    Mem ym{iy, sizeof iy};
    auto yp = [&](uint16_t a) { return ym(a); };
    CHECK(d->at(0, yp).text == "INC (IY+03)" && d->at(0, yp).len == 3, "FD 34 is INC (IY+d)");

    SECTION("the IXH/IXL half registers are undocumented, and prefix-ignoring opcodes stay put");

    uint8_t half[] = {0xDD, 0x24,   // INC IXH  (undoc)
                      0xDD, 0x44,   // LD B,IXH (undoc)
                      0xDD, 0xEB,   // EX DE,HL (DD ignored)
                      0xDD, 0xE9,   // JP (IX)  (no displacement!)
                      0xDD, 0x66, 0x01};  // LD H,(IX+01) -- H stays H, memory becomes (IX+d)
    Mem hm{half, sizeof half};
    auto hp = [&](uint16_t a) { return hm(a); };
    Insn inxh = d->at(0, hp);
    CHECK(inxh.text == "*INC IXH" && inxh.undocumented, "DD 24 is INC IXH -- undocumented half register");
    CHECK(d->at(2, hp).text == "*LD B,IXH", "DD 44 is LD B,IXH -- also undocumented");
    Insn exdehl = d->at(4, hp);
    CHECK(exdehl.text == "EX DE,HL" && exdehl.len == 2,
          "DD EB is EX DE,HL -- the prefix is wasted, HL is NOT touched");
    Insn jpix = d->at(6, hp);
    CHECK(jpix.text == "JP (IX)" && jpix.len == 2,
          "DD E9 is JP (IX) with NO displacement -- it loads PC from IX, it does not read memory");
    CHECK(d->at(8, hp).text == "LD H,(IX+01)",
          "LD H,(IX+d): the (HL) memory operand indexes, but the H register stays H");

    SECTION("DD CB dd op -- four bytes, displacement BEFORE the opcode, undoc reg-copy forms");

    uint8_t ddcb[] = {0xDD, 0xCB, 0x04, 0x06,   // RLC (IX+04)
                      0xDD, 0xCB, 0xFC, 0x46,   // BIT 0,(IX-04)
                      0xDD, 0xCB, 0x02, 0x00};  // RLC (IX+02),B  (undocumented reg copy)
    Mem dm{ddcb, sizeof ddcb};
    auto dp = [&](uint16_t a) { return dm(a); };
    Insn r = d->at(0, dp);
    CHECK(r.text == "RLC (IX+04)" && r.len == 4, "DD CB 04 06 is RLC (IX+04), four bytes");
    CHECK(d->at(4, dp).text == "BIT 0,(IX-04)", "the displacement is signed and comes before the opcode");
    Insn copy = d->at(8, dp);
    CHECK(copy.text == "*RLC (IX+02),B" && copy.undocumented,
          "a non-6 register field copies the result into a register -- undocumented");

    SECTION("a stray index prefix is a wasted byte, and the decoder must not desync on it");

    // DD FD 00 : the DD is re-primed away by the FD; it disassembles as a lone
    // 1-byte NOP so a caller stepping by `len` lands on the FD and decodes it fresh.
    uint8_t stray[] = {0xDD, 0xFD, 0x00};
    Mem strm{stray, sizeof stray};
    auto sp = [&](uint16_t a) { return strm(a); };
    Insn st = d->at(0, sp);
    CHECK(st.text == "*NOP" && st.len == 1,
          "DD before another prefix is one wasted byte -- only the last prefix counts");

    SECTION("reading past the top of memory WRAPS, exactly like the address bus");

    auto ff = [](uint16_t) -> uint8_t { return 0xC3; };  // JP everywhere
    Insn top = d->at(0xFFFF, ff);
    CHECK(top.text == "JP C3C3", "an instruction at FFFF reads its operand from 0000 -- it WRAPS");

    // -----------------------------------------------------------------------
    // THE CONVENIENCE ASSEMBLER -- the decoder read the other way. Same layer,
    // same statelessness: text in, bytes out, no CPU.
    // -----------------------------------------------------------------------
    SECTION("the assembler round-trips the documented main/CB/ED forms -- disassemble, reassemble, same bytes");

    const Assembler* a = assemblerFor("z80");
    CHECK(a != nullptr, "we assemble z80");
    if (!a) return;
    CHECK(std::string(a->name()) == "z80", "and it says so");
    CHECK(assemblerFor("z80") == assemblerFor("z80"), "the same one every time");

    auto headOf = [](const std::string& s) {
        return s.substr(0, s.find(' '));
    };

    // MAIN table: lay each opcode down with NON-PALINDROMIC operand bytes (34 12,
    // so a low/high transposition can't hide), disassemble, feed the text back and
    // demand the same bytes. The four prefix bytes are not main instructions; the
    // JR/DJNZ (relative) forms are EXCLUDED and must come back as an error.
    for (int op = 0; op < 256; ++op) {
        if (op == 0xCB || op == 0xDD || op == 0xED || op == 0xFD) continue;
        uint8_t win[3] = {(uint8_t)op, 0x34, 0x12};
        auto wp = [&](uint16_t x) -> uint8_t { return x < 3 ? win[x] : 0x00; };
        Insn in = d->at(0, wp);
        AsmResult ra = a->assemble(0, in.text);
        if (headOf(in.text) == "JR" || headOf(in.text) == "DJNZ") {
            CHECK(!ra.error.empty() && ra.bytes.empty(),
                  ("a relative jump is not implemented: " + in.text).c_str());
            continue;
        }
        bool ok = ra.error.empty() && ra.bytes.size() == in.len && ra.bytes[0] == (uint8_t)op;
        if (ok && in.len >= 2) ok = ra.bytes[1] == win[1];
        if (ok && in.len == 3) ok = ra.bytes[2] == win[2];
        CHECK(ok, ("main round-trips: " + in.text).c_str());
    }

    // CB page: two bytes, no immediate. Documented forms round-trip to {CB, op};
    // the undocumented SLL and reg-copy forms (the disassembler's `*` text) must
    // NOT assemble back.
    for (int op = 0; op < 256; ++op) {
        uint8_t win[2] = {0xCB, (uint8_t)op};
        auto wp = [&](uint16_t x) -> uint8_t { return x < 2 ? win[x] : 0x00; };
        Insn in = d->at(0, wp);
        AsmResult ra = a->assemble(0, in.text);
        if (in.undocumented) {
            CHECK(!ra.error.empty(), ("undoc CB does not assemble: " + in.text).c_str());
            continue;
        }
        bool ok = ra.error.empty() && ra.bytes.size() == 2 && ra.bytes[0] == 0xCB && ra.bytes[1] == (uint8_t)op;
        CHECK(ok, ("CB round-trips: " + in.text).c_str());
    }

    // ED page: {ED, op} for the operand-less forms, plus a low-first word for the
    // LD (nn),ss / LD ss,(nn) family (len 4). Undocumented ED must not round-trip.
    for (int op = 0; op < 256; ++op) {
        uint8_t win[4] = {0xED, (uint8_t)op, 0x34, 0x12};
        auto wp = [&](uint16_t x) -> uint8_t { return x < 4 ? win[x] : 0x00; };
        Insn in = d->at(0, wp);
        AsmResult ra = a->assemble(0, in.text);
        if (in.undocumented) {
            CHECK(!ra.error.empty(), ("undoc ED does not assemble: " + in.text).c_str());
            continue;
        }
        bool ok = ra.error.empty() && ra.bytes.size() == in.len &&
                  ra.bytes[0] == 0xED && ra.bytes[1] == (uint8_t)op;
        if (ok && in.len == 4) ok = ra.bytes[2] == win[2] && ra.bytes[3] == win[3];
        CHECK(ok, ("ED round-trips: " + in.text).c_str());
    }

    SECTION("the assembler -- spot encodings, leniency, octal, and honest errors");

    auto asmBytes = [&](const std::string& line, int base = 16) {
        return a->assemble(0, line, base).bytes;
    };
    auto eq = [](const std::vector<uint8_t>& got, std::vector<uint8_t> want) { return got == want; };

    CHECK(eq(asmBytes("LD B,C"), {0x41}), "LD B,C -> 41, a one-byte register move");
    CHECK(eq(asmBytes("LD HL,1234"), {0x21, 0x34, 0x12}), "LD HL,1234 -> 21 34 12, operand LOW BYTE FIRST");
    CHECK(eq(asmBytes("JP 0100"), {0xC3, 0x00, 0x01}), "JP 0100 -> C3 00 01");
    CHECK(eq(asmBytes("LD (0100),HL"), {0x22, 0x00, 0x01}), "LD (0100),HL -> 22 00 01, a MID-string word operand");
    CHECK(eq(asmBytes("IN A,(10)"), {0xDB, 0x10}), "IN A,(10) -> DB 10");
    CHECK(eq(asmBytes("OUT (10),A"), {0xD3, 0x10}), "OUT (10),A -> D3 10");
    CHECK(eq(asmBytes("RST 08"), {0xCF}), "RST 08 -> CF, exact-mapped, its hex digits not parsed as a number");
    CHECK(eq(asmBytes("HALT"), {0x76}), "HALT -> 76");
    CHECK(eq(asmBytes("EX DE,HL"), {0xEB}), "EX DE,HL -> EB");
    CHECK(eq(asmBytes("LDIR"), {0xED, 0xB0}), "LDIR -> ED B0, an ED-page block op");
    CHECK(eq(asmBytes("NEG"), {0xED, 0x44}), "NEG -> ED 44");
    CHECK(eq(asmBytes("IM 1"), {0xED, 0x56}), "IM 1 -> ED 56, its mode digit is fixed text");
    CHECK(eq(asmBytes("SBC HL,BC"), {0xED, 0x42}), "SBC HL,BC -> ED 42");
    CHECK(eq(asmBytes("IN A,(C)"), {0xED, 0x78}), "IN A,(C) -> ED 78, distinct from IN A,(n)");
    CHECK(eq(asmBytes("LD BC,(0100)"), {0xED, 0x4B, 0x00, 0x01}), "LD BC,(0100) -> ED 4B 00 01, ED word form");
    CHECK(eq(asmBytes("BIT 7,A"), {0xCB, 0x7F}), "BIT 7,A -> CB 7F");
    CHECK(eq(asmBytes("RES 0,(HL)"), {0xCB, 0x86}), "RES 0,(HL) -> CB 86");
    CHECK(eq(asmBytes("SET 3,C"), {0xCB, 0xD9}), "SET 3,C -> CB D9");

    // Leniency: case, spacing and an explicit suffix all reach the same bytes.
    CHECK(eq(asmBytes("ld b,c"), {0x41}), "lowercase assembles");
    CHECK(eq(asmBytes("LD  HL , 1234"), {0x21, 0x34, 0x12}), "extra spaces and spaces around the comma are forgiven");
    CHECK(eq(asmBytes("IN A,(10H)"), {0xDB, 0x10}), "a trailing H suffix is honored");
    CHECK(eq(asmBytes("LD B,0x42"), {0x06, 0x42}), "and a 0x prefix");

    // Octal: base 8 changes ONLY how a bare operand is read.
    CHECK(eq(asmBytes("LD B,377", 8), {0x06, 0xFF}), "base 8: 377 is FF");
    CHECK(eq(asmBytes("LD B,377Q", 16), {0x06, 0xFF}), "a Q suffix forces octal even in hex mode");

    // The excluded families get a DISTINCT "not implemented" message -- so you know
    // the mnemonic is real and why it was refused -- while garbage stays "unknown".
    auto notImpl = [&](const std::string& line) {
        AsmResult rr = a->assemble(0, line);
        return rr.bytes.empty() && rr.error.find("not implemented") != std::string::npos;
    };
    CHECK(notImpl("JR 0100"), "JR is not implemented -- a relative branch");
    CHECK(notImpl("DJNZ 0100"), "DJNZ is not implemented -- a relative branch");
    CHECK(notImpl("LD A,(IX+5)"), "an (IX+d) indexed load is not implemented");
    CHECK(notImpl("ADD A,IXH"), "the IXH half register is not implemented");
    CHECK(notImpl("LD IX,1234"), "LD IX,nn is not implemented");
    CHECK(notImpl("LD B,(IY+2)"), "an (IY+d) indexed load is not implemented");

    auto errs = [&](const std::string& line, int base = 16) {
        AsmResult rr = a->assemble(0, line, base);
        return rr.bytes.empty() && !rr.error.empty();
    };
    CHECK(errs("FOO 1") && a->assemble(0, "FOO 1").error.find("unknown") != std::string::npos,
          "genuine garbage is 'unknown instruction', not 'not implemented'");
    CHECK(errs("JP"), "a word instruction with no operand errors");
    CHECK(errs("LD B,1FF"), "a byte operand over FF errors");
    CHECK(errs(""), "an empty line errors");
    CHECK(errs("*RLC B"), "the disassembler's undocumented '*' marker is not valid input");
}
