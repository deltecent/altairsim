#include "isa/isa.h"

#include <cassert>
#include <cctype>
#include <cstdio>
#include <unordered_map>

namespace altair {
namespace {

// ---------------------------------------------------------------------------
// The 8080 opcode table. 256 entries, in order, with holes filled in.
//
// Source: Intel 8080 Assembly Language Programming Manual (1975) and the 8080
// Microcomputer Systems User's Manual -- the mnemonics are Intel's, so `MOV A,B`
// and `LXI H,` and `JNZ`, not the Zilog spelling. A listing printed in 1977 and
// a DISASM here read the same, which is the whole reason to use the period
// mnemonics rather than something more regular.
//
// `%B` is an immediate byte, `%W` an immediate word (low byte first, as the 8080
// stores it). The length falls out of which one appears -- there is no separate
// length column to disagree with the text.
//
// THE TWELVE UNDOCUMENTED OPCODES ARE HERE, AND THEY ARE NOT ERRORS. Real silicon
// executes 08/10/18/20/28/30/38 as NOP, CB as JMP, D9 as RET, and DD/ED/FD as
// CALL -- seven plus five, twelve in all. But a DISASSEMBLER cannot know a byte is
// code: it may be data or an operand, so like DDT and SID we advance ONE byte over
// each and read no operand -- decoding CB as a 3-byte JMP would invent an address
// from whatever came next. Each prints as DDT's own `??= <byte>` (how real DDT/SID
// flag a byte outside the published set) followed by the BARE `*` mnemonic of the
// effect: you SEE you have wandered into data or a Z80 binary, and what the byte
// would do if it ran, without the length claim that would desync the listing.
// (The CPU is the other story -- it really does execute CB as a 3-byte JMP.)
// ---------------------------------------------------------------------------
struct Op {
    const char* text;
    bool undoc;
};

// clang-format off
static const Op kOps[256] = {
/* 00 */ {"NOP",false},        {"LXI B,%W",false},   {"STAX B",false},   {"INX B",false},
/* 04 */ {"INR B",false},      {"DCR B",false},      {"MVI B,%B",false}, {"RLC",false},
/* 08 */ {"NOP",true},         {"DAD B",false},      {"LDAX B",false},   {"DCX B",false},
/* 0C */ {"INR C",false},      {"DCR C",false},      {"MVI C,%B",false}, {"RRC",false},
/* 10 */ {"NOP",true},         {"LXI D,%W",false},   {"STAX D",false},   {"INX D",false},
/* 14 */ {"INR D",false},      {"DCR D",false},      {"MVI D,%B",false}, {"RAL",false},
/* 18 */ {"NOP",true},         {"DAD D",false},      {"LDAX D",false},   {"DCX D",false},
/* 1C */ {"INR E",false},      {"DCR E",false},      {"MVI E,%B",false}, {"RAR",false},
/* 20 */ {"NOP",true},         {"LXI H,%W",false},   {"SHLD %W",false},  {"INX H",false},
/* 24 */ {"INR H",false},      {"DCR H",false},      {"MVI H,%B",false}, {"DAA",false},
/* 28 */ {"NOP",true},         {"DAD H",false},      {"LHLD %W",false},  {"DCX H",false},
/* 2C */ {"INR L",false},      {"DCR L",false},      {"MVI L,%B",false}, {"CMA",false},
/* 30 */ {"NOP",true},         {"LXI SP,%W",false},  {"STA %W",false},   {"INX SP",false},
/* 34 */ {"INR M",false},      {"DCR M",false},      {"MVI M,%B",false}, {"STC",false},
/* 38 */ {"NOP",true},         {"DAD SP",false},     {"LDA %W",false},   {"DCX SP",false},
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
/* C8 */ {"RZ",false},         {"RET",false},        {"JZ %W",false},    {"JMP %W",true},
/* CC */ {"CZ %W",false},      {"CALL %W",false},    {"ACI %B",false},   {"RST 1",false},
/* D0 */ {"RNC",false},        {"POP D",false},      {"JNC %W",false},   {"OUT %B",false},
/* D4 */ {"CNC %W",false},     {"PUSH D",false},     {"SUI %B",false},   {"RST 2",false},
/* D8 */ {"RC",false},         {"RET",true},         {"JC %W",false},    {"IN %B",false},
/* DC */ {"CC %W",false},      {"CALL %W",true},     {"SBI %B",false},   {"RST 3",false},
/* E0 */ {"RPO",false},        {"POP H",false},      {"JPO %W",false},   {"XTHL",false},
/* E4 */ {"CPO %W",false},     {"PUSH H",false},     {"ANI %B",false},   {"RST 4",false},
/* E8 */ {"RPE",false},        {"PCHL",false},       {"JPE %W",false},   {"XCHG",false},
/* EC */ {"CPE %W",false},     {"CALL %W",true},     {"XRI %B",false},   {"RST 5",false},
/* F0 */ {"RP",false},         {"POP PSW",false},    {"JP %W",false},    {"DI",false},
/* F4 */ {"CP %W",false},      {"PUSH PSW",false},   {"ORI %B",false},   {"RST 6",false},
/* F8 */ {"RM",false},         {"SPHL",false},       {"JM %W",false},    {"EI",false},
/* FC */ {"CM %W",false},      {"CALL %W",true},     {"CPI %B",false},   {"RST 7",false},
};
// clang-format on

// An operand in the requested base. `digits` is the HEX width (2 for a byte, 4 for
// a word) and doubles as the width selector: octal renders a byte as three digits
// (000..377) and a word as SPLIT octal -- its two bytes, hi then lo, one space
// between. Base 16 is the original hex, untouched.
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

class Isa8080 : public Disassembler {
public:
    const char* name() const override { return "8080"; }

    Insn at(uint16_t addr, const PeekFn& peek, int base) const override {
        uint8_t opc = peek(addr);
        const Op& op = kOps[opc];
        Insn in;
        in.undocumented = op.undoc;

        std::string t = op.text;

        // An undocumented opcode is ONE byte -- the way real DDT and SID step over it.
        // The bytes that follow are NOT its operand: they are far more likely data, or
        // the tail of a real instruction we mis-started on, so reading them as an
        // address would invent an operand out of whatever is next. We print DDT's own
        // `??= <byte>` marker plus the BARE mnemonic of what the byte would do if it
        // ran (`*JMP`, `*CALL`), consuming nothing and reading no address. The `??=`
        // says "not real 8080"; the `*JMP` names the effect without pretending to know
        // its target. The byte follows the console base like every other number here.
        if (op.undoc) {
            std::string m = t.substr(0, t.find('%'));  // drop "%W"/"%B": there is no operand
            while (!m.empty() && m.back() == ' ') m.pop_back();
            in.len = 1;
            in.text = "?\?= " + fmtNum(opc, 2, base) + "  *" + m;
            return in;
        }

        size_t p = t.find('%');
        if (p == std::string::npos) {
            in.len = 1;
        } else if (t[p + 1] == 'B') {
            in.len = 2;
            t = t.substr(0, p) + fmtNum(peek((uint16_t)(addr + 1)), 2, base) + t.substr(p + 2);
        } else {
            in.len = 3;
            // Low byte first: that is how the 8080 stores an address, and reading
            // it any other way is the classic transposition bug.
            unsigned w = peek((uint16_t)(addr + 1)) | (peek((uint16_t)(addr + 2)) << 8);
            in.operand = (uint16_t)w;  // the address a symbol can name
            in.operandBits = 16;
            t = t.substr(0, p) + fmtNum(w, 4, base) + t.substr(p + 2);
        }
        in.text = t;
        return in;
    }
};

const Isa8080 k8080;

// ---------------------------------------------------------------------------
// The 8080 assembler -- the inverse of the table above, built once by REVERSING
// kOps. Nothing here is hand-written per opcode: mnemonic->opcode is exactly the
// opcode->mnemonic table read the other way, so the two can never drift, and the
// round-trip `assemble(disassemble(b)) == b` over all 256 bytes proves it.
//
// The undocumented entries are SKIPPED in the reverse map, so the documented
// mnemonics stay a bijection: NOP keeps only 00 (not 08/10/...), JMP only C3,
// RET only C9, CALL only CD. The `??= XX *MNEM` text a disassembler prints for
// an undocumented byte is not valid input and does not round-trip -- by design.
// ---------------------------------------------------------------------------

// Uppercase; trim ends; collapse internal whitespace to one space; drop the space
// on either side of a comma; keep a trailing comma but no trailing space. Applied
// to BOTH the stored keys and the operator's input so the two always agree -- that
// symmetry is what lets `MVI C, EB` match the key built from `MVI C,%B`.
std::string asmNormalize(const std::string& s) {
    std::string t;
    for (char c : s) t += (char)std::toupper((unsigned char)c);

    std::string out;
    bool pendingSpace = false;
    for (char c : t) {
        if (c == ' ' || c == '\t') {
            if (!out.empty()) pendingSpace = true;  // fold runs; never a leading space
            continue;
        }
        if (c == ',') {
            while (!out.empty() && out.back() == ' ') out.pop_back();  // no space before ','
            out += ',';
            pendingSpace = false;  // and none after it
            continue;
        }
        if (pendingSpace) { out += ' '; pendingSpace = false; }
        out += c;
    }
    // A pending space here was trailing whitespace: drop it. A trailing comma stays.
    return out;
}

// Parse one operand into a value. Honors `base` (16 or 8) unless the token carries
// an explicit radix: a trailing H (hex) or Q/O (octal), or a leading 0x (hex). No
// symbols, no decimal, no sign -- the ISA layer owns no symbol table, and a bare
// A-F is a hex digit here, not a name. Returns false on empty/garbage/overflow of
// 16 bits. Self-contained on purpose: this file must not reach into core/value.
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
        if (v > 0xFFFF) return false;  // no operand is wider than a word
    }
    out = (unsigned)v;
    return true;
}

class Isa8080Assembler : public Assembler {
public:
    Isa8080Assembler() {
        for (int i = 0; i < 256; ++i) {
            const Op& op = kOps[i];
            if (op.undoc) continue;  // keep the documented copy only -- preserve the bijection
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

    const char* name() const override { return "8080"; }

    AsmResult assemble(uint16_t /*addr*/, const std::string& line, int base) const override {
        std::string n = asmNormalize(line);
        if (n.empty()) return fail("empty");

        // Exact map first: every no-operand form, including RST 0..RST 7, MOV M,B,
        // PUSH PSW and XCHG, so a register letter or RST digit is never mistaken for
        // a number to parse.
        auto e = exact_.find(n);
        if (e != exact_.end()) return ok({e->second});

        // Otherwise split at the RIGHTMOST separator of either kind: the prefix is
        // everything left of it, the operand the token to its right. `LXI SP,0100`
        // has a comma at 6 and a space at 3 -- 6 wins, so prefix `LXI SP,`, operand
        // `0100`. A prefix map key already ends in the comma for `MVI C,` forms.
        size_t sp = n.rfind(' ');
        size_t cm = n.rfind(',');
        size_t cut;
        if (sp == std::string::npos && cm == std::string::npos)
            return fail("unknown instruction: " + n);
        if (cm == std::string::npos) cut = sp;
        else if (sp == std::string::npos) cut = cm;
        else cut = sp > cm ? sp : cm;

        std::string operand = n.substr(cut + 1);
        // Keep a splitting comma as part of the prefix key (`MVI C,`); a splitting
        // space is a separator and is dropped.
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
        bool word;  // %W (2 operand bytes, low first) vs %B (1)
    };

    static AsmResult ok(std::vector<uint8_t> b) { return {std::move(b), {}}; }
    static AsmResult fail(std::string e) { return {{}, std::move(e)}; }

    std::unordered_map<std::string, uint8_t> exact_;
    std::unordered_map<std::string, Enc> prefix_;
};

const Isa8080Assembler k8080asm;

} // namespace

// Defined in isaZ80.cpp / isa6800.cpp. The registry lives here, in one place; each
// other decoder is a whole file of its own but registers through this one accessor.
const Disassembler* z80Disassembler();
const Disassembler* mc6800Disassembler();

const Disassembler* disassemblerFor(const std::string& isa) {
    std::string k;
    for (char c : isa) k += (char)std::tolower((unsigned char)c);
    if (k == "8080") return &k8080;
    if (k == "z80") return z80Disassembler();
    if (k == "6800") return mc6800Disassembler();
    return nullptr;  // The caller reports it. Disassembling a Z80 or a 6800 as an
                     // 8080 produces plausible, WRONG text -- worse than an error.
}

const Assembler* assemblerFor(const std::string& isa) {
    std::string k;
    for (char c : isa) k += (char)std::tolower((unsigned char)c);
    if (k == "8080") return &k8080asm;
    // No Z80 assembler yet -- prefixes, (IX+d) and signed JR make it a real
    // mini-assembler, not a table reverse. EDIT falls back to bytes until it lands.
    return nullptr;
}

std::vector<std::string> instructionSets() { return {"8080", "z80", "6800"}; }

} // namespace altair
