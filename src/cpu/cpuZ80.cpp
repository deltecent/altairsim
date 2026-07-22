#include "cpu/cpuZ80.h"

#include "core/statefile.h"

#include <array>

namespace altair {

// Even-parity table, generated from the definition so it cannot drift from it --
// the same technique the 8080 core uses. Even parity: set when the number of 1
// bits is EVEN. The Z80's P/V bit is this for logical ops and overflow for
// arithmetic, sharing one flag position.
static constexpr std::array<bool, 256> kEvenParity = [] {
    std::array<bool, 256> t{};
    for (int v = 0; v < 256; ++v) {
        int bits = 0;
        for (int i = 0; i < 8; ++i)
            if (v & (1 << i)) ++bits;
        t[v] = (bits % 2) == 0;
    }
    return t;
}();

uint8_t CpuZ80::szxp(uint8_t v) const {
    uint8_t f = (uint8_t)(v & (FS | FF5 | FF3));
    if (v == 0) f |= FZ;
    if (kEvenParity[v]) f |= FPV;
    return f;
}
uint8_t CpuZ80::szx(uint8_t v) const {
    uint8_t f = (uint8_t)(v & (FS | FF5 | FF3));
    if (v == 0) f |= FZ;
    return f;
}

// ---------------------------------------------------------------------------
// Fetch and store -- every one a real bus cycle.
// ---------------------------------------------------------------------------
uint8_t CpuZ80::fetchOp(Bus& bus) {
    // R counts M1 cycles: bit 7 is held, bits 0..6 increment. A prefix is its own
    // M1, so DD/CB/ED each bump R -- which is why LD A,R after a prefix reads odd.
    r_ = (uint8_t)((r_ & 0x80) | ((r_ + 1) & 0x7F));
    if (intFetch_) return bus.intAck();  // and PC does not move
    return bus.memRead(pc_++);
}

uint8_t CpuZ80::fetchByte(Bus& bus) {
    if (intFetch_) return bus.intAck();
    return bus.memRead(pc_++);
}

uint16_t CpuZ80::fetch16(Bus& bus) {
    uint8_t lo = fetchByte(bus);
    uint8_t hi = fetchByte(bus);
    return (uint16_t)(lo | (hi << 8));
}

void CpuZ80::push(Bus& bus, uint16_t v) {
    bus.memWrite(--sp_, (uint8_t)(v >> 8));
    bus.memWrite(--sp_, (uint8_t)(v & 0xFF));
}

uint16_t CpuZ80::pop(Bus& bus) {
    uint8_t lo = bus.memRead(sp_++);
    uint8_t hi = bus.memRead(sp_++);
    return (uint16_t)(lo | (hi << 8));
}

// ---------------------------------------------------------------------------
// Register-file plumbing, aware of the DD/FD index context.
// ---------------------------------------------------------------------------
uint8_t* CpuZ80::reg8(int i) {
    switch (i) {
    case 0: return &b_;
    case 1: return &c_;
    case 2: return &d_;
    case 3: return &e_;
    case 4: return &h_;
    case 5: return &l_;
    default: return &a_;   // 7; 6 is memory and never routes here
    }
}

uint16_t& CpuZ80::idxReg(const Ctx& c) { return c.idx == Idx::IX ? ix_ : iy_; }

// (HL), or (IX+d)/(IY+d). The indexed form latches WZ -- the address it computed
// is exactly what MEMPTR holds, and that is what leaks into F5/F3 for BIT.
uint16_t CpuZ80::idxAddr(const Ctx& c) {
    if (c.idx == Idx::HL) return hl();
    uint16_t a = (uint16_t)(idxReg(c) + c.disp);
    wz_ = a;
    return a;
}

uint8_t CpuZ80::getR(Bus& bus, int i, const Ctx& c) {
    if (i == 6) return bus.memRead(idxAddr(c));
    // The DD/FD half registers -- IXH/IXL -- appear only when the instruction has
    // NO memory operand (Sean Young 6.1). LD H,(IX+d) keeps H as H.
    if (c.idx != Idx::HL && !c.mem) {
        if (i == 4) return (uint8_t)(idxReg(c) >> 8);
        if (i == 5) return (uint8_t)(idxReg(c) & 0xFF);
    }
    return *reg8(i);
}

void CpuZ80::setR(Bus& bus, int i, const Ctx& c, uint8_t v) {
    if (i == 6) { bus.memWrite(idxAddr(c), v); return; }
    if (c.idx != Idx::HL && !c.mem) {
        uint16_t& r = idxReg(c);
        if (i == 4) { r = (uint16_t)((r & 0x00FF) | (v << 8)); return; }
        if (i == 5) { r = (uint16_t)((r & 0xFF00) | v); return; }
    }
    *reg8(i) = v;
}

uint16_t CpuZ80::getRP(int i, const Ctx& c) {
    switch (i) {
    case 0: return bc();
    case 1: return de();
    case 2: return hl16(c);
    default: return sp_;
    }
}
void CpuZ80::setRP(int i, const Ctx& c, uint16_t v) {
    switch (i) {
    case 0: setBC(v); break;
    case 1: setDE(v); break;
    case 2: if (c.idx == Idx::HL) setHL(v); else idxReg(c) = v; break;
    default: sp_ = v; break;
    }
}

bool CpuZ80::cond(int i) const {
    switch (i) {
    case 0: return !flag(FZ);
    case 1: return flag(FZ);
    case 2: return !flag(FC);
    case 3: return flag(FC);
    case 4: return !flag(FPV);  // PO -- parity odd / no overflow
    case 5: return flag(FPV);   // PE
    case 6: return !flag(FS);   // P -- plus
    default: return flag(FS);   // M -- minus
    }
}

// ---------------------------------------------------------------------------
// The ALU. F5/F3 copy result bits 5 and 3 on every op that writes them -- that is
// the delta over a documented-only core, and it is why ZEXALL is the gate.
// ---------------------------------------------------------------------------
void CpuZ80::addA(uint8_t v, uint8_t cin) {
    unsigned r = (unsigned)a_ + v + cin;
    uint8_t res = (uint8_t)r;
    uint8_t f = 0;
    if (((a_ & 0xF) + (v & 0xF) + cin) & 0x10) f |= FH;
    if ((~(a_ ^ v) & (a_ ^ res) & 0x80)) f |= FPV;  // signs equal in, differ out
    if (r > 0xFF) f |= FC;
    f |= szx(res) & ~(uint8_t)FZ;
    if (res == 0) f |= FZ;
    a_ = res;
    f_ = f;  // N = 0
}

void CpuZ80::subA(uint8_t v, uint8_t cin, bool store) {
    unsigned r = (unsigned)a_ - v - cin;
    uint8_t res = (uint8_t)r;
    uint8_t f = FN;
    if (((a_ & 0xF) - (v & 0xF) - cin) & 0x10) f |= FH;
    if (((a_ ^ v) & (a_ ^ res) & 0x80)) f |= FPV;   // signs differ in, result flips
    if (r & 0x100) f |= FC;
    if (res & 0x80) f |= FS;
    if (res == 0) f |= FZ;
    // CP takes F5/F3 from the OPERAND, not the (discarded) result.
    f |= (store ? res : v) & (FF5 | FF3);
    if (store) a_ = res;
    f_ = f;
}

void CpuZ80::andA(uint8_t v) { a_ &= v; f_ = (uint8_t)(szxp(a_) | FH); }
void CpuZ80::orA(uint8_t v)  { a_ |= v; f_ = szxp(a_); }
void CpuZ80::xorA(uint8_t v) { a_ ^= v; f_ = szxp(a_); }

// INC/DEC leave CARRY alone -- the one exception that lets a counter tick inside a
// multi-byte add without eating the carry it is propagating.
uint8_t CpuZ80::incR(uint8_t v) {
    uint8_t r = (uint8_t)(v + 1);
    uint8_t f = (uint8_t)(f_ & FC);
    f |= szx(r);
    if ((r & 0x0F) == 0) f |= FH;
    if (v == 0x7F) f |= FPV;
    f_ = f;  // N = 0
    return r;
}
uint8_t CpuZ80::decR(uint8_t v) {
    uint8_t r = (uint8_t)(v - 1);
    uint8_t f = (uint8_t)((f_ & FC) | FN);
    f |= szx(r);
    if ((r & 0x0F) == 0x0F) f |= FH;
    if (v == 0x80) f |= FPV;
    f_ = f;
    return r;
}

// ADD HL,ss (or ADD IX/IY,ss): CARRY and half-carry only, S/Z/PV preserved. F5/F3
// come from the HIGH byte of the result; MEMPTR = HL+1.
void CpuZ80::addHL(const Ctx& c, uint16_t v) {
    uint16_t a = hl16(c);
    unsigned r = (unsigned)a + v;
    uint8_t f = (uint8_t)(f_ & (FS | FZ | FPV));
    if (((a & 0x0FFF) + (v & 0x0FFF)) & 0x1000) f |= FH;
    if (r > 0xFFFF) f |= FC;
    uint16_t res = (uint16_t)r;
    f |= (uint8_t)((res >> 8) & (FF5 | FF3));
    setRP(2, c, res);
    f_ = f;      // N = 0
    wz_ = (uint16_t)(a + 1);
}

void CpuZ80::adcHL(uint16_t v) {
    uint16_t a = hl();
    unsigned cin = flag(FC) ? 1 : 0;
    unsigned r = (unsigned)a + v + cin;
    uint16_t res = (uint16_t)r;
    uint8_t f = 0;
    if (res & 0x8000) f |= FS;
    if (res == 0) f |= FZ;
    if (((a & 0x0FFF) + (v & 0x0FFF) + cin) & 0x1000) f |= FH;
    if ((~(a ^ v) & (a ^ res) & 0x8000)) f |= FPV;
    if (r > 0xFFFF) f |= FC;
    f |= (uint8_t)((res >> 8) & (FF5 | FF3));
    setHL(res);
    f_ = f;
    wz_ = (uint16_t)(a + 1);
}

void CpuZ80::sbcHL(uint16_t v) {
    uint16_t a = hl();
    unsigned cin = flag(FC) ? 1 : 0;
    unsigned r = (unsigned)a - v - cin;
    uint16_t res = (uint16_t)r;
    uint8_t f = FN;
    if (res & 0x8000) f |= FS;
    if (res == 0) f |= FZ;
    if (((a & 0x0FFF) - (v & 0x0FFF) - cin) & 0x1000) f |= FH;
    if (((a ^ v) & (a ^ res) & 0x8000)) f |= FPV;
    if (r & 0x10000) f |= FC;
    f |= (uint8_t)((res >> 8) & (FF5 | FF3));
    setHL(res);
    f_ = f;
    wz_ = (uint16_t)(a + 1);
}

// DAA, with the Z80's N flag deciding add vs subtract -- the thing the 8080 could
// not do (its DAA always adds).
void CpuZ80::daa() {
    uint8_t a = a_;
    uint8_t lo = (uint8_t)(a & 0x0F);
    bool cf = flag(FC), hf = flag(FH), nf = flag(FN);
    uint8_t diff = 0;
    bool newC = false;
    if (cf || a > 0x99) { diff = 0x60; newC = true; }
    if (hf || lo > 9) diff |= 0x06;
    uint8_t res = (uint8_t)(nf ? a - diff : a + diff);
    uint8_t f = (uint8_t)(nf ? FN : 0);
    bool newH = nf ? (hf && lo < 6) : (lo > 9);
    if (newC) f |= FC;
    if (newH) f |= FH;
    f |= szxp(res);
    a_ = res;
    f_ = f;
}

void CpuZ80::neg() {
    uint8_t v = a_;
    uint8_t res = (uint8_t)(0 - v);
    uint8_t f = FN;
    if (res & 0x80) f |= FS;
    if (res == 0) f |= FZ;
    if (v & 0x0F) f |= FH;     // a borrow out of bit 3 unless the low nibble was 0
    if (v == 0x80) f |= FPV;
    if (v != 0) f |= FC;
    f |= (uint8_t)(res & (FF5 | FF3));
    a_ = res;
    f_ = f;
}

// The accumulator rotates: CARRY, and F5/F3 from the result. S/Z/PV are preserved,
// which is the difference from the CB-prefixed rotates below.
void CpuZ80::rlca() { uint8_t c = (uint8_t)(a_ >> 7); a_ = (uint8_t)((a_ << 1) | c);
    f_ = (uint8_t)((f_ & (FS | FZ | FPV)) | (a_ & (FF5 | FF3)) | (c ? FC : 0)); }
void CpuZ80::rrca() { uint8_t c = (uint8_t)(a_ & 1); a_ = (uint8_t)((a_ >> 1) | (c << 7));
    f_ = (uint8_t)((f_ & (FS | FZ | FPV)) | (a_ & (FF5 | FF3)) | (c ? FC : 0)); }
void CpuZ80::rla() { uint8_t c = (uint8_t)(a_ >> 7); a_ = (uint8_t)((a_ << 1) | (flag(FC) ? 1 : 0));
    f_ = (uint8_t)((f_ & (FS | FZ | FPV)) | (a_ & (FF5 | FF3)) | (c ? FC : 0)); }
void CpuZ80::rra() { uint8_t c = (uint8_t)(a_ & 1); a_ = (uint8_t)((a_ >> 1) | (flag(FC) ? 0x80 : 0));
    f_ = (uint8_t)((f_ & (FS | FZ | FPV)) | (a_ & (FF5 | FF3)) | (c ? FC : 0)); }

// CB-prefixed rotate/shift: full S Z F5 F3 P(arity) flags, H=N=0, C from the bit
// shifted out. SLL (kind 6) is the undocumented one that shifts a 1 into bit 0.
uint8_t CpuZ80::rot(int kind, uint8_t v) {
    uint8_t c = 0, r = 0;
    switch (kind) {
    case 0: c = (uint8_t)(v >> 7); r = (uint8_t)((v << 1) | c); break;              // RLC
    case 1: c = (uint8_t)(v & 1);  r = (uint8_t)((v >> 1) | (c << 7)); break;       // RRC
    case 2: c = (uint8_t)(v >> 7); r = (uint8_t)((v << 1) | (flag(FC) ? 1 : 0)); break;    // RL
    case 3: c = (uint8_t)(v & 1);  r = (uint8_t)((v >> 1) | (flag(FC) ? 0x80 : 0)); break; // RR
    case 4: c = (uint8_t)(v >> 7); r = (uint8_t)(v << 1); break;                    // SLA
    case 5: c = (uint8_t)(v & 1);  r = (uint8_t)((v & 0x80) | (v >> 1)); break;     // SRA
    case 6: c = (uint8_t)(v >> 7); r = (uint8_t)((v << 1) | 1); break;              // SLL (undoc)
    default: c = (uint8_t)(v & 1); r = (uint8_t)(v >> 1); break;                    // SRL
    }
    f_ = (uint8_t)(szxp(r) | (c ? FC : 0));
    return r;
}

// BIT n,r -- Z (and P/V) from the tested bit, S only for bit 7 set, H always set.
// F5/F3 come from `x53`: the tested value for a register, but the MEMPTR high byte
// for (HL) and the (IX+d) address high byte for the indexed form.
void CpuZ80::bit(uint8_t v, int n, uint8_t x53) {
    uint8_t r = (uint8_t)(v & (1 << n));
    uint8_t f = (uint8_t)((f_ & FC) | FH);
    if (r == 0) f |= (FZ | FPV);
    if (n == 7 && r) f |= FS;
    f |= (uint8_t)(x53 & (FF5 | FF3));
    f_ = f;
}

void CpuZ80::rld(Bus& bus, const Ctx& c) {
    uint16_t a = idxAddr(c);
    uint8_t m = bus.memRead(a);
    uint8_t nm = (uint8_t)((m << 4) | (a_ & 0x0F));
    a_ = (uint8_t)((a_ & 0xF0) | (m >> 4));
    bus.memWrite(a, nm);
    f_ = (uint8_t)((f_ & FC) | szxp(a_));
    wz_ = (uint16_t)(a + 1);
}
void CpuZ80::rrd(Bus& bus, const Ctx& c) {
    uint16_t a = idxAddr(c);
    uint8_t m = bus.memRead(a);
    uint8_t nm = (uint8_t)((a_ << 4) | (m >> 4));
    a_ = (uint8_t)((a_ & 0xF0) | (m & 0x0F));
    bus.memWrite(a, nm);
    f_ = (uint8_t)((f_ & FC) | szxp(a_));
    wz_ = (uint16_t)(a + 1);
}

// ---------------------------------------------------------------------------
// Block moves/searches/I-O. The undocumented F5/F3 rules here come from Sean
// Young 4.2; ZEXDOC masks them off, ZEXALL checks them.
// ---------------------------------------------------------------------------
void CpuZ80::blockLd(Bus& bus, int dir, bool repeat) {
    uint8_t v = bus.memRead(hl());
    bus.memWrite(de(), v);
    setHL((uint16_t)(hl() + dir));
    setDE((uint16_t)(de() + dir));
    uint16_t left = (uint16_t)(bc() - 1);
    setBC(left);
    uint8_t n = (uint8_t)(v + a_);
    uint8_t f = (uint8_t)(f_ & (FS | FZ | FC));
    if (left != 0) f |= FPV;
    if (n & 0x02) f |= FF5;
    if (n & 0x08) f |= FF3;
    f_ = f;  // H = N = 0
    if (repeat && left != 0) {
        pc_ = (uint16_t)(pc_ - 2);        // re-execute
        wz_ = (uint16_t)(pc_ + 1);
        f_ = (uint8_t)((f_ & ~(FF5 | FF3)) | ((pc_ >> 8) & (FF5 | FF3)));
    }
}

void CpuZ80::blockCp(Bus& bus, int dir, bool repeat) {
    uint8_t v = bus.memRead(hl());
    uint8_t res = (uint8_t)(a_ - v);
    bool h = (((a_ & 0xF) - (v & 0xF)) & 0x10) != 0;
    setHL((uint16_t)(hl() + dir));
    uint16_t left = (uint16_t)(bc() - 1);
    setBC(left);
    uint8_t f = (uint8_t)((f_ & FC) | FN);
    if (res & 0x80) f |= FS;
    if (res == 0) f |= FZ;
    if (h) f |= FH;
    if (left != 0) f |= FPV;
    uint8_t n = (uint8_t)(res - (h ? 1 : 0));
    if (n & 0x02) f |= FF5;
    if (n & 0x08) f |= FF3;
    f_ = f;
    wz_ = (uint16_t)(wz_ + dir);
    if (repeat && left != 0 && res != 0) {
        pc_ = (uint16_t)(pc_ - 2);
        wz_ = (uint16_t)(pc_ + 1);
        f_ = (uint8_t)((f_ & ~(FF5 | FF3)) | ((pc_ >> 8) & (FF5 | FF3)));
    }
}

void CpuZ80::blockIn(Bus& bus, int dir, bool repeat) {
    uint8_t v = bus.ioRead(c_);
    bus.memWrite(hl(), v);
    wz_ = (uint16_t)(bc() + dir);
    b_ = (uint8_t)(b_ - 1);
    setHL((uint16_t)(hl() + dir));
    uint8_t f = szx(b_);
    if (v & 0x80) f |= FN;
    unsigned k = (unsigned)v + ((c_ + dir) & 0xFF);
    if (k > 0xFF) f |= (FH | FC);
    if (kEvenParity[(uint8_t)((k & 7) ^ b_)]) f |= FPV;
    f_ = f;
    if (repeat && b_ != 0) pc_ = (uint16_t)(pc_ - 2);
}

void CpuZ80::blockOut(Bus& bus, int dir, bool repeat) {
    uint8_t v = bus.memRead(hl());
    b_ = (uint8_t)(b_ - 1);
    bus.ioWrite(c_, v);
    setHL((uint16_t)(hl() + dir));
    wz_ = (uint16_t)(bc() + dir);
    uint8_t f = szx(b_);
    if (v & 0x80) f |= FN;
    unsigned k = (unsigned)v + l_;
    if (k > 0xFF) f |= (FH | FC);
    if (kEvenParity[(uint8_t)((k & 7) ^ b_)]) f |= FPV;
    f_ = f;
    if (repeat && b_ != 0) pc_ = (uint16_t)(pc_ - 2);
}

// ---------------------------------------------------------------------------
// Reflection (DESIGN.md 3.0.3). REGS/SET REG/breakpoints/MCP render these with no
// change from the 8080 -- it is just a longer list. Flags first, PC last, matching
// the 8080's status-line convention; the alternate bank, I/R and interrupt state
// are reachable by name but kept off the one-line display (RegShow::Off/Field).
// ---------------------------------------------------------------------------
std::vector<RegDef> CpuZ80::registers() {
    auto flag = [this](const char* n, const char* lbl, const char* help, uint8_t mask) {
        return RegDef{n, 1, lbl, RegShow::Flag, help,
                      [this, mask] { return (uint32_t)((f_ & mask) ? 1 : 0); },
                      [this, mask](uint32_t v) { f_ = v ? (uint8_t)(f_ | mask) : (uint8_t)(f_ & ~mask); }};
    };
    auto half = [](const char* n, uint8_t* p) {
        return RegDef{n, 8, "", RegShow::Off, "", [p] { return (uint32_t)*p; },
                      [p](uint32_t v) { *p = (uint8_t)v; }};
    };
    auto pair = [](const char* n, const char* lbl, RegShow show, const char* help,
                   uint8_t* hi, uint8_t* lo) {
        return RegDef{n, 16, lbl, show, help,
                      [hi, lo] { return (uint32_t)((*hi << 8) | *lo); },
                      [hi, lo](uint32_t v) { *hi = (uint8_t)(v >> 8); *lo = (uint8_t)v; }};
    };
    auto word = [](const char* n, const char* lbl, RegShow show, const char* help, uint16_t* p) {
        return RegDef{n, 16, lbl, show, help, [p] { return (uint32_t)*p; },
                      [p](uint32_t v) { *p = (uint16_t)v; }};
    };
    auto byte = [](const char* n, const char* lbl, RegShow show, const char* help, uint8_t* p) {
        return RegDef{n, 8, lbl, show, help, [p] { return (uint32_t)*p; },
                      [p](uint32_t v) { *p = (uint8_t)v; }};
    };

    return {
        // Flag NAMES avoid the register letters -- `C` and `H` are the B,C,...H,L
        // register halves, so the carry and half-carry flags are `CY` and `HF`
        // (the 8080 core dodges the same clash by calling its carry `CY`). The
        // LABEL is the Z80's own S Z H P/V N C, which is what the status line shows.
        flag("CY", "C", "carry", FC),
        flag("Z", "Z", "zero", FZ),
        flag("S", "S", "sign -- negative", FS),
        flag("PV", "P", "parity / overflow", FPV),
        flag("HF", "H", "half carry", FH),
        flag("N", "N", "add/subtract (for DAA)", FN),

        {"A", 8, "", RegShow::Field, "accumulator", [this] { return (uint32_t)a_; },
         [this](uint32_t v) { a_ = (uint8_t)v; }},
        pair("BC", "B", RegShow::Field, "the B,C pair", &b_, &c_),
        pair("DE", "D", RegShow::Field, "the D,E pair", &d_, &e_),
        pair("HL", "H", RegShow::Field, "the H,L pair", &h_, &l_),
        word("IX", "IX", RegShow::Field, "index register X", &ix_),
        word("IY", "IY", RegShow::Field, "index register Y", &iy_),
        word("SP", "S", RegShow::Field, "stack pointer", &sp_),
        byte("I", "I", RegShow::Field, "interrupt vector base", &i_),
        {"IM", 8, "IM", RegShow::Field, "interrupt mode (0/1/2)", [this] { return (uint32_t)im_; },
         [this](uint32_t v) { im_ = (uint8_t)(v & 3); }},
        {"IFF1", 1, "IE", RegShow::Field, "interrupts enabled (IFF1)",
         [this] { return (uint32_t)(iff1_ ? 1 : 0); }, [this](uint32_t v) { iff1_ = v != 0; }},
        {"PC", 16, "P", RegShow::Field, "program counter", [this] { return (uint32_t)pc_; },
         [this](uint32_t v) { pc_ = (uint16_t)v; }},

        // Reachable by name, off the status line: the halves, the flag byte, the
        // alternate bank, R, IFF2 and WZ.
        half("B", &b_), half("C", &c_), half("D", &d_), half("E", &e_),
        half("H", &h_), half("L", &l_),
        {"F", 8, "", RegShow::Off, "flags: S Z F5 H F3 P/V N C", [this] { return (uint32_t)f_; },
         [this](uint32_t v) { f_ = (uint8_t)v; }},
        pair("AF'", "", RegShow::Off, "alternate AF", &a2_, &f2_),
        pair("BC'", "", RegShow::Off, "alternate BC", &b2_, &c2_),
        pair("DE'", "", RegShow::Off, "alternate DE", &d2_, &e2_),
        pair("HL'", "", RegShow::Off, "alternate HL", &h2_, &l2_),
        byte("R", "", RegShow::Off, "memory refresh", &r_),
        {"IFF2", 1, "", RegShow::Off, "the IFF shadow (copied to P/V by LD A,I)",
         [this] { return (uint32_t)(iff2_ ? 1 : 0); }, [this](uint32_t v) { iff2_ = v != 0; }},
        word("WZ", "", RegShow::Off, "MEMPTR internal register", &wz_),
    };
}

// Reset: PC and I and R to zero, interrupts off, IM 0, out of HALT. The Z80's
// reset does not clear the general registers, and neither does ours (DESIGN.md 6).
void CpuZ80::reset(Reset) {
    pc_ = 0;
    i_ = 0;
    r_ = 0;
    iff1_ = iff2_ = false;
    im_ = 0;
    eiPending_ = false;
    halted_ = false;
    intFetch_ = false;
    wz_ = 0;
}

bool CpuZ80::memFormMain(uint8_t op) {
    if (op >= 0x40 && op <= 0x7F) return op != 0x76 && ((op & 7) == 6 || ((op >> 3) & 7) == 6);
    if (op >= 0x80 && op <= 0xBF) return (op & 7) == 6;
    uint8_t lo = (uint8_t)(op & 0xC7);
    if (lo == 0x04 || lo == 0x05 || lo == 0x06) return ((op >> 3) & 7) == 6;  // INC/DEC/LD r,n
    return false;
}

// ---------------------------------------------------------------------------
// One instruction.
// ---------------------------------------------------------------------------
StepResult CpuZ80::step(Bus& bus) {
    // ---- The maskable interrupt, at the instruction boundary ----
    //
    // pINT is a LEVEL (bus.intPending() reads the wire-OR). If IFF1 is set we
    // acknowledge: IFF1/IFF2 clear, and the vector depends on the mode. IFF1 only
    // becomes true at the END of the instruction after EI, so an interrupt cannot
    // land in the one-instruction shadow of EI -- the same guard the 8080 has.
    if (iff1_ && bus.intPending()) {
        iff1_ = iff2_ = false;
        eiPending_ = false;
        halted_ = false;
        switch (im_) {
        case 1:
            push(bus, pc_);
            pc_ = 0x0038;
            wz_ = 0x0038;
            return {13, RunStatus::Ok};
        case 2: {
            uint8_t vec = bus.intAck();
            uint16_t p = (uint16_t)((i_ << 8) | vec);
            uint16_t lo = bus.memRead(p);
            uint16_t hi = bus.memRead((uint16_t)(p + 1));
            push(bus, pc_);
            pc_ = (uint16_t)(lo | (hi << 8));
            wz_ = pc_;
            return {19, RunStatus::Ok};
        }
        default:
            intFetch_ = true;   // IM 0: the opcode is driven onto the bus; fall through
            break;
        }
    } else if (halted_) {
        // HALT holds until an interrupt or reset. Time still passes -- the very
        // T-states that clock the board whose interrupt will wake it. R still ticks.
        r_ = (uint8_t)((r_ & 0x80) | ((r_ + 1) & 0x7F));
        return {4, RunStatus::Halted};
    }

    bool takingInterrupt = intFetch_;
    bool eiWasPending = eiPending_;

    uint8_t op = fetchOp(bus);
    StepResult res;

    if (op == 0xCB) {
        res = execCB(bus, Ctx{});
    } else if (op == 0xED) {
        res = execED(bus);
    } else if (op == 0xDD) {
        res = execIndexed(bus, Idx::IX);
    } else if (op == 0xFD) {
        res = execIndexed(bus, Idx::IY);
    } else {
        res = execMain(bus, op, Ctx{});
    }

    // EI's one-instruction delay: eiWasPending was latched BEFORE this instruction,
    // so the EI that armed the flag does not enable itself -- the next one does.
    if (eiWasPending && op != 0xFB) {
        iff1_ = iff2_ = true;
        eiPending_ = false;
    }

    if (takingInterrupt) intFetch_ = false;
    res.status = halted_ ? RunStatus::Halted : RunStatus::Ok;
    return res;
}

// ---------------------------------------------------------------------------
// DD / FD. Read the sub-opcode; a prefix chained onto another prefix is wasted
// silicon (only the last counts), so leave it for the next step. Otherwise
// pre-fetch the (IX+d) displacement if the opcode reaches memory, then run the
// ordinary main decode with the index context.
// ---------------------------------------------------------------------------
StepResult CpuZ80::execIndexed(Bus& bus, Idx idx) {
    uint8_t peekOp = intFetch_ ? 0x00 : bus.peek(pc_);
    if (peekOp == 0xDD || peekOp == 0xFD || peekOp == 0xED) return {4, RunStatus::Ok};

    uint8_t op = fetchOp(bus);
    if (op == 0xCB) { Ctx c; c.idx = idx; c.mem = true; return execDDCB(bus, c); }

    Ctx c;
    c.idx = idx;
    c.mem = memFormMain(op);
    if (c.mem) c.disp = (int8_t)fetchByte(bus);
    StepResult r = execMain(bus, op, c);
    r.tStates += 4;   // the prefix M1
    return r;
}

// ---------------------------------------------------------------------------
// The main (unprefixed, or DD/FD-indexed) decode. `c` says whether HL means IX/IY
// and carries the pre-fetched displacement.
// ---------------------------------------------------------------------------
StepResult CpuZ80::execMain(Bus& bus, uint8_t op, Ctx c) {
    uint32_t t = 4;

    // ---- LD r,r' -- 01dddsss, with 76 punched out for HALT ----
    if (op >= 0x40 && op <= 0x7F) {
        if (op == 0x76) {
            halted_ = true;
            intFetch_ = false;
            return {4, RunStatus::Halted};
        }
        int dst = (op >> 3) & 7, src = op & 7;
        setR(bus, dst, c, getR(bus, src, c));
        t = (dst == 6 || src == 6) ? 7 : 4;
        return {t, RunStatus::Ok};
    }
    // ---- ALU A,r -- 10ppp sss ----
    if (op >= 0x80 && op <= 0xBF) {
        int kind = (op >> 3) & 7, src = op & 7;
        uint8_t v = getR(bus, src, c);
        switch (kind) {
        case 0: addA(v, 0); break;
        case 1: addA(v, flag(FC) ? 1 : 0); break;
        case 2: subA(v, 0, true); break;
        case 3: subA(v, flag(FC) ? 1 : 0, true); break;
        case 4: andA(v); break;
        case 5: xorA(v); break;
        case 6: orA(v); break;
        default: subA(v, 0, false); break;  // CP
        }
        t = (src == 6) ? 7 : 4;
        return {t, RunStatus::Ok};
    }

    switch (op) {
    case 0x00: break;  // NOP

    // ---- 16-bit immediate loads: LD rp,nn ----
    case 0x01: setBC(fetch16(bus)); t = 10; break;
    case 0x11: setDE(fetch16(bus)); t = 10; break;
    case 0x21: setRP(2, c, fetch16(bus)); t = 10; break;
    case 0x31: sp_ = fetch16(bus); t = 10; break;

    // ---- LD (rp),A / LD A,(rp) ----
    case 0x02: bus.memWrite(bc(), a_); wz_ = (uint16_t)((a_ << 8) | ((bc() + 1) & 0xFF)); t = 7; break;
    case 0x12: bus.memWrite(de(), a_); wz_ = (uint16_t)((a_ << 8) | ((de() + 1) & 0xFF)); t = 7; break;
    case 0x0A: a_ = bus.memRead(bc()); wz_ = (uint16_t)(bc() + 1); t = 7; break;
    case 0x1A: a_ = bus.memRead(de()); wz_ = (uint16_t)(de() + 1); t = 7; break;

    case 0x32: { uint16_t a = fetch16(bus); bus.memWrite(a, a_);
                 wz_ = (uint16_t)((a_ << 8) | ((a + 1) & 0xFF)); t = 13; break; }  // LD (nn),A
    case 0x3A: { uint16_t a = fetch16(bus); a_ = bus.memRead(a); wz_ = (uint16_t)(a + 1); t = 13; break; }

    case 0x22: {  // LD (nn),HL/IX/IY
        uint16_t a = fetch16(bus);
        uint16_t v = hl16(c);
        bus.memWrite(a, (uint8_t)v);
        bus.memWrite((uint16_t)(a + 1), (uint8_t)(v >> 8));
        wz_ = (uint16_t)(a + 1);
        t = 16;
        break;
    }
    case 0x2A: {  // LD HL/IX/IY,(nn)
        uint16_t a = fetch16(bus);
        uint8_t lo = bus.memRead(a);
        uint8_t hi = bus.memRead((uint16_t)(a + 1));
        setRP(2, c, (uint16_t)(lo | (hi << 8)));
        wz_ = (uint16_t)(a + 1);
        t = 16;
        break;
    }

    // ---- INC/DEC rp -- no flags ----
    case 0x03: setBC((uint16_t)(bc() + 1)); t = 6; break;
    case 0x13: setDE((uint16_t)(de() + 1)); t = 6; break;
    case 0x23: setRP(2, c, (uint16_t)(hl16(c) + 1)); t = 6; break;
    case 0x33: ++sp_; t = 6; break;
    case 0x0B: setBC((uint16_t)(bc() - 1)); t = 6; break;
    case 0x1B: setDE((uint16_t)(de() - 1)); t = 6; break;
    case 0x2B: setRP(2, c, (uint16_t)(hl16(c) - 1)); t = 6; break;
    case 0x3B: --sp_; t = 6; break;

    // ---- ADD HL,rp ----
    case 0x09: addHL(c, getRP(0, c)); t = 11; break;
    case 0x19: addHL(c, getRP(1, c)); t = 11; break;
    case 0x29: addHL(c, hl16(c));     t = 11; break;
    case 0x39: addHL(c, sp_);         t = 11; break;

    // ---- INC/DEC r -- 00 rrr 10x ----
    case 0x04: case 0x0C: case 0x14: case 0x1C:
    case 0x24: case 0x2C: case 0x34: case 0x3C: {
        int r = (op >> 3) & 7;
        setR(bus, r, c, incR(getR(bus, r, c)));
        t = (r == 6) ? 11 : 4;
        break;
    }
    case 0x05: case 0x0D: case 0x15: case 0x1D:
    case 0x25: case 0x2D: case 0x35: case 0x3D: {
        int r = (op >> 3) & 7;
        setR(bus, r, c, decR(getR(bus, r, c)));
        t = (r == 6) ? 11 : 4;
        break;
    }

    // ---- LD r,n -- 00 rrr 110. For (HL)/(IX+d) the displacement was already
    // pre-fetched into c.disp, so the immediate is next in the stream. ----
    case 0x06: case 0x0E: case 0x16: case 0x1E:
    case 0x26: case 0x2E: case 0x36: case 0x3E: {
        int r = (op >> 3) & 7;
        uint8_t imm = fetchByte(bus);
        setR(bus, r, c, imm);
        t = (r == 6) ? 10 : 7;
        break;
    }

    // ---- accumulator rotates and the flag ops ----
    case 0x07: rlca(); break;
    case 0x0F: rrca(); break;
    case 0x17: rla(); break;
    case 0x1F: rra(); break;
    case 0x27: daa(); break;
    case 0x2F: a_ = (uint8_t)~a_;
               f_ = (uint8_t)((f_ & (FS | FZ | FPV | FC)) | FH | FN | (a_ & (FF5 | FF3))); break;  // CPL
    case 0x37: f_ = (uint8_t)((f_ & (FS | FZ | FPV)) | FC | (a_ & (FF5 | FF3))); break;             // SCF
    case 0x3F: { bool cy = flag(FC);   // CCF
                 f_ = (uint8_t)((f_ & (FS | FZ | FPV)) | (cy ? FH : 0) | (cy ? 0 : FC) | (a_ & (FF5 | FF3)));
                 break; }

    // ---- relative jumps ----
    case 0x18: { int8_t e = (int8_t)fetchByte(bus); pc_ = (uint16_t)(pc_ + e); wz_ = pc_; t = 12; break; }  // JR
    case 0x20: case 0x28: case 0x30: case 0x38: {  // JR cc
        int cc = (op >> 3) & 3;   // NZ Z NC C
        bool take = cc == 0 ? !flag(FZ) : cc == 1 ? flag(FZ) : cc == 2 ? !flag(FC) : flag(FC);
        int8_t e = (int8_t)fetchByte(bus);
        if (take) { pc_ = (uint16_t)(pc_ + e); wz_ = pc_; t = 12; } else t = 7;
        break;
    }
    case 0x10: {  // DJNZ
        int8_t e = (int8_t)fetchByte(bus);
        b_ = (uint8_t)(b_ - 1);
        if (b_ != 0) { pc_ = (uint16_t)(pc_ + e); wz_ = pc_; t = 13; } else t = 8;
        break;
    }

    // ---- EX / EXX ----
    case 0x08: { uint8_t t2 = a_; a_ = a2_; a2_ = t2; t2 = f_; f_ = f2_; f2_ = t2; break; }  // EX AF,AF'
    case 0xEB: { uint8_t th = h_, tl = l_; h_ = d_; l_ = e_; d_ = th; e_ = tl; break; }      // EX DE,HL
    case 0xD9: {  // EXX
        uint8_t t2;
        t2 = b_; b_ = b2_; b2_ = t2;  t2 = c_; c_ = c2_; c2_ = t2;
        t2 = d_; d_ = d2_; d2_ = t2;  t2 = e_; e_ = e2_; e2_ = t2;
        t2 = h_; h_ = h2_; h2_ = t2;  t2 = l_; l_ = l2_; l2_ = t2;
        break;
    }
    case 0xE3: {  // EX (SP),HL/IX/IY
        uint16_t v = hl16(c);
        uint8_t lo = bus.memRead(sp_);
        uint8_t hi = bus.memRead((uint16_t)(sp_ + 1));
        bus.memWrite(sp_, (uint8_t)v);
        bus.memWrite((uint16_t)(sp_ + 1), (uint8_t)(v >> 8));
        setRP(2, c, (uint16_t)(lo | (hi << 8)));
        wz_ = (uint16_t)(lo | (hi << 8));
        t = 19;
        break;
    }

    // ---- immediate ALU ----
    case 0xC6: addA(fetchByte(bus), 0); t = 7; break;
    case 0xCE: addA(fetchByte(bus), flag(FC) ? 1 : 0); t = 7; break;
    case 0xD6: subA(fetchByte(bus), 0, true); t = 7; break;
    case 0xDE: subA(fetchByte(bus), flag(FC) ? 1 : 0, true); t = 7; break;
    case 0xE6: andA(fetchByte(bus)); t = 7; break;
    case 0xEE: xorA(fetchByte(bus)); t = 7; break;
    case 0xF6: orA(fetchByte(bus)); t = 7; break;
    case 0xFE: subA(fetchByte(bus), 0, false); t = 7; break;   // CP

    // ---- jumps ----
    case 0xC3: pc_ = fetch16(bus); wz_ = pc_; t = 10; break;
    case 0xC2: case 0xCA: case 0xD2: case 0xDA:
    case 0xE2: case 0xEA: case 0xF2: case 0xFA: {
        uint16_t a = fetch16(bus);
        wz_ = a;
        if (cond((op >> 3) & 7)) pc_ = a;
        t = 10;
        break;
    }
    case 0xE9: pc_ = hl16(c); t = 4; break;   // JP (HL)/(IX)/(IY) -- no memory read

    // ---- calls ----
    case 0xCD: { uint16_t a = fetch16(bus); wz_ = a; push(bus, pc_); pc_ = a; t = 17; break; }
    case 0xC4: case 0xCC: case 0xD4: case 0xDC:
    case 0xE4: case 0xEC: case 0xF4: case 0xFC: {
        uint16_t a = fetch16(bus);
        wz_ = a;
        if (cond((op >> 3) & 7)) { push(bus, pc_); pc_ = a; t = 17; } else t = 10;
        break;
    }

    // ---- returns ----
    case 0xC9: pc_ = pop(bus); wz_ = pc_; t = 10; break;
    case 0xC0: case 0xC8: case 0xD0: case 0xD8:
    case 0xE0: case 0xE8: case 0xF0: case 0xF8:
        if (cond((op >> 3) & 7)) { pc_ = pop(bus); wz_ = pc_; t = 11; } else t = 5;
        break;

    // ---- RST ----
    case 0xC7: case 0xCF: case 0xD7: case 0xDF:
    case 0xE7: case 0xEF: case 0xF7: case 0xFF:
        push(bus, pc_);
        pc_ = (uint16_t)(op & 0x38);
        wz_ = pc_;
        t = 11;
        break;

    // ---- stack ----
    case 0xC5: push(bus, bc()); t = 11; break;
    case 0xD5: push(bus, de()); t = 11; break;
    case 0xE5: push(bus, hl16(c)); t = 11; break;
    case 0xF5: push(bus, af()); t = 11; break;
    case 0xC1: setBC(pop(bus)); t = 10; break;
    case 0xD1: setDE(pop(bus)); t = 10; break;
    case 0xE1: setRP(2, c, pop(bus)); t = 10; break;
    case 0xF1: { uint16_t v = pop(bus); a_ = (uint8_t)(v >> 8); f_ = (uint8_t)v; t = 10; break; }

    case 0xF9: sp_ = hl16(c); t = 6; break;   // LD SP,HL/IX/IY

    // ---- I/O ----
    case 0xDB: { uint8_t port = fetchByte(bus);   // IN A,(n)
                 wz_ = (uint16_t)(((a_ << 8) | port) + 1);
                 a_ = bus.ioRead(port); t = 11; break; }
    case 0xD3: { uint8_t port = fetchByte(bus);   // OUT (n),A
                 bus.ioWrite(port, a_);
                 wz_ = (uint16_t)((a_ << 8) | ((port + 1) & 0xFF)); t = 11; break; }

    case 0xF3: iff1_ = iff2_ = false; eiPending_ = false; break;  // DI
    case 0xFB: eiPending_ = true; break;                          // EI

    default:
        break;  // every opcode is covered; a stray falls through as a 4T NOP
    }

    return {t, RunStatus::Ok};
}

// ---------------------------------------------------------------------------
// CB -- rotate/shift and bit ops on a register or (HL). Fully regular.
// ---------------------------------------------------------------------------
StepResult CpuZ80::execCB(Bus& bus, const Ctx& c) {
    uint8_t op = fetchOp(bus);
    int y = (op >> 3) & 7, z = op & 7;
    uint32_t t = (z == 6) ? 15 : 8;

    if (op < 0x40) {                    // rotates/shifts
        setR(bus, z, c, rot(y, getR(bus, z, c)));
    } else if (op < 0x80) {             // BIT y,r
        uint8_t v = getR(bus, z, c);
        // For (HL), F5/F3 come from the MEMPTR high byte, not the value read.
        uint8_t x53 = (z == 6) ? (uint8_t)(wz_ >> 8) : v;
        bit(v, y, x53);
        t = (z == 6) ? 12 : 8;
    } else if (op < 0xC0) {             // RES y,r
        setR(bus, z, c, (uint8_t)(getR(bus, z, c) & ~(1 << y)));
    } else {                            // SET y,r
        setR(bus, z, c, (uint8_t)(getR(bus, z, c) | (1 << y)));
    }
    return {t, RunStatus::Ok};
}

// ---------------------------------------------------------------------------
// DD CB dd op / FD CB dd op -- four bytes, displacement BEFORE the opcode. The op
// acts on (IX+d); a register field other than 6 ALSO copies the result into that
// register (undocumented). BIT ignores the register field entirely.
// ---------------------------------------------------------------------------
StepResult CpuZ80::execDDCB(Bus& bus, const Ctx& c) {
    Ctx cc = c;
    cc.disp = (int8_t)fetchByte(bus);
    uint8_t op = fetchOp(bus);     // this CB sub-opcode is an M1 too
    int y = (op >> 3) & 7, z = op & 7;
    uint16_t a = idxAddr(cc);      // IX+d, and latches WZ
    uint8_t v = bus.memRead(a);

    if (op >= 0x40 && op < 0x80) {         // BIT y,(IX+d) -- F5/F3 from the address high byte
        bit(v, y, (uint8_t)(a >> 8));
        return {20, RunStatus::Ok};
    }

    uint8_t r;
    if (op < 0x40) r = rot(y, v);                       // rotate/shift
    else if (op < 0xC0) r = (uint8_t)(v & ~(1 << y));   // RES
    else r = (uint8_t)(v | (1 << y));                   // SET
    bus.memWrite(a, r);
    if (z != 6) *reg8(z) = r;    // the undocumented reg-copy
    return {23, RunStatus::Ok};
}

// ---------------------------------------------------------------------------
// ED -- the extended page: block ops, 16-bit arithmetic, I register moves, I/O.
// Undefined slots are 2-byte NOPs.
// ---------------------------------------------------------------------------
StepResult CpuZ80::execED(Bus& bus) {
    uint8_t op = fetchOp(bus);
    Ctx hl;  // ED never indexes -- DD ED is a wasted DD, handled in execIndexed

    if (op >= 0x40 && op <= 0x7F) {
        int y = (op >> 3) & 7, z = op & 7;
        int rp = (op >> 4) & 3;
        switch (z) {
        case 0: {  // IN r,(C) -- y==6 reads and only sets flags
            uint8_t v = bus.ioRead(c_);
            wz_ = (uint16_t)(bc() + 1);
            f_ = (uint8_t)((f_ & FC) | szxp(v));
            if (y != 6) *reg8(y) = v;
            return {12, RunStatus::Ok};
        }
        case 1:  // OUT (C),r -- y==6 outputs 0
            bus.ioWrite(c_, y == 6 ? 0 : *reg8(y));
            wz_ = (uint16_t)(bc() + 1);
            return {12, RunStatus::Ok};
        case 2:  // SBC/ADC HL,rp
            if (op & 8) adcHL(getRP(rp, hl)); else sbcHL(getRP(rp, hl));
            return {15, RunStatus::Ok};
        case 3: {  // LD (nn),rp / LD rp,(nn)
            uint16_t a = fetch16(bus);
            wz_ = (uint16_t)(a + 1);
            if (op & 8) {
                uint8_t lo = bus.memRead(a), hi = bus.memRead((uint16_t)(a + 1));
                setRP(rp, hl, (uint16_t)(lo | (hi << 8)));
            } else {
                uint16_t v = getRP(rp, hl);
                bus.memWrite(a, (uint8_t)v);
                bus.memWrite((uint16_t)(a + 1), (uint8_t)(v >> 8));
            }
            return {20, RunStatus::Ok};
        }
        case 4: neg(); return {8, RunStatus::Ok};
        case 5:  // RETN (45) / RETI (4D) and their undocumented twins
            iff1_ = iff2_;
            pc_ = pop(bus);
            wz_ = pc_;
            return {14, RunStatus::Ok};
        case 6:  // IM
            im_ = (op == 0x56 || op == 0x76) ? 1 : (op == 0x5E || op == 0x7E) ? 2 : 0;
            return {8, RunStatus::Ok};
        default:  // z == 7
            switch (op) {
            case 0x47: i_ = a_; return {9, RunStatus::Ok};   // LD I,A
            case 0x4F: r_ = a_; return {9, RunStatus::Ok};   // LD R,A
            case 0x57:  // LD A,I
                a_ = i_;
                f_ = (uint8_t)((f_ & FC) | szx(a_) | (iff2_ ? FPV : 0));
                return {9, RunStatus::Ok};
            case 0x5F:  // LD A,R
                a_ = r_;
                f_ = (uint8_t)((f_ & FC) | szx(a_) | (iff2_ ? FPV : 0));
                return {9, RunStatus::Ok};
            case 0x67: rrd(bus, hl); return {18, RunStatus::Ok};
            case 0x6F: rld(bus, hl); return {18, RunStatus::Ok};
            default: return {8, RunStatus::Ok};   // 77, 7F -- NOP
            }
        }
    }

    switch (op) {
    case 0xA0: blockLd(bus, +1, false); return {16, RunStatus::Ok};  // LDI
    case 0xA8: blockLd(bus, -1, false); return {16, RunStatus::Ok};  // LDD
    case 0xB0: blockLd(bus, +1, true);  return {(bc() != 0) ? 21u : 16u, RunStatus::Ok};  // LDIR
    case 0xB8: blockLd(bus, -1, true);  return {(bc() != 0) ? 21u : 16u, RunStatus::Ok};  // LDDR
    case 0xA1: blockCp(bus, +1, false); return {16, RunStatus::Ok};  // CPI
    case 0xA9: blockCp(bus, -1, false); return {16, RunStatus::Ok};  // CPD
    case 0xB1: blockCp(bus, +1, true);  return {16, RunStatus::Ok};  // CPIR
    case 0xB9: blockCp(bus, -1, true);  return {16, RunStatus::Ok};  // CPDR
    case 0xA2: blockIn(bus, +1, false); return {16, RunStatus::Ok};  // INI
    case 0xAA: blockIn(bus, -1, false); return {16, RunStatus::Ok};  // IND
    case 0xB2: blockIn(bus, +1, true);  return {16, RunStatus::Ok};  // INIR
    case 0xBA: blockIn(bus, -1, true);  return {16, RunStatus::Ok};  // INDR
    case 0xA3: blockOut(bus, +1, false); return {16, RunStatus::Ok}; // OUTI
    case 0xAB: blockOut(bus, -1, false); return {16, RunStatus::Ok}; // OUTD
    case 0xB3: blockOut(bus, +1, true);  return {16, RunStatus::Ok}; // OTIR
    case 0xBB: blockOut(bus, -1, true);  return {16, RunStatus::Ok}; // OTDR
    default: return {8, RunStatus::Ok};   // undefined ED -- a 2-byte NOP
    }
}

// SNAPSHOT/RESTORE (DESIGN.md 13). The whole architectural state, plus the four
// bits registers() cannot show: WZ/MEMPTR (it leaks into F5/F3), IFF2 (LD A,I
// copies it into P/V), the interrupt mode, and the two EI/INTA latches. Miss any
// of them and a snapshot taken between an EI and its RET, or mid-block-transfer,
// resumes subtly wrong.
void CpuZ80::serialize(StateWriter& w) const {
    w.u8(a_); w.u8(f_); w.u8(b_); w.u8(c_); w.u8(d_); w.u8(e_); w.u8(h_); w.u8(l_);
    w.u8(a2_); w.u8(f2_); w.u8(b2_); w.u8(c2_); w.u8(d2_); w.u8(e2_); w.u8(h2_); w.u8(l2_);
    w.u16(ix_); w.u16(iy_); w.u16(sp_); w.u16(pc_);
    w.u8(i_); w.u8(r_);
    w.u16(wz_);
    w.u8(im_);
    w.boolean(iff1_);
    w.boolean(iff2_);
    w.boolean(halted_);
    w.boolean(eiPending_);
    w.boolean(intFetch_);
}

void CpuZ80::deserialize(StateReader& r) {
    a_ = r.u8(); f_ = r.u8(); b_ = r.u8(); c_ = r.u8(); d_ = r.u8(); e_ = r.u8(); h_ = r.u8(); l_ = r.u8();
    a2_ = r.u8(); f2_ = r.u8(); b2_ = r.u8(); c2_ = r.u8(); d2_ = r.u8(); e2_ = r.u8(); h2_ = r.u8(); l2_ = r.u8();
    ix_ = r.u16(); iy_ = r.u16(); sp_ = r.u16(); pc_ = r.u16();
    i_ = r.u8(); r_ = r.u8();
    wz_ = r.u16();
    im_ = r.u8();
    iff1_      = r.boolean();
    iff2_      = r.boolean();
    halted_    = r.boolean();
    eiPending_ = r.boolean();
    intFetch_  = r.boolean();
}

} // namespace altair
