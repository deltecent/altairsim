#include "isa/isa.h"

#include <cctype>
#include <string>

namespace altair {
namespace {

// ---------------------------------------------------------------------------
// The Z80 disassembler. Same contract as the 8080 (isa8080.cpp): bytes in, text
// and a length out, no registers and no bus. It is a SEPARATE table from the
// 8080's on purpose -- the bytes the 8080 calls undocumented NOPs/CALLs the Z80
// calls prefixes (CB/DD/ED/FD) and EXX (D9). Sharing a table would teach one
// chip the other's lies (DESIGN.md 3.0.2, and the plan's Phase 1 note).
//
// Source: Zilog Z80 CPU User's Manual (UM008) and the Sean Young "Undocumented
// Z80 Documented" survey -- so `LD B,C`, `JR NZ,`, `SLL`, the DD/FD IXH/IXL half
// registers and the DDCB reg-copy forms, all in Zilog spelling.
//
// Decoding branches on the first byte:
//   main (unprefixed)  the 256-entry kMain table, below
//   CB                 rotate/shift/BIT/RES/SET -- fully regular, decoded by bits
//   ED                 extended -- block ops, 16-bit arithmetic, I/O
//   DD / FD            the main (and CB) tables with HL->IX/IY and a signed
//                      displacement inserted for (HL) -> (IX+d)
// The prefix bytes are summed into Insn::len, which is 1..4.
// ---------------------------------------------------------------------------

std::string hex(unsigned v, int digits) {
    static const char* d = "0123456789ABCDEF";
    std::string s;
    for (int i = digits - 1; i >= 0; --i) s += d[(v >> (i * 4)) & 0xF];
    return s;
}

const char* r8[8] = {"B", "C", "D", "E", "H", "L", "(HL)", "A"};
const char* ss16[4] = {"BC", "DE", "HL", "SP"};

// ---------------------------------------------------------------------------
// The 256-entry main (unprefixed) table. `%B` immediate byte, `%W` immediate
// word (low byte first), `%R` relative-jump target (printed as the absolute
// destination). Nothing here is undocumented -- the four holes are the CB/DD/ED/
// FD prefixes and are dispatched before this table is ever indexed, so their text
// is unreachable; D9 is EXX, a real instruction. Undocumented-ness on the Z80
// enters only via the index prefixes (IXH/IXL) and the CB/ED tables.
// ---------------------------------------------------------------------------
// clang-format off
static const char* kMain[256] = {
/* 00 */ "NOP",          "LD BC,%W",     "LD (BC),A",    "INC BC",
/* 04 */ "INC B",        "DEC B",        "LD B,%B",      "RLCA",
/* 08 */ "EX AF,AF'",    "ADD HL,BC",    "LD A,(BC)",    "DEC BC",
/* 0C */ "INC C",        "DEC C",        "LD C,%B",      "RRCA",
/* 10 */ "DJNZ %R",      "LD DE,%W",     "LD (DE),A",    "INC DE",
/* 14 */ "INC D",        "DEC D",        "LD D,%B",      "RLA",
/* 18 */ "JR %R",        "ADD HL,DE",    "LD A,(DE)",    "DEC DE",
/* 1C */ "INC E",        "DEC E",        "LD E,%B",      "RRA",
/* 20 */ "JR NZ,%R",     "LD HL,%W",     "LD (%W),HL",   "INC HL",
/* 24 */ "INC H",        "DEC H",        "LD H,%B",      "DAA",
/* 28 */ "JR Z,%R",      "ADD HL,HL",    "LD HL,(%W)",   "DEC HL",
/* 2C */ "INC L",        "DEC L",        "LD L,%B",      "CPL",
/* 30 */ "JR NC,%R",     "LD SP,%W",     "LD (%W),A",    "INC SP",
/* 34 */ "INC (HL)",     "DEC (HL)",     "LD (HL),%B",   "SCF",
/* 38 */ "JR C,%R",      "ADD HL,SP",    "LD A,(%W)",    "DEC SP",
/* 3C */ "INC A",        "DEC A",        "LD A,%B",      "CCF",

/* 40 */ "LD B,B",       "LD B,C",       "LD B,D",       "LD B,E",
/* 44 */ "LD B,H",       "LD B,L",       "LD B,(HL)",    "LD B,A",
/* 48 */ "LD C,B",       "LD C,C",       "LD C,D",       "LD C,E",
/* 4C */ "LD C,H",       "LD C,L",       "LD C,(HL)",    "LD C,A",
/* 50 */ "LD D,B",       "LD D,C",       "LD D,D",       "LD D,E",
/* 54 */ "LD D,H",       "LD D,L",       "LD D,(HL)",    "LD D,A",
/* 58 */ "LD E,B",       "LD E,C",       "LD E,D",       "LD E,E",
/* 5C */ "LD E,H",       "LD E,L",       "LD E,(HL)",    "LD E,A",
/* 60 */ "LD H,B",       "LD H,C",       "LD H,D",       "LD H,E",
/* 64 */ "LD H,H",       "LD H,L",       "LD H,(HL)",    "LD H,A",
/* 68 */ "LD L,B",       "LD L,C",       "LD L,D",       "LD L,E",
/* 6C */ "LD L,H",       "LD L,L",       "LD L,(HL)",    "LD L,A",
/* 70 */ "LD (HL),B",    "LD (HL),C",    "LD (HL),D",    "LD (HL),E",
/* 74 */ "LD (HL),H",    "LD (HL),L",    "HALT",         "LD (HL),A",
/* 78 */ "LD A,B",       "LD A,C",       "LD A,D",       "LD A,E",
/* 7C */ "LD A,H",       "LD A,L",       "LD A,(HL)",    "LD A,A",

/* 80 */ "ADD A,B",      "ADD A,C",      "ADD A,D",      "ADD A,E",
/* 84 */ "ADD A,H",      "ADD A,L",      "ADD A,(HL)",   "ADD A,A",
/* 88 */ "ADC A,B",      "ADC A,C",      "ADC A,D",      "ADC A,E",
/* 8C */ "ADC A,H",      "ADC A,L",      "ADC A,(HL)",   "ADC A,A",
/* 90 */ "SUB B",        "SUB C",        "SUB D",        "SUB E",
/* 94 */ "SUB H",        "SUB L",        "SUB (HL)",     "SUB A",
/* 98 */ "SBC A,B",      "SBC A,C",      "SBC A,D",      "SBC A,E",
/* 9C */ "SBC A,H",      "SBC A,L",      "SBC A,(HL)",   "SBC A,A",
/* A0 */ "AND B",        "AND C",        "AND D",        "AND E",
/* A4 */ "AND H",        "AND L",        "AND (HL)",     "AND A",
/* A8 */ "XOR B",        "XOR C",        "XOR D",        "XOR E",
/* AC */ "XOR H",        "XOR L",        "XOR (HL)",     "XOR A",
/* B0 */ "OR B",         "OR C",         "OR D",         "OR E",
/* B4 */ "OR H",         "OR L",         "OR (HL)",      "OR A",
/* B8 */ "CP B",         "CP C",         "CP D",         "CP E",
/* BC */ "CP H",         "CP L",         "CP (HL)",      "CP A",

/* C0 */ "RET NZ",       "POP BC",       "JP NZ,%W",     "JP %W",
/* C4 */ "CALL NZ,%W",   "PUSH BC",      "ADD A,%B",     "RST 00",
/* C8 */ "RET Z",        "RET",          "JP Z,%W",      "CB",
/* CC */ "CALL Z,%W",    "CALL %W",      "ADC A,%B",     "RST 08",
/* D0 */ "RET NC",       "POP DE",       "JP NC,%W",     "OUT (%B),A",
/* D4 */ "CALL NC,%W",   "PUSH DE",      "SUB %B",       "RST 10",
/* D8 */ "RET C",        "EXX",          "JP C,%W",      "IN A,(%B)",
/* DC */ "CALL C,%W",    "DD",           "SBC A,%B",     "RST 18",
/* E0 */ "RET PO",       "POP HL",       "JP PO,%W",     "EX (SP),HL",
/* E4 */ "CALL PO,%W",   "PUSH HL",      "AND %B",       "RST 20",
/* E8 */ "RET PE",       "JP (HL)",      "JP PE,%W",     "EX DE,HL",
/* EC */ "CALL PE,%W",   "ED",           "XOR %B",       "RST 28",
/* F0 */ "RET P",        "POP AF",       "JP P,%W",      "DI",
/* F4 */ "CALL P,%W",    "PUSH AF",      "OR %B",        "RST 30",
/* F8 */ "RET M",        "LD SP,HL",     "JP M,%W",      "EI",
/* FC */ "CALL M,%W",    "FD",           "CP %B",        "RST 38",
};
// clang-format on

// The absolute destination of a JR/DJNZ. `cur` points at the displacement byte;
// the instruction after it is one byte further, and the signed displacement is
// relative to THAT -- which is why a JR to itself reads as e = FE, not 00.
std::string relTarget(uint16_t cur, int8_t e) {
    return hex((uint16_t)(cur + 1 + e), 4);
}

// A signed IX/IY displacement, printed +dd / -dd.
std::string dispStr(int8_t d) {
    int mag = d < 0 ? -(int)d : d;
    return (d < 0 ? std::string("-") : std::string("+")) + hex((unsigned)mag, 2);
}

// Walk the `%` tokens left to right, each consuming operand bytes from a cursor
// that starts just past the opcode. Returns the filled text and how many operand
// bytes it ate -- the caller adds that to the opcode/prefix length. Scanning in a
// loop is the whole reason Z80 needs more than the 8080's single find('%'): a
// `LD (IX+d),n` carries a displacement AND an immediate.
struct Filled {
    std::string text;
    uint8_t operandBytes = 0;
};

Filled fillOperands(std::string t, const PeekFn& peek, uint16_t firstOperand) {
    Filled out;
    uint16_t cur = firstOperand;
    size_t p;
    while ((p = t.find('%')) != std::string::npos) {
        char kind = t[p + 1];
        std::string rep;
        if (kind == 'B') {
            rep = hex(peek(cur), 2);
            cur += 1;
            out.operandBytes += 1;
        } else if (kind == 'W') {
            unsigned w = peek(cur) | (peek((uint16_t)(cur + 1)) << 8);
            rep = hex(w, 4);
            cur += 2;
            out.operandBytes += 2;
        } else if (kind == 'R') {
            rep = relTarget(cur, (int8_t)peek(cur));
            cur += 1;
            out.operandBytes += 1;
        } else if (kind == 'D') {
            rep = dispStr((int8_t)peek(cur));
            cur += 1;
            out.operandBytes += 1;
        } else {
            rep = "%";  // unreachable -- the tables never emit a bare %
        }
        t = t.substr(0, p) + rep + t.substr(p + 2);
    }
    out.text = t;
    return out;
}

Insn make(const std::string& text, uint8_t len, bool undoc) {
    Insn in;
    in.text = undoc ? "*" + text : text;
    in.len = len;
    in.undocumented = undoc;
    return in;
}

// ---------------------------------------------------------------------------
// DD/FD register substitution on a main-table mnemonic (Sean Young 6.1).
//
// A DD (FD) prefix makes the instruction use IX (IY) in place of HL:
//   (HL)      -> (IX+d)   a displacement byte is inserted; H/L keep their meaning
//   HL        -> IX       the 16-bit pair
//   H, L      -> IXH, IXL the half registers -- UNDOCUMENTED
// Two main-table opcodes wear "HL" but ignore the prefix: EX DE,HL (EB) is
// unaffected, and JP (HL) (E9) becomes JP (IX) with NO displacement. They are
// special-cased; everything else falls out of the three rules above.
// ---------------------------------------------------------------------------
struct Subst {
    std::string text;
    bool undocHalf = false;  // an IXH/IXL appeared -> the whole instruction is undocumented
};

bool isDelim(char c) { return c == ' ' || c == ',' || c == '(' || c == ')' || c == '\0'; }

// Replace every standalone one-char register token `ch` with `repl`. "Standalone"
// = delimited on both sides, so the H in HALT or the L in CPL is left alone.
bool replaceReg(std::string& s, char ch, const std::string& repl) {
    bool any = false;
    for (size_t i = 0; i < s.size(); ) {
        char before = i == 0 ? '\0' : s[i - 1];
        char after = i + 1 >= s.size() ? '\0' : s[i + 1];
        if (s[i] == ch && isDelim(before) && isDelim(after)) {
            s = s.substr(0, i) + repl + s.substr(i + 1);
            i += repl.size();
            any = true;
        } else {
            ++i;
        }
    }
    return any;
}

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos) {
        s = s.substr(0, p) + to + s.substr(p + from.size());
        p += to.size();
    }
}

Subst applyIndex(const std::string& t0, const std::string& ix, uint8_t op) {
    Subst s;
    s.text = t0;
    if (op == 0xEB) return s;                          // EX DE,HL -- prefix ignored
    if (op == 0xE9) { s.text = "JP (" + ix + ")"; return s; }  // JP (HL) -> JP (IX), no d

    size_t mem = s.text.find("(HL)");
    if (mem != std::string::npos) {
        s.text = s.text.substr(0, mem) + "(" + ix + "%D)" + s.text.substr(mem + 4);
        return s;
    }
    replaceAll(s.text, "HL", ix);
    if (replaceReg(s.text, 'H', ix + "H")) s.undocHalf = true;
    if (replaceReg(s.text, 'L', ix + "L")) s.undocHalf = true;
    return s;
}

// ---------------------------------------------------------------------------
// CB -- rotates/shifts and bit ops. Fully regular: y=(op>>3)&7 picks the op,
// z=op&7 the register (6 = (HL)). SLL (y==6 in the shift block) is undocumented.
// ---------------------------------------------------------------------------
const char* kRot[8] = {"RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL"};

Insn decodeCB(uint16_t addr, const PeekFn& peek) {
    uint8_t op = peek((uint16_t)(addr + 1));
    uint8_t y = (op >> 3) & 7, z = op & 7;
    std::string reg = r8[z];
    if (op < 0x40) {
        bool undoc = (y == 6);  // SLL
        return make(std::string(kRot[y]) + " " + reg, 2, undoc);
    }
    std::string bit = std::to_string(y);
    if (op < 0x80) return make("BIT " + bit + "," + reg, 2, false);
    if (op < 0xC0) return make("RES " + bit + "," + reg, 2, false);
    return make("SET " + bit + "," + reg, 2, false);
}

// DD CB dd op / FD CB dd op -- always four bytes, and the displacement comes
// BEFORE the opcode. The documented forms operate on (IX+d) (z==6); the z!=6
// forms ALSO copy the result into register z (BIT ignores z), all undocumented.
Insn decodeIndexedCB(uint16_t addr, const PeekFn& peek, const std::string& ix) {
    int8_t d = (int8_t)peek((uint16_t)(addr + 2));
    uint8_t op = peek((uint16_t)(addr + 3));
    uint8_t y = (op >> 3) & 7, z = op & 7;
    std::string tgt = "(" + ix + dispStr(d) + ")";
    const char* copy = r8[z];  // z != 6 here for the undoc forms

    if (op < 0x40) {
        std::string t = std::string(kRot[y]) + " " + tgt;
        bool undoc = (z != 6) || (y == 6);
        if (z != 6) t += std::string(",") + copy;
        return make(t, 4, undoc);
    }
    std::string bit = std::to_string(y);
    if (op < 0x80) return make("BIT " + bit + "," + tgt, 4, z != 6);  // z ignored, still undoc
    const char* mnem = op < 0xC0 ? "RES " : "SET ";
    std::string t = mnem + bit + "," + tgt;
    bool undoc = (z != 6);
    if (z != 6) t += std::string(",") + copy;
    return make(t, 4, undoc);
}

// ---------------------------------------------------------------------------
// ED -- the extended page. Sparse: everything not decoded here is an undefined
// 2-byte no-op, which real silicon runs as a NOP, so it is marked and printed.
// The 0x40..0x7F quadrant is a regular grid (Zilog UM008 table); 0xA0..0xBB are
// the block moves/searches/I-O and their repeating forms.
// ---------------------------------------------------------------------------
Insn decodeED(uint16_t addr, const PeekFn& peek) {
    uint8_t op = peek((uint16_t)(addr + 1));

    auto fill = [&](const std::string& t, bool undoc) {
        Filled f = fillOperands(t, peek, (uint16_t)(addr + 2));
        return make(f.text, (uint8_t)(2 + f.operandBytes), undoc);
    };

    if (op >= 0x40 && op <= 0x7F) {
        uint8_t y = (op >> 3) & 7, z = op & 7;
        const char* ss = ss16[(op >> 4) & 3];
        switch (z) {
        case 0:  // IN r,(C) -- y==6 is the flags-only "IN (C)", undocumented
            if (y == 6) return make("IN (C)", 2, true);
            return make(std::string("IN ") + r8[y] + ",(C)", 2, false);
        case 1:  // OUT (C),r -- y==6 outputs 0 (undocumented)
            if (y == 6) return make("OUT (C),0", 2, true);
            return make(std::string("OUT (C),") + r8[y], 2, false);
        case 2:  // SBC/ADC HL,ss
            return make((op & 8 ? std::string("ADC HL,") : std::string("SBC HL,")) + ss,
                        2, false);
        case 3: {  // LD (nn),ss / LD ss,(nn) -- the HL forms (63/6B) duplicate 22/2A (undoc)
            bool undoc = ((op >> 4) & 3) == 2;
            if (op & 8) return fill(std::string("LD ") + ss + ",(%W)", undoc);
            return fill(std::string("LD (%W),") + ss, undoc);
        }
        case 4:  // NEG -- only 44 documented
            return make("NEG", 2, op != 0x44);
        case 5:  // RETN / RETI -- only 45 (RETN) and 4D (RETI) documented
            return make(op == 0x4D ? "RETI" : "RETN", 2, op != 0x45 && op != 0x4D);
        case 6: {  // IM -- 46/56/5E documented; the rest are undocumented aliases
            int mode = (op == 0x56 || op == 0x76) ? 1 : (op == 0x5E || op == 0x7E) ? 2 : 0;
            bool undoc = op != 0x46 && op != 0x56 && op != 0x5E;
            return make("IM " + std::to_string(mode), 2, undoc);
        }
        case 7:  // the specials -- LD I,A / LD A,R / RRD / RLD ...
            switch (op) {
            case 0x47: return make("LD I,A", 2, false);
            case 0x4F: return make("LD R,A", 2, false);
            case 0x57: return make("LD A,I", 2, false);
            case 0x5F: return make("LD A,R", 2, false);
            case 0x67: return make("RRD", 2, false);
            case 0x6F: return make("RLD", 2, false);
            default: return make("NOP", 2, true);  // 77, 7F
            }
        }
    }

    switch (op) {
    case 0xA0: return make("LDI", 2, false);
    case 0xA1: return make("CPI", 2, false);
    case 0xA2: return make("INI", 2, false);
    case 0xA3: return make("OUTI", 2, false);
    case 0xA8: return make("LDD", 2, false);
    case 0xA9: return make("CPD", 2, false);
    case 0xAA: return make("IND", 2, false);
    case 0xAB: return make("OUTD", 2, false);
    case 0xB0: return make("LDIR", 2, false);
    case 0xB1: return make("CPIR", 2, false);
    case 0xB2: return make("INIR", 2, false);
    case 0xB3: return make("OTIR", 2, false);
    case 0xB8: return make("LDDR", 2, false);
    case 0xB9: return make("CPDR", 2, false);
    case 0xBA: return make("INDR", 2, false);
    case 0xBB: return make("OTDR", 2, false);
    default: return make("NOP", 2, true);  // undefined ED -- a marked 2-byte NOP
    }
}

// ---------------------------------------------------------------------------
// The unprefixed main path.
// ---------------------------------------------------------------------------
Insn decodeMain(uint16_t addr, const PeekFn& peek) {
    uint8_t op = peek(addr);
    Filled f = fillOperands(kMain[op], peek, (uint16_t)(addr + 1));
    return make(f.text, (uint8_t)(1 + f.operandBytes), false);
}

// ---------------------------------------------------------------------------
// DD / FD. The opcode is one byte past the prefix; its operands one byte past
// that (a displacement, if the substitution inserted (IX+d), then any immediate).
// A prefix followed by ANOTHER prefix (DD/FD/ED) is wasted silicon -- only the
// last one counts -- so it disassembles as a lone 1-byte NOP and the caller
// re-decodes the next byte fresh, which is exactly how the chip re-fetches it.
// ---------------------------------------------------------------------------
Insn decodeIndexed(uint16_t addr, const PeekFn& peek, const std::string& ix) {
    uint8_t op = peek((uint16_t)(addr + 1));
    if (op == 0xCB) return decodeIndexedCB(addr, peek, ix);
    if (op == 0xDD || op == 0xFD || op == 0xED) return make("NOP", 1, true);

    Subst s = applyIndex(kMain[op], ix, op);
    Filled f = fillOperands(s.text, peek, (uint16_t)(addr + 2));
    return make(f.text, (uint8_t)(2 + f.operandBytes), s.undocHalf);
}

class IsaZ80 : public Disassembler {
public:
    const char* name() const override { return "z80"; }

    Insn at(uint16_t addr, const PeekFn& peek) const override {
        uint8_t b0 = peek(addr);
        if (b0 == 0xCB) return decodeCB(addr, peek);
        if (b0 == 0xED) return decodeED(addr, peek);
        if (b0 == 0xDD) return decodeIndexed(addr, peek, "IX");
        if (b0 == 0xFD) return decodeIndexed(addr, peek, "IY");
        return decodeMain(addr, peek);
    }
};

const IsaZ80 kZ80;

} // namespace

// The 8080 file owns the registry (disassemblerFor/instructionSets); it reaches
// this instance through here so the lookup stays in one place.
const Disassembler* z80Disassembler() { return &kZ80; }

} // namespace altair
