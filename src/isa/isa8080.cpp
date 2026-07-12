#include "isa/isa.h"

#include <cctype>

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
// THE TEN UNDOCUMENTED OPCODES ARE HERE, AND THEY ARE NOT ERRORS. Real silicon
// executes 08/10/18/20/28/30/38 as NOP, CB as JMP, D9 as RET, and DD/ED/FD as
// CALL. A disassembler that prints `???` for them lies about what the chip does
// -- and code in the wild hits them. They are marked, and printed with a `*`, so
// you can SEE you have wandered into data or into a Z80 binary, which is nearly
// always what a run of them means.
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

std::string hex(unsigned v, int digits) {
    static const char* d = "0123456789ABCDEF";
    std::string s;
    for (int i = digits - 1; i >= 0; --i) s += d[(v >> (i * 4)) & 0xF];
    return s;
}

class Isa8080 : public Disassembler {
public:
    const char* name() const override { return "8080"; }

    Insn at(uint16_t addr, const PeekFn& peek) const override {
        const Op& op = kOps[peek(addr)];
        Insn in;
        in.undocumented = op.undoc;

        std::string t = op.text;
        size_t p = t.find('%');
        if (p == std::string::npos) {
            in.len = 1;
        } else if (t[p + 1] == 'B') {
            in.len = 2;
            t = t.substr(0, p) + hex(peek((uint16_t)(addr + 1)), 2) + t.substr(p + 2);
        } else {
            in.len = 3;
            // Low byte first: that is how the 8080 stores an address, and reading
            // it any other way is the classic transposition bug.
            unsigned w = peek((uint16_t)(addr + 1)) | (peek((uint16_t)(addr + 2)) << 8);
            t = t.substr(0, p) + hex(w, 4) + t.substr(p + 2);
        }

        // A leading `*` on the ten undocumented opcodes. A run of them is nearly
        // always a sign you are disassembling data -- or a Z80 binary -- and that
        // is worth being able to see at a glance.
        in.text = op.undoc ? "*" + t : t;
        return in;
    }
};

const Isa8080 k8080;

} // namespace

const Disassembler* disassemblerFor(const std::string& isa) {
    std::string k;
    for (char c : isa) k += (char)std::tolower((unsigned char)c);
    if (k == "8080") return &k8080;
    return nullptr;  // The caller reports it. Disassembling a Z80 as an 8080
                     // produces plausible, WRONG text -- worse than an error.
}

std::vector<std::string> instructionSets() { return {"8080"}; }

} // namespace altair
