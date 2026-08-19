#include "isa/isa.h"

#include <cassert>
#include <cctype>
#include <cstdio>
#include <unordered_map>

namespace altair {
namespace {

// ---------------------------------------------------------------------------
// The 8085 opcode table. 256 entries, in order, with the holes filled in.
//
// THE 8085 IS AN 8080 SUPERSET, and this table is the 8080's (isa8080.cpp) with
// exactly TWELVE slots changed -- the same twelve the 8080 leaves undocumented.
// Two of them are DOCUMENTED 8085 instructions and decode as themselves; the
// other ten are the 8085's *undocumented* opcodes and are marked as such:
//
//   20  RIM      documented -- read the interrupt mask and the SID serial pin
//   30  SIM      documented -- set the interrupt mask and the SOD serial pin
//   08  DSUB     undocumented -- HL -= BC
//   10  ARHL     undocumented -- arithmetic shift right of HL
//   18  RDEL     undocumented -- rotate DE left through carry
//   28  LDHI d8  undocumented -- DE = HL + imm8
//   38  LDSI d8  undocumented -- DE = SP + imm8
//   CB  RSTV     undocumented -- RST to 0040 if the V (overflow) flag is set
//   D9  SHLX     undocumented -- store HL at (DE)
//   DD  JNK a16  undocumented -- jump if the K (X5) flag is reset
//   ED  LHLX     undocumented -- load HL from (DE)
//   FD  JK  a16  undocumented -- jump if the K (X5) flag is set
//
// This file is INDEPENDENT of isa8080.cpp on purpose -- the same call isaZ80.cpp
// and isa6800.cpp made. A decoder shares nothing so the two tables cannot drift
// into agreement on a bug; a test (test_8085_isa.cpp) instead PROVES the intended
// relationship -- identical to the 8080 on all 244 shared opcodes, differing on
// exactly these twelve.
//
// DOCUMENTED vs the CORE. The core (src/cpu/cpu8085.cpp) executes RIM and SIM for
// real but still runs the ten undocumented slots as NOP -- their faithful ALU and
// the V/K flags are deferred to issue #347, gated on a genuine 8085 exerciser. So
// the ten are marked `undoc` here and print DDT-style, exactly as the 8080's holes
// do: `?\?= <byte>  *MNEM`, one byte, no operand invented. RIM/SIM, being real on
// both the silicon and this core, decode as ordinary one-byte instructions.
//
// `%B` is an immediate byte, `%W` an immediate word (low byte first). See
// isa8080.cpp for the fuller account of the table format and the undoc handling.
// ---------------------------------------------------------------------------
struct Op {
    const char* text;
    bool undoc;
};

// clang-format off
static const Op kOps[256] = {
/* 00 */ {"NOP",false},        {"LXI B,%W",false},   {"STAX B",false},   {"INX B",false},
/* 04 */ {"INR B",false},      {"DCR B",false},      {"MVI B,%B",false}, {"RLC",false},
/* 08 */ {"DSUB",true},        {"DAD B",false},      {"LDAX B",false},   {"DCX B",false},
/* 0C */ {"INR C",false},      {"DCR C",false},      {"MVI C,%B",false}, {"RRC",false},
/* 10 */ {"ARHL",true},        {"LXI D,%W",false},   {"STAX D",false},   {"INX D",false},
/* 14 */ {"INR D",false},      {"DCR D",false},      {"MVI D,%B",false}, {"RAL",false},
/* 18 */ {"RDEL",true},        {"DAD D",false},      {"LDAX D",false},   {"DCX D",false},
/* 1C */ {"INR E",false},      {"DCR E",false},      {"MVI E,%B",false}, {"RAR",false},
/* 20 */ {"RIM",false},        {"LXI H,%W",false},   {"SHLD %W",false},  {"INX H",false},
/* 24 */ {"INR H",false},      {"DCR H",false},      {"MVI H,%B",false}, {"DAA",false},
/* 28 */ {"LDHI",true},        {"DAD H",false},      {"LHLD %W",false},  {"DCX H",false},
/* 2C */ {"INR L",false},      {"DCR L",false},      {"MVI L,%B",false}, {"CMA",false},
/* 30 */ {"SIM",false},        {"LXI SP,%W",false},  {"STA %W",false},   {"INX SP",false},
/* 34 */ {"INR M",false},      {"DCR M",false},      {"MVI M,%B",false}, {"STC",false},
/* 38 */ {"LDSI",true},        {"DAD SP",false},     {"LDA %W",false},   {"DCX SP",false},
/* 3C */ {"INR A",false},      {"DCR A",false},      {"MVI A,%B",false}, {"CMC",false},

/* 40 */ {"MOV B,B",false},    {"MOV B,C",false},    {"MOV B,D",false},  {"MOV B,E",false},
/* 44 */ {"MOV B,H",false},    {"MOV B,L",false},    {"MOV B,M",false},  {"MOV B,A",false},
/* 48 */ {"MOV C,B",false},    {"MOV C,C",false},    {"MOV C,D",false},  {"MOV C,E",false},
/* 4C */ {"MOV C,H",false},    {"MOV C,L",false},    {"MOV C,M",false},  {"MOV C,A",false},
/* 50 */ {"MOV D,B",false},    {"MOV D,C",false},    {"MOV D,D",false},  {"MOV D,E",false},
/* 54 */ {"MOV D,H",false},    {"MOV D,L",false},    {"MOV D,M",false},  {"MOV D,A",false},
/* 58 */ {"MOV E,B",false},    {"MOV E,C",false},    {"MOV E,D",false},  {"MOV E,E",false},
/* 5C */ {"MOV E,H",false},    {"MOV E,L",false},    {"MOV E,M",false},  {"MOV E,A",false},
/* 60 */ {"MOV H,B",false},    {"MOV H,C",false},    {"MOV H,D",false},  {"MOV H,E",false},
/* 64 */ {"MOV H,H",false},    {"MOV H,L",false},    {"MOV H,M",false},  {"MOV H,A",false},
/* 68 */ {"MOV L,B",false},    {"MOV L,C",false},    {"MOV L,D",false},  {"MOV L,E",false},
/* 6C */ {"MOV L,H",false},    {"MOV L,L",false},    {"MOV L,M",false},  {"MOV L,A",false},
/* 70 */ {"MOV M,B",false},    {"MOV M,C",false},    {"MOV M,D",false},  {"MOV M,E",false},
/* 74 */ {"MOV M,H",false},    {"MOV M,L",false},    {"HLT",false},      {"MOV M,A",false},
/* 78 */ {"MOV A,B",false},    {"MOV A,C",false},    {"MOV A,D",false},  {"MOV A,E",false},
/* 7C */ {"MOV A,H",false},    {"MOV A,L",false},    {"MOV A,M",false},  {"MOV A,A",false},

/* 80 */ {"ADD B",false},      {"ADD C",false},      {"ADD D",false},    {"ADD E",false},
/* 84 */ {"ADD H",false},      {"ADD L",false},      {"ADD M",false},    {"ADD A",false},
/* 88 */ {"ADC B",false},      {"ADC C",false},      {"ADC D",false},    {"ADC E",false},
/* 8C */ {"ADC H",false},      {"ADC L",false},      {"ADC M",false},    {"ADC A",false},
/* 90 */ {"SUB B",false},      {"SUB C",false},      {"SUB D",false},    {"SUB E",false},
/* 94 */ {"SUB H",false},      {"SUB L",false},      {"SUB M",false},    {"SUB A",false},
/* 98 */ {"SBB B",false},      {"SBB C",false},      {"SBB D",false},    {"SBB E",false},
/* 9C */ {"SBB H",false},      {"SBB L",false},      {"SBB M",false},    {"SBB A",false},
/* A0 */ {"ANA B",false},      {"ANA C",false},      {"ANA D",false},    {"ANA E",false},
/* A4 */ {"ANA H",false},      {"ANA L",false},      {"ANA M",false},    {"ANA A",false},
/* A8 */ {"XRA B",false},      {"XRA C",false},      {"XRA D",false},    {"XRA E",false},
/* AC */ {"XRA H",false},      {"XRA L",false},      {"XRA M",false},    {"XRA A",false},
/* B0 */ {"ORA B",false},      {"ORA C",false},      {"ORA D",false},    {"ORA E",false},
/* B4 */ {"ORA H",false},      {"ORA L",false},      {"ORA M",false},    {"ORA A",false},
/* B8 */ {"CMP B",false},      {"CMP C",false},      {"CMP D",false},    {"CMP E",false},
/* BC */ {"CMP H",false},      {"CMP L",false},      {"CMP M",false},    {"CMP A",false},

/* C0 */ {"RNZ",false},        {"POP B",false},      {"JNZ %W",false},   {"JMP %W",false},
/* C4 */ {"CNZ %W",false},     {"PUSH B",false},     {"ADI %B",false},   {"RST 0",false},
/* C8 */ {"RZ",false},         {"RET",false},        {"JZ %W",false},    {"RSTV",true},
/* CC */ {"CZ %W",false},      {"CALL %W",false},    {"ACI %B",false},   {"RST 1",false},
/* D0 */ {"RNC",false},        {"POP D",false},      {"JNC %W",false},   {"OUT %B",false},
/* D4 */ {"CNC %W",false},     {"PUSH D",false},     {"SUI %B",false},   {"RST 2",false},
/* D8 */ {"RC",false},         {"SHLX",true},        {"JC %W",false},    {"IN %B",false},
/* DC */ {"CC %W",false},      {"JNK",true},         {"SBI %B",false},   {"RST 3",false},
/* E0 */ {"RPO",false},        {"POP H",false},      {"JPO %W",false},   {"XTHL",false},
/* E4 */ {"CPO %W",false},     {"PUSH H",false},     {"ANI %B",false},   {"RST 4",false},
/* E8 */ {"RPE",false},        {"PCHL",false},       {"JPE %W",false},   {"XCHG",false},
/* EC */ {"CPE %W",false},     {"LHLX",true},        {"XRI %B",false},   {"RST 5",false},
/* F0 */ {"RP",false},         {"POP PSW",false},    {"JP %W",false},    {"DI",false},
/* F4 */ {"CP %W",false},      {"PUSH PSW",false},   {"ORI %B",false},   {"RST 6",false},
/* F8 */ {"RM",false},         {"SPHL",false},       {"JM %W",false},    {"EI",false},
/* FC */ {"CM %W",false},      {"JK",true},          {"CPI %B",false},   {"RST 7",false},
};
// clang-format on

// An operand in the requested base -- same rule as the 8080 decoder's fmtNum, kept
// file-local because each decoder is a table plus these few helpers, and sharing
// them would tie the files together for no gain (isaZ80.cpp made the same call).
std::string fmtNum(unsigned v, int digits, int base) {
    char b[16];
    if (base == 8) {
        if (digits <= 2)
            std::snprintf(b, sizeof b, "%03o", v & 0xFF);
        else
            std::snprintf(b, sizeof b, "%03o %03o", (v >> 8) & 0xFF, v & 0xFF);
        return b;
    }
    static const char* d = "0123456789ABCDEF";
    std::string s;
    for (int i = digits - 1; i >= 0; --i) s += d[(v >> (i * 4)) & 0xF];
    return s;
}

class Isa8085 : public Disassembler {
public:
    const char* name() const override { return "8085"; }

    Insn at(uint16_t addr, const PeekFn& peek, int base) const override {
        uint8_t opc = peek(addr);
        const Op& op = kOps[opc];
        Insn in;
        in.undocumented = op.undoc;

        std::string t = op.text;

        // An undocumented opcode is ONE byte -- the way real DDT and SID step over
        // it (isa8080.cpp explains the reasoning at length). We print DDT's `??=
        // <byte>` marker plus the BARE mnemonic of the 8085 effect, reading no
        // operand. On the 8085 these are DSUB/ARHL/.../JK -- not the 8080's holes --
        // and the core still runs them as NOP, so the marker is doubly honest here.
        if (op.undoc) {
            std::string m = t.substr(0, t.find('%'));  // drop "%W"/"%B": there is no operand
            while (!m.empty() && m.back() == ' ') m.pop_back();
            in.len = 1;
            in.text = "?\?= " + fmtNum(opc, 2, base) + "  *" + m;
            return in;
        }

        size_t p = t.find('%');
        if (p == std::string::npos) {
            in.len = 1;  // includes RIM and SIM -- documented, one byte, no operand
        } else if (t[p + 1] == 'B') {
            in.len = 2;
            t = t.substr(0, p) + fmtNum(peek((uint16_t)(addr + 1)), 2, base) + t.substr(p + 2);
        } else {
            in.len = 3;
            unsigned w = peek((uint16_t)(addr + 1)) | (peek((uint16_t)(addr + 2)) << 8);
            in.operand = (uint16_t)w;  // the address a symbol can name
            in.operandBits = 16;
            t = t.substr(0, p) + fmtNum(w, 4, base) + t.substr(p + 2);
        }
        in.text = t;
        return in;
    }
};

const Isa8085 k8085;

// ---------------------------------------------------------------------------
// The 8085 assembler -- the table above read the other way, built exactly as the
// 8080's is (isa8080.cpp). The ten undocumented entries are SKIPPED, so the
// documented mnemonics stay a bijection; RIM and SIM, being documented, DO
// assemble -- to 20 and 30 respectively. NOP still maps only to 00.
// ---------------------------------------------------------------------------

std::string asmNormalize(const std::string& s) {
    std::string t;
    for (char c : s) t += (char)std::toupper((unsigned char)c);

    std::string out;
    bool pendingSpace = false;
    for (char c : t) {
        if (c == ' ' || c == '\t') {
            if (!out.empty()) pendingSpace = true;
            continue;
        }
        if (c == ',') {
            while (!out.empty() && out.back() == ' ') out.pop_back();
            out += ',';
            pendingSpace = false;
            continue;
        }
        if (pendingSpace) { out += ' '; pendingSpace = false; }
        out += c;
    }
    return out;
}

bool asmParseNum(const std::string& tok, int base, unsigned& out) {
    std::string s = tok;
    if (s.empty()) return false;

    if (s.size() > 2 && s[0] == '0' && (s[1] == 'X' || s[1] == 'x')) {
        base = 16;
        s = s.substr(2);
    } else {
        char last = (char)std::toupper((unsigned char)s.back());
        if (last == 'H') { base = 16; s.pop_back(); }
        else if (last == 'Q' || last == 'O') { base = 8; s.pop_back(); }
    }
    if (s.empty()) return false;

    unsigned long v = 0;
    for (char c : s) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else return false;
        if (d >= base) return false;
        v = v * base + (unsigned)d;
        if (v > 0xFFFF) return false;
    }
    out = (unsigned)v;
    return true;
}

class Isa8085Assembler : public Assembler {
public:
    Isa8085Assembler() {
        for (int i = 0; i < 256; ++i) {
            const Op& op = kOps[i];
            if (op.undoc) continue;  // documented copy only -- preserve the bijection
            std::string text = op.text;
            size_t p = text.find('%');
            if (p == std::string::npos) {
                bool ins = exact_.emplace(asmNormalize(text), (uint8_t)i).second;
                assert(ins && "duplicate exact mnemonic -- kOps is no longer a bijection");
                (void)ins;
            } else {
                bool word = text[p + 1] == 'W';
                std::string key = asmNormalize(text.substr(0, p));
                bool ins = prefix_.emplace(key, Enc{(uint8_t)i, word}).second;
                assert(ins && "duplicate operand mnemonic -- kOps is no longer a bijection");
                (void)ins;
            }
        }
    }

    const char* name() const override { return "8085"; }

    AsmResult assemble(uint16_t /*addr*/, const std::string& line, int base) const override {
        std::string n = asmNormalize(line);
        if (n.empty()) return fail("empty");

        auto e = exact_.find(n);
        if (e != exact_.end()) return ok({e->second});

        size_t sp = n.rfind(' ');
        size_t cm = n.rfind(',');
        size_t cut;
        if (sp == std::string::npos && cm == std::string::npos)
            return fail("unknown instruction: " + n);
        if (cm == std::string::npos) cut = sp;
        else if (sp == std::string::npos) cut = cm;
        else cut = sp > cm ? sp : cm;

        std::string operand = n.substr(cut + 1);
        std::string prefix = n.substr(0, n[cut] == ',' ? cut + 1 : cut);
        if (operand.empty()) return fail("missing operand");

        auto pr = prefix_.find(asmNormalize(prefix));
        if (pr == prefix_.end()) return fail("unknown instruction: " + n);

        unsigned v;
        if (!asmParseNum(operand, base, v)) return fail("bad operand: " + operand);

        const Enc& enc = pr->second;
        if (enc.word) {
            if (v > 0xFFFF) return fail("operand too large");
            return ok({enc.opcode, (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF)});
        }
        if (v > 0xFF) return fail("operand too large");
        return ok({enc.opcode, (uint8_t)v});
    }

private:
    struct Enc {
        uint8_t opcode;
        bool word;
    };

    static AsmResult ok(std::vector<uint8_t> b) { return {std::move(b), {}}; }
    static AsmResult fail(std::string e) { return {{}, std::move(e)}; }

    std::unordered_map<std::string, uint8_t> exact_;
    std::unordered_map<std::string, Enc> prefix_;
};

const Isa8085Assembler k8085asm;

} // namespace

// Registered through isa8080.cpp's disassemblerFor/assemblerFor/instructionSets,
// the one registry -- the same way isaZ80.cpp and isa6800.cpp hook in.
const Disassembler* isa8085Disassembler() { return &k8085; }
const Assembler* isa8085Assembler() { return &k8085asm; }

} // namespace altair
