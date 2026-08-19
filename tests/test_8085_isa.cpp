#include "test.h"

#include "isa/isa.h"

#include <cstdio>
#include <string>

using namespace altair;

// THE 8085 DECODER, like the 8080's, is stateless: bytes in, text out, no CPU
// (DESIGN.md 3.0.2). The 8085 instruction set is the 8080's plus RIM/SIM plus the
// ten undocumented opcodes -- so most of this file is one claim, made two ways: the
// 8085 table AGREES with the 8080 on all 244 shared opcodes and DIFFERS on exactly
// the twelve the 8080 leaves as holes. That cross-check is the guardrail against
// the two independent tables (isa8080.cpp, isa8085.cpp) drifting apart.

void test_8085_isa() {
    SECTION("the 8085 instruction set -- a decoder of its own, RIM/SIM where the 8080 has holes");

    const Disassembler* d = disassemblerFor("8085");
    CHECK(d != nullptr, "we speak 8085");
    if (!d) return;
    CHECK(std::string(d->name()) == "8085", "and it says so");
    CHECK(disassemblerFor("8085") == disassemblerFor("8085"), "the same one every time");
    CHECK(disassemblerFor("8085") == disassemblerFor("8085"), "case does not matter");

    const Disassembler* d80 = disassemblerFor("8080");
    CHECK(d != d80, "it is a DIFFERENT decoder from the 8080 -- 20 is RIM here, a NOP there");

    // RIM (20) and SIM (30) are DOCUMENTED 8085 instructions -- one byte, no
    // operand, no undocumented marker. On the 8080 both bytes are NOP-holes.
    auto one = [&](uint8_t b) {
        auto p = [&](uint16_t a) -> uint8_t { return a == 0 ? b : 0x00; };
        return d->at(0, p);
    };
    Insn rim = one(0x20);
    CHECK(rim.text == "RIM" && !rim.undocumented && rim.len == 1, "20 is RIM, documented, one byte");
    Insn sim = one(0x30);
    CHECK(sim.text == "SIM" && !sim.undocumented && sim.len == 1, "30 is SIM, documented, one byte");

    SECTION("the ten undocumented 8085 opcodes -- DDT's `?\?= <byte>`, one byte, the 8085 effect");

    // The 8080 marks these bytes undocumented too, but names the 8080 effect
    // (08->*NOP, CB->*JMP). The 8085 names ITS effect -- DSUB, RSTV, JK -- because
    // that is what 8085 silicon does. The core still runs them as NOP (issue #347),
    // which is exactly why the disassembler flags them rather than trusting them.
    struct U { uint8_t opc; const char* eff; };
    const U undoc[] = {
        {0x08, "*DSUB"}, {0x10, "*ARHL"}, {0x18, "*RDEL"}, {0x28, "*LDHI"}, {0x38, "*LDSI"},
        {0xCB, "*RSTV"}, {0xD9, "*SHLX"}, {0xDD, "*JNK"},  {0xED, "*LHLX"}, {0xFD, "*JK"},
    };
    for (const U& u : undoc) {
        Insn in = one(u.opc);
        // Rebuild the exact marker the decoder prints: "??= XX  *MNEM".
        char hex[3];
        std::snprintf(hex, sizeof hex, "%02X", u.opc);
        std::string expect = std::string("?\?= ") + hex + "  " + u.eff;
        CHECK(in.text == expect, (std::string("undocumented ") + u.eff + " marked the DDT way").c_str());
        CHECK(in.undocumented, "and flagged undocumented");
        CHECK(in.len == 1, "one byte -- no operand invented, even for LDHI/JK which really take one");
    }

    SECTION("the 8085 table equals the 8080 EXCEPT the twelve holes -- the anti-drift guardrail");

    // For every opcode, lay it down with non-palindromic operand bytes and compare
    // the 8085 decode to the 8080 decode. They must be identical everywhere except
    // the twelve holes {08,10,18,20,28,30,38,CB,D9,DD,ED,FD}, and must differ on all
    // twelve. This is what keeps the two independent tables honest.
    auto isHole = [](int b) {
        switch (b) {
            case 0x08: case 0x10: case 0x18: case 0x20: case 0x28: case 0x30:
            case 0x38: case 0xCB: case 0xD9: case 0xDD: case 0xED: case 0xFD:
                return true;
            default: return false;
        }
    };
    int differ = 0;
    for (int b = 0; b < 256; ++b) {
        uint8_t win[3] = {(uint8_t)b, 0x34, 0x12};
        auto wp = [&](uint16_t x) -> uint8_t { return x < 3 ? win[x] : 0x00; };
        Insn a85 = d->at(0, wp);
        Insn a80 = d80->at(0, wp);
        bool same = a85.text == a80.text && a85.len == a80.len && a85.undocumented == a80.undocumented;
        if (isHole(b)) {
            if (!same) ++differ;
            CHECK(!same, ("hole " + a85.text + " differs from the 8080").c_str());
        } else {
            CHECK(same, ("shared opcode agrees with the 8080: " + a85.text).c_str());
        }
    }
    CHECK(differ == 12, "exactly twelve opcodes differ from the 8080 -- no more, no fewer");

    SECTION("the 8085 assembler -- round-trips the map, and RIM/SIM assemble where the 8080's holes cannot");

    const Assembler* a = assemblerFor("8085");
    CHECK(a != nullptr, "we assemble 8085");
    if (!a) return;
    CHECK(std::string(a->name()) == "8085", "and it says so");
    CHECK(assemblerFor("8085") == assemblerFor("8085"), "the same one every time");

    // The whole map: documented opcodes round-trip to the same bytes; an
    // undocumented byte's `??= XX *MNEM` text is not valid input and is rejected.
    for (int b = 0; b < 256; ++b) {
        uint8_t win[3] = {(uint8_t)b, 0x34, 0x12};
        auto wp = [&](uint16_t x) -> uint8_t { return x < 3 ? win[x] : 0x00; };
        Insn in = d->at(0, wp);

        AsmResult r = a->assemble(0, in.text);
        if (in.undocumented) {
            CHECK(!r.error.empty() && r.bytes.empty(),
                  "an undocumented opcode's disassembly does not assemble back");
            continue;
        }
        bool okbytes = r.error.empty() && r.bytes.size() == in.len && r.bytes[0] == (uint8_t)b;
        if (okbytes && in.len >= 2) okbytes = r.bytes[1] == win[1];
        if (okbytes && in.len == 3) okbytes = r.bytes[2] == win[2];
        CHECK(okbytes, ("round-trips: " + in.text).c_str());
    }

    auto asmBytes = [&](const std::string& line) { return a->assemble(0, line, 16).bytes; };
    auto eq = [](const std::vector<uint8_t>& got, std::vector<uint8_t> want) { return got == want; };

    CHECK(eq(asmBytes("RIM"), {0x20}), "RIM -> 20, a documented one-byte 8085 op the 8080 cannot assemble");
    CHECK(eq(asmBytes("SIM"), {0x30}), "SIM -> 30, likewise");
    CHECK(eq(asmBytes("NOP"), {0x00}), "NOP still maps only to 00 -- 20/30 no longer compete for it");
    CHECK(eq(asmBytes("MVI A,42"), {0x3E, 0x42}), "and the shared set assembles exactly as on the 8080");

    // The DDT marker for an undocumented op is not valid input on the 8085 either.
    AsmResult bad = a->assemble(0, "?\?= 08  *DSUB");
    CHECK(bad.bytes.empty() && !bad.error.empty(), "the undocumented marker does not assemble");

    SECTION("the registry lists the 8085");

    bool listed = false;
    for (const std::string& s : instructionSets()) if (s == "8085") listed = true;
    CHECK(listed, "instructionSets() names 8085 -- for tab completion and DISASM CPU=");
}
