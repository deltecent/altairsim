#include "isa/isa.h"

#include <cassert>
#include <cctype>
#include <cstdio>
#include <unordered_map>

namespace altair {
namespace {

// ---------------------------------------------------------------------------
// The Motorola M6800 instruction set -- the CPU of the Altair 680b (DESIGN.md 3).
//
// Source: the M6800 programming reference reprinted in the MITS *Programming
// Manual -- altair 680b* (reference/Altair 680b Programming Manual.md), and the
// standard Motorola M6800 databook. 72 mnemonics across 197 valid opcodes; the
// other 59 of the 256 are UNDEFINED on the 6800 (unlike the 8080's twelve, which
// have real published effects) -- so a disassembler marks them the DDT way and
// steps ONE byte, inventing no operand from whatever follows.
//
// TWO THINGS DIFFER FROM THE 8080 AND BOTH BITE IF MISSED (Programming Manual 2):
//
//   * 16-bit operands are BIG-ENDIAN -- high byte first. `LDX #FC00` assembles CE
//     FC 00, and an extended address in bytes 2-3 is (byte2 << 8) | byte3. The
//     8080 is the other way round; reading a 6800 word low-first is the classic
//     transposition bug.
//
//   * There are SEVEN addressing modes, not the 8080's byte/word split. The mode
//     is carried per opcode below and decides both the length and how the operand
//     is spelled: `#nn` immediate, bare `nn` direct, `nn,X` indexed, a computed
//     target for a relative branch, and a 16-bit extended address.
//
// MNEMONICS ARE THE UNIFIED-SUFFIX MOTOROLA FORM -- `LDAA`/`STAB`/`PSHA`, and the
// accumulator read-modify-writes as `ASRA`/`INCB` (the Programming Manual's own
// §2 example). Every accumulator form is a suffix, so the table is a clean
// bijection the assembler can reverse without a second convention to disagree with.
// ---------------------------------------------------------------------------
enum class Mode {
    Inh,    // 1 byte, no operand:            NOP, RTS, ASRA, PSHB
    Imm,    // 2 bytes, #nn (8-bit immediate): LDAA #41
    Imm16,  // 3 bytes, #nnnn (16-bit imm):    CPX/LDS/LDX -- MS byte first
    Dir,    // 2 bytes, direct page 00-FF:     LDAA 50
    Idx,    // 2 bytes, offset,X:              LDAA 05,X
    Ext,    // 3 bytes, extended 16-bit addr:  LDAA FC00 -- MS byte first
    Rel,    // 2 bytes, relative branch -> computed target: BRA FC10
    Ill,    // undefined on the 6800: mnem is ""
};

struct Op {
    const char* mnem;
    Mode mode;
};

// clang-format off
static const Op kOps[256] = {
/* 00 */ {"",Mode::Ill},   {"NOP",Mode::Inh}, {"",Mode::Ill},   {"",Mode::Ill},
/* 04 */ {"",Mode::Ill},   {"",Mode::Ill},    {"TAP",Mode::Inh},{"TPA",Mode::Inh},
/* 08 */ {"INX",Mode::Inh},{"DEX",Mode::Inh}, {"CLV",Mode::Inh},{"SEV",Mode::Inh},
/* 0C */ {"CLC",Mode::Inh},{"SEC",Mode::Inh}, {"CLI",Mode::Inh},{"SEI",Mode::Inh},

/* 10 */ {"SBA",Mode::Inh},{"CBA",Mode::Inh}, {"",Mode::Ill},   {"",Mode::Ill},
/* 14 */ {"",Mode::Ill},   {"",Mode::Ill},    {"TAB",Mode::Inh},{"TBA",Mode::Inh},
/* 18 */ {"",Mode::Ill},   {"DAA",Mode::Inh}, {"",Mode::Ill},   {"ABA",Mode::Inh},
/* 1C */ {"",Mode::Ill},   {"",Mode::Ill},    {"",Mode::Ill},   {"",Mode::Ill},

/* 20 */ {"BRA",Mode::Rel},{"",Mode::Ill},    {"BHI",Mode::Rel},{"BLS",Mode::Rel},
/* 24 */ {"BCC",Mode::Rel},{"BCS",Mode::Rel}, {"BNE",Mode::Rel},{"BEQ",Mode::Rel},
/* 28 */ {"BVC",Mode::Rel},{"BVS",Mode::Rel}, {"BPL",Mode::Rel},{"BMI",Mode::Rel},
/* 2C */ {"BGE",Mode::Rel},{"BLT",Mode::Rel}, {"BGT",Mode::Rel},{"BLE",Mode::Rel},

/* 30 */ {"TSX",Mode::Inh}, {"INS",Mode::Inh}, {"PULA",Mode::Inh},{"PULB",Mode::Inh},
/* 34 */ {"DES",Mode::Inh}, {"TXS",Mode::Inh}, {"PSHA",Mode::Inh},{"PSHB",Mode::Inh},
/* 38 */ {"",Mode::Ill},    {"RTS",Mode::Inh}, {"",Mode::Ill},    {"RTI",Mode::Inh},
/* 3C */ {"",Mode::Ill},    {"",Mode::Ill},    {"WAI",Mode::Inh}, {"SWI",Mode::Inh},

/* 40 */ {"NEGA",Mode::Inh},{"",Mode::Ill},    {"",Mode::Ill},    {"COMA",Mode::Inh},
/* 44 */ {"LSRA",Mode::Inh},{"",Mode::Ill},    {"RORA",Mode::Inh},{"ASRA",Mode::Inh},
/* 48 */ {"ASLA",Mode::Inh},{"ROLA",Mode::Inh},{"DECA",Mode::Inh},{"",Mode::Ill},
/* 4C */ {"INCA",Mode::Inh},{"TSTA",Mode::Inh},{"",Mode::Ill},    {"CLRA",Mode::Inh},

/* 50 */ {"NEGB",Mode::Inh},{"",Mode::Ill},    {"",Mode::Ill},    {"COMB",Mode::Inh},
/* 54 */ {"LSRB",Mode::Inh},{"",Mode::Ill},    {"RORB",Mode::Inh},{"ASRB",Mode::Inh},
/* 58 */ {"ASLB",Mode::Inh},{"ROLB",Mode::Inh},{"DECB",Mode::Inh},{"",Mode::Ill},
/* 5C */ {"INCB",Mode::Inh},{"TSTB",Mode::Inh},{"",Mode::Ill},    {"CLRB",Mode::Inh},

/* 60 */ {"NEG",Mode::Idx},{"",Mode::Ill},   {"",Mode::Ill},   {"COM",Mode::Idx},
/* 64 */ {"LSR",Mode::Idx},{"",Mode::Ill},   {"ROR",Mode::Idx},{"ASR",Mode::Idx},
/* 68 */ {"ASL",Mode::Idx},{"ROL",Mode::Idx},{"DEC",Mode::Idx},{"",Mode::Ill},
/* 6C */ {"INC",Mode::Idx},{"TST",Mode::Idx},{"JMP",Mode::Idx},{"CLR",Mode::Idx},

/* 70 */ {"NEG",Mode::Ext},{"",Mode::Ill},   {"",Mode::Ill},   {"COM",Mode::Ext},
/* 74 */ {"LSR",Mode::Ext},{"",Mode::Ill},   {"ROR",Mode::Ext},{"ASR",Mode::Ext},
/* 78 */ {"ASL",Mode::Ext},{"ROL",Mode::Ext},{"DEC",Mode::Ext},{"",Mode::Ill},
/* 7C */ {"INC",Mode::Ext},{"TST",Mode::Ext},{"JMP",Mode::Ext},{"CLR",Mode::Ext},

/* 80 */ {"SUBA",Mode::Imm},{"CMPA",Mode::Imm},{"SBCA",Mode::Imm},{"",Mode::Ill},
/* 84 */ {"ANDA",Mode::Imm},{"BITA",Mode::Imm},{"LDAA",Mode::Imm},{"",Mode::Ill},
/* 88 */ {"EORA",Mode::Imm},{"ADCA",Mode::Imm},{"ORAA",Mode::Imm},{"ADDA",Mode::Imm},
/* 8C */ {"CPX",Mode::Imm16},{"BSR",Mode::Rel},{"LDS",Mode::Imm16},{"",Mode::Ill},

/* 90 */ {"SUBA",Mode::Dir},{"CMPA",Mode::Dir},{"SBCA",Mode::Dir},{"",Mode::Ill},
/* 94 */ {"ANDA",Mode::Dir},{"BITA",Mode::Dir},{"LDAA",Mode::Dir},{"STAA",Mode::Dir},
/* 98 */ {"EORA",Mode::Dir},{"ADCA",Mode::Dir},{"ORAA",Mode::Dir},{"ADDA",Mode::Dir},
/* 9C */ {"CPX",Mode::Dir}, {"",Mode::Ill},    {"LDS",Mode::Dir}, {"STS",Mode::Dir},

/* A0 */ {"SUBA",Mode::Idx},{"CMPA",Mode::Idx},{"SBCA",Mode::Idx},{"",Mode::Ill},
/* A4 */ {"ANDA",Mode::Idx},{"BITA",Mode::Idx},{"LDAA",Mode::Idx},{"STAA",Mode::Idx},
/* A8 */ {"EORA",Mode::Idx},{"ADCA",Mode::Idx},{"ORAA",Mode::Idx},{"ADDA",Mode::Idx},
/* AC */ {"CPX",Mode::Idx}, {"JSR",Mode::Idx}, {"LDS",Mode::Idx}, {"STS",Mode::Idx},

/* B0 */ {"SUBA",Mode::Ext},{"CMPA",Mode::Ext},{"SBCA",Mode::Ext},{"",Mode::Ill},
/* B4 */ {"ANDA",Mode::Ext},{"BITA",Mode::Ext},{"LDAA",Mode::Ext},{"STAA",Mode::Ext},
/* B8 */ {"EORA",Mode::Ext},{"ADCA",Mode::Ext},{"ORAA",Mode::Ext},{"ADDA",Mode::Ext},
/* BC */ {"CPX",Mode::Ext}, {"JSR",Mode::Ext}, {"LDS",Mode::Ext}, {"STS",Mode::Ext},

/* C0 */ {"SUBB",Mode::Imm},{"CMPB",Mode::Imm},{"SBCB",Mode::Imm},{"",Mode::Ill},
/* C4 */ {"ANDB",Mode::Imm},{"BITB",Mode::Imm},{"LDAB",Mode::Imm},{"",Mode::Ill},
/* C8 */ {"EORB",Mode::Imm},{"ADCB",Mode::Imm},{"ORAB",Mode::Imm},{"ADDB",Mode::Imm},
/* CC */ {"",Mode::Ill},    {"",Mode::Ill},    {"LDX",Mode::Imm16},{"",Mode::Ill},

/* D0 */ {"SUBB",Mode::Dir},{"CMPB",Mode::Dir},{"SBCB",Mode::Dir},{"",Mode::Ill},
/* D4 */ {"ANDB",Mode::Dir},{"BITB",Mode::Dir},{"LDAB",Mode::Dir},{"STAB",Mode::Dir},
/* D8 */ {"EORB",Mode::Dir},{"ADCB",Mode::Dir},{"ORAB",Mode::Dir},{"ADDB",Mode::Dir},
/* DC */ {"",Mode::Ill},    {"",Mode::Ill},    {"LDX",Mode::Dir}, {"STX",Mode::Dir},

/* E0 */ {"SUBB",Mode::Idx},{"CMPB",Mode::Idx},{"SBCB",Mode::Idx},{"",Mode::Ill},
/* E4 */ {"ANDB",Mode::Idx},{"BITB",Mode::Idx},{"LDAB",Mode::Idx},{"STAB",Mode::Idx},
/* E8 */ {"EORB",Mode::Idx},{"ADCB",Mode::Idx},{"ORAB",Mode::Idx},{"ADDB",Mode::Idx},
/* EC */ {"",Mode::Ill},    {"",Mode::Ill},    {"LDX",Mode::Idx}, {"STX",Mode::Idx},

/* F0 */ {"SUBB",Mode::Ext},{"CMPB",Mode::Ext},{"SBCB",Mode::Ext},{"",Mode::Ill},
/* F4 */ {"ANDB",Mode::Ext},{"BITB",Mode::Ext},{"LDAB",Mode::Ext},{"STAB",Mode::Ext},
/* F8 */ {"EORB",Mode::Ext},{"ADCB",Mode::Ext},{"ORAB",Mode::Ext},{"ADDB",Mode::Ext},
/* FC */ {"",Mode::Ill},    {"",Mode::Ill},    {"LDX",Mode::Ext}, {"STX",Mode::Ext},
};
// clang-format on

// An operand in the requested base -- same spelling rule as the 8080 decoder so a
// session in octal reads both machines the same way. `digits` is the HEX width (2
// for a byte, 4 for a word); octal renders a byte as three digits and a word as
// SPLIT octal (hi then lo, one space between).
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

class Isa6800 : public Disassembler {
public:
    const char* name() const override { return "6800"; }

    Insn at(uint16_t addr, const PeekFn& peek, int base) const override {
        uint8_t opc = peek(addr);
        const Op& op = kOps[opc];
        Insn in;

        // An undefined opcode is ONE byte -- the same DDT-style step the 8080 decoder
        // takes over its undocumented set, except a 6800 illegal has NO published
        // effect to name, so we print the bare `??= XX` marker and read no operand.
        if (op.mode == Mode::Ill) {
            in.undocumented = true;
            in.len = 1;
            in.text = "?\?= " + fmtNum(opc, 2, base);
            return in;
        }

        std::string t = op.mnem;
        switch (op.mode) {
        case Mode::Inh:
            in.len = 1;
            break;
        case Mode::Imm:
            in.len = 2;
            t += " #" + fmtNum(peek((uint16_t)(addr + 1)), 2, base);
            break;
        case Mode::Dir:
            in.len = 2;
            t += " " + fmtNum(peek((uint16_t)(addr + 1)), 2, base);
            break;
        case Mode::Idx:
            in.len = 2;
            t += " " + fmtNum(peek((uint16_t)(addr + 1)), 2, base) + ",X";
            break;
        case Mode::Imm16: {
            // 16-bit immediate, MS byte first (CPX/LDS/LDX). It is a VALUE, but it is
            // just as often a table address a symbol names, so we hand it up like the
            // 8080's LXI operand.
            in.len = 3;
            unsigned w = (peek((uint16_t)(addr + 1)) << 8) | peek((uint16_t)(addr + 2));
            in.operand = (uint16_t)w;
            in.operandBits = 16;
            t += " #" + fmtNum(w, 4, base);
            break;
        }
        case Mode::Ext: {
            // Extended address, MS byte first -- the opposite order from the 8080.
            in.len = 3;
            unsigned w = (peek((uint16_t)(addr + 1)) << 8) | peek((uint16_t)(addr + 2));
            in.operand = (uint16_t)w;
            in.operandBits = 16;
            t += " " + fmtNum(w, 4, base);
            break;
        }
        case Mode::Rel: {
            // The stored byte is a signed offset from the address AFTER the branch;
            // we resolve it to the absolute target, which is the useful thing to show
            // and the thing a symbol can name. D = (PC+2) + R (Programming Manual 2).
            in.len = 2;
            int8_t off = (int8_t)peek((uint16_t)(addr + 1));
            uint16_t target = (uint16_t)(addr + 2 + off);
            in.operand = target;
            in.operandBits = 16;
            t += " " + fmtNum(target, 4, base);
            break;
        }
        case Mode::Ill:
            break;  // handled above
        }
        in.text = t;
        return in;
    }
};

const Isa6800 k6800;

} // namespace

const Disassembler* mc6800Disassembler() { return &k6800; }

} // namespace altair
