#include "cpu/cpu6800.h"

#include "core/statefile.h"

namespace altair {

// ---------------------------------------------------------------------------
// Fetch and store. Every one is a REAL bus cycle, and every 16-bit access is
// BIG-ENDIAN -- high byte at the lower address (Programming Manual 2).
// ---------------------------------------------------------------------------
uint16_t Cpu6800::fetch16(Bus& bus) {
    uint8_t hi = fetch(bus);
    uint8_t lo = fetch(bus);
    return (uint16_t)((hi << 8) | lo);
}

uint16_t Cpu6800::read16(Bus& bus, uint16_t a) const {
    uint8_t hi = bus.memRead(a);
    uint8_t lo = bus.memRead((uint16_t)(a + 1));
    return (uint16_t)((hi << 8) | lo);
}

// The 6800 stack grows DOWN: a push writes at SP and then decrements, so the
// first byte pushed ends up at the highest address. A pull increments first,
// then reads.
void Cpu6800::push8(Bus& bus, uint8_t v) {
    bus.memWrite(sp_, v);
    sp_ = (uint16_t)(sp_ - 1);
}

uint8_t Cpu6800::pull8(Bus& bus) {
    sp_ = (uint16_t)(sp_ + 1);
    return bus.memRead(sp_);
}

// The interrupt/SWI/WAI stack frame, in hardware order: PCL, PCH, IXL, IXH, A,
// B, CCR. CCR ends up on top (lowest address), which is why RTI pulls it first.
void Cpu6800::pushState(Bus& bus) {
    push8(bus, (uint8_t)pc_);
    push8(bus, (uint8_t)(pc_ >> 8));
    push8(bus, (uint8_t)x_);
    push8(bus, (uint8_t)(x_ >> 8));
    push8(bus, a_);
    push8(bus, b_);
    push8(bus, cc());
}

void Cpu6800::rti(Bus& bus) {
    setCc(pull8(bus));
    b_ = pull8(bus);
    a_ = pull8(bus);
    uint8_t xh = pull8(bus);
    uint8_t xl = pull8(bus);
    x_ = (uint16_t)((xh << 8) | xl);
    uint8_t pch = pull8(bus);
    uint8_t pcl = pull8(bus);
    pc_ = (uint16_t)((pch << 8) | pcl);
}

// ---------------------------------------------------------------------------
// The condition code register: 1 1 H I N Z V C. The top two bits read as 1, so
// TPA / an interrupt push / RTI round-trip through those constants.
// ---------------------------------------------------------------------------
uint8_t Cpu6800::cc() const {
    uint8_t v = 0xC0;
    if (hf_) v |= 0x20;
    if (if_) v |= 0x10;
    if (nf_) v |= 0x08;
    if (zf_) v |= 0x04;
    if (vf_) v |= 0x02;
    if (cf_) v |= 0x01;
    return v;
}

void Cpu6800::setCc(uint8_t v) {
    hf_ = (v & 0x20) != 0;
    if_ = (v & 0x10) != 0;
    nf_ = (v & 0x08) != 0;
    zf_ = (v & 0x04) != 0;
    vf_ = (v & 0x02) != 0;
    cf_ = (v & 0x01) != 0;
}

// ---------------------------------------------------------------------------
// ALU primitives. The comments name the ONE thing each rule gets wrong if copied
// from 8080 habit.
// ---------------------------------------------------------------------------

// ADD sets H (carry out of bit 3) and V (signed overflow: operands same sign,
// result differs). ADC folds carry in.
uint8_t Cpu6800::add8(uint8_t a, uint8_t m, bool carry) {
    unsigned ci = carry ? 1 : 0;
    unsigned r = (unsigned)a + m + ci;
    hf_ = ((a & 0x0F) + (m & 0x0F) + ci) > 0x0F;
    cf_ = r > 0xFF;
    uint8_t res = (uint8_t)r;
    vf_ = ((a ^ res) & (m ^ res) & 0x80) != 0;
    setNZ8(res);
    return res;
}

// SUB / SBC / CMP / SBA / CBA. C is a BORROW (unsigned underflow); V is signed
// overflow. H IS NOT AFFECTED -- the single most common 8080-habit bug on a 6800.
uint8_t Cpu6800::sub8(uint8_t a, uint8_t m, bool borrow) {
    unsigned bi = borrow ? 1 : 0;
    unsigned r = (unsigned)a - m - bi;
    cf_ = (r & 0x100) != 0;
    uint8_t res = (uint8_t)r;
    vf_ = ((a ^ m) & (a ^ res) & 0x80) != 0;
    setNZ8(res);
    return res;
}

uint8_t Cpu6800::and8(uint8_t a, uint8_t m) {
    uint8_t r = a & m;
    setNZ8(r);
    vf_ = false;
    return r;
}

uint8_t Cpu6800::or8(uint8_t a, uint8_t m) {
    uint8_t r = a | m;
    setNZ8(r);
    vf_ = false;
    return r;
}

uint8_t Cpu6800::eor8(uint8_t a, uint8_t m) {
    uint8_t r = a ^ m;
    setNZ8(r);
    vf_ = false;
    return r;
}

// NEG = 0 - m. V set only when m was 0x80 (its own negative); C set unless m was
// 0 (0-0 borrows nothing). H untouched.
uint8_t Cpu6800::neg8(uint8_t m) {
    uint8_t r = (uint8_t)(0 - m);
    vf_ = (m == 0x80);
    cf_ = (m != 0x00);
    setNZ8(r);
    return r;
}

// COM = ones-complement. C is ALWAYS SET (it makes COM+INC a two's-complement
// idiom carry correctly), V cleared.
uint8_t Cpu6800::com8(uint8_t m) {
    uint8_t r = (uint8_t)~m;
    setNZ8(r);
    vf_ = false;
    cf_ = true;
    return r;
}

// INC / DEC touch N Z V but NOT C -- so a counter can be stepped inside a
// multi-byte add without disturbing the carry it is propagating. V marks the
// signed wrap (7F->80 for INC, 80->7F for DEC).
uint8_t Cpu6800::inc8(uint8_t m) {
    uint8_t r = (uint8_t)(m + 1);
    vf_ = (m == 0x7F);
    setNZ8(r);
    return r;
}

uint8_t Cpu6800::dec8(uint8_t m) {
    uint8_t r = (uint8_t)(m - 1);
    vf_ = (m == 0x80);
    setNZ8(r);
    return r;
}

uint8_t Cpu6800::clr8() {
    nf_ = false;
    zf_ = true;
    vf_ = false;
    cf_ = false;
    return 0;
}

// TST compares against zero: N and Z from the value, V and C cleared. It does not
// write anything back.
void Cpu6800::tst8(uint8_t m) {
    setNZ8(m);
    vf_ = false;
    cf_ = false;
}

// Shifts and rotates all clock the shifted-out bit into C and set V = N ^ C, with
// N and C taken AFTER the shift.
uint8_t Cpu6800::asl8(uint8_t m) {
    cf_ = (m & 0x80) != 0;
    uint8_t r = (uint8_t)(m << 1);
    setNZ8(r);
    vf_ = nf_ ^ cf_;
    return r;
}

uint8_t Cpu6800::asr8(uint8_t m) {
    cf_ = (m & 0x01) != 0;
    uint8_t r = (uint8_t)((m >> 1) | (m & 0x80));  // sign preserved
    setNZ8(r);
    vf_ = nf_ ^ cf_;
    return r;
}

uint8_t Cpu6800::lsr8(uint8_t m) {
    cf_ = (m & 0x01) != 0;
    uint8_t r = (uint8_t)(m >> 1);  // bit 7 in -> 0, so N is always 0
    setNZ8(r);
    vf_ = nf_ ^ cf_;
    return r;
}

uint8_t Cpu6800::rol8(uint8_t m) {
    bool oldC = cf_;
    cf_ = (m & 0x80) != 0;
    uint8_t r = (uint8_t)((m << 1) | (oldC ? 1 : 0));
    setNZ8(r);
    vf_ = nf_ ^ cf_;
    return r;
}

uint8_t Cpu6800::ror8(uint8_t m) {
    bool oldC = cf_;
    cf_ = (m & 0x01) != 0;
    uint8_t r = (uint8_t)((m >> 1) | (oldC ? 0x80 : 0));
    setNZ8(r);
    vf_ = nf_ ^ cf_;
    return r;
}

// LDX/LDS/STX/STS: N is bit 15, Z the whole 16 bits, V cleared, C untouched.
void Cpu6800::ld16(uint16_t v) {
    nf_ = (v & 0x8000) != 0;
    zf_ = (v == 0);
    vf_ = false;
}

// CPX is the 6800's notorious compare: it sets N, Z and V from the 16-bit
// subtraction but LEAVES CARRY ALONE. We model N/Z/V from the full-width result
// (as MAME does) rather than reproducing the silicon's high-byte-only N/V quirk,
// which only mattered to code that then branched on the buggy signed result; the
// C-untouched rule -- the part real monitors rely on -- is exact.
void Cpu6800::cpx(uint16_t m) {
    uint16_t r = (uint16_t)(x_ - m);
    nf_ = (r & 0x8000) != 0;
    zf_ = (r == 0);
    vf_ = ((x_ ^ m) & (x_ ^ r) & 0x8000) != 0;
}

// DAA: decimal-adjust A after a BCD add, straight from the M6800 datasheet table.
// The low correction (+06) fires on a half-carry or a low nibble > 9; the high
// correction (+60) on carry, a high nibble > 9, or a 9-high-nibble that itself
// carried. C after is exactly "did the high correction fire", so a carry that was
// already set is never cleared. V is left undefined-as-zero (matches MAME).
void Cpu6800::daa() {
    uint8_t hi = a_ >> 4;
    uint8_t lo = a_ & 0x0F;
    uint8_t add = 0;
    if (hf_ || lo > 9) add |= 0x06;
    if (cf_ || hi > 9 || (hi == 9 && lo > 9)) add |= 0x60;
    a_ = (uint8_t)(a_ + add);
    cf_ = (add & 0x60) != 0;
    vf_ = false;
    setNZ8(a_);
}

// ---------------------------------------------------------------------------
// Reflection (DESIGN.md 3.0.3). The CCR prints as six lamps in hardware order
// H I N Z V C, then the accumulators, index, stack and PC. `CC` is reachable by
// name (SET REG CC=.., breakpoint conditions) but stays off the clustered line.
// ---------------------------------------------------------------------------
std::vector<RegDef> Cpu6800::registers() {
    auto flag = [](const char* n, const char* help, bool* p) {
        return RegDef{n, 1, n, RegShow::Flag, help, [p] { return (uint32_t)(*p ? 1 : 0); },
                      [p](uint32_t v) { *p = v != 0; }};
    };

    return {
        flag("H", "half carry", &hf_),
        flag("I", "interrupt mask", &if_),
        flag("N", "negative", &nf_),
        flag("Z", "zero", &zf_),
        flag("V", "overflow", &vf_),
        flag("C", "carry", &cf_),

        {"A", 8, "", RegShow::Field, "accumulator A", [this] { return (uint32_t)a_; },
         [this](uint32_t v) { a_ = (uint8_t)v; }},
        {"B", 8, "", RegShow::Field, "accumulator B", [this] { return (uint32_t)b_; },
         [this](uint32_t v) { b_ = (uint8_t)v; }},
        {"X", 16, "X", RegShow::Field, "index register", [this] { return (uint32_t)x_; },
         [this](uint32_t v) { x_ = (uint16_t)v; }},
        {"SP", 16, "SP", RegShow::Field, "stack pointer", [this] { return (uint32_t)sp_; },
         [this](uint32_t v) { sp_ = (uint16_t)v; }},
        {"PC", 16, "PC", RegShow::Field, "program counter", [this] { return (uint32_t)pc_; },
         [this](uint32_t v) { pc_ = (uint16_t)v; }},

        {"CC", 8, "", RegShow::Off, "condition codes: 1 1 H I N Z V C",
         [this] { return (uint32_t)cc(); }, [this](uint32_t v) { setCc((uint8_t)v); }},
    };
}

// Reset sets the I mask and arms the vector fetch. It touches NO registers and NO
// memory -- the 6800's reset does neither, and the FFFE/FFFF read is deferred to
// the first step() so that stays literally true (DESIGN.md 6).
void Cpu6800::reset(Reset) {
    if_ = true;
    waiting_ = false;
    nmiPending_ = false;
    fetchResetVector_ = true;
}

// The vector sequence, shared by IRQ/NMI/SWI. Outside a WAI it stacks the frame
// (12 cycles); a WAI has already stacked it, so the take is only 4 cycles -- the
// whole reason WAI exists. Either way I is set and PC comes from the vector.
uint32_t Cpu6800::takeInterrupt(Bus& bus, uint16_t vector) {
    uint32_t t;
    if (waiting_) {
        waiting_ = false;
        t = 4;
    } else {
        pushState(bus);
        t = 12;
    }
    if_ = true;
    pc_ = read16(bus, vector);
    return t;
}

uint32_t Cpu6800::undefinedOp() {
    // 59 of the 256 opcodes are undefined on the 6800, with no published effect
    // (some really do lock the chip). We treat one as an inert 2-cycle step: the
    // opcode byte is already consumed, matching the disassembler's one-byte skip,
    // and nothing else changes.
    return 2;
}

// ---------------------------------------------------------------------------
// Relative branch. The offset is signed and measured from the address AFTER the
// two-byte instruction, which is exactly where PC sits once both bytes are read.
// ---------------------------------------------------------------------------
void Cpu6800::branch(Bus& bus, bool take) {
    int8_t off = (int8_t)fetch(bus);
    if (take) pc_ = (uint16_t)(pc_ + off);
}

// ---- operand fetch by mode (0=immediate 1=direct 2=indexed 3=extended) ----
void Cpu6800::fetchOperand8(Bus& bus, int mode, uint8_t& m, uint32_t& t) {
    switch (mode) {
    case 0: m = fetch(bus); t = 2; break;
    case 1: m = bus.memRead(fetch(bus)); t = 3; break;
    case 2: m = bus.memRead((uint16_t)(x_ + fetch(bus))); t = 5; break;
    default: m = bus.memRead(fetch16(bus)); t = 4; break;
    }
}

void Cpu6800::fetchOperand16(Bus& bus, int mode, uint16_t& m, uint32_t& t) {
    switch (mode) {
    case 0: m = fetch16(bus); t = 3; break;
    case 1: m = read16(bus, fetch(bus)); t = 4; break;
    case 2: m = read16(bus, (uint16_t)(x_ + fetch(bus))); t = 6; break;
    default: m = read16(bus, fetch16(bus)); t = 5; break;
    }
}

void Cpu6800::addrOperand8store(Bus& bus, int mode, uint16_t& addr, uint32_t& t) {
    switch (mode) {
    case 1: addr = fetch(bus); t = 4; break;
    case 2: addr = (uint16_t)(x_ + fetch(bus)); t = 6; break;
    default: addr = fetch16(bus); t = 5; break;
    }
}

void Cpu6800::addrOperand16store(Bus& bus, int mode, uint16_t& addr, uint32_t& t) {
    switch (mode) {
    case 1: addr = fetch(bus); t = 5; break;
    case 2: addr = (uint16_t)(x_ + fetch(bus)); t = 7; break;
    default: addr = fetch16(bus); t = 6; break;
    }
}

// ---- 40-5F: single-operand read-modify-write on an accumulator ----
uint32_t Cpu6800::accInherentRmw(uint8_t op) {
    uint8_t* acc = (op & 0x10) ? &b_ : &a_;  // 40-4F = A, 50-5F = B
    uint8_t v = *acc;
    switch (op & 0x0F) {
    case 0x0: *acc = neg8(v); break;
    case 0x3: *acc = com8(v); break;
    case 0x4: *acc = lsr8(v); break;
    case 0x6: *acc = ror8(v); break;
    case 0x7: *acc = asr8(v); break;
    case 0x8: *acc = asl8(v); break;
    case 0x9: *acc = rol8(v); break;
    case 0xA: *acc = dec8(v); break;
    case 0xC: *acc = inc8(v); break;
    case 0xD: tst8(v); break;         // no writeback
    case 0xF: *acc = clr8(); break;
    default: return undefinedOp();    // cols 1,2,5,B,E have no accumulator form
    }
    return 2;
}

// ---- 60-7F: the same ops against memory (indexed / extended), plus JMP ----
uint32_t Cpu6800::memRmw(Bus& bus, uint8_t op) {
    int col = op & 0x0F;
    if (col == 1 || col == 2 || col == 5 || col == 0xB)
        return undefinedOp();  // undefined before any operand byte is consumed

    bool ext = op >= 0x70;
    uint16_t addr = ext ? fetch16(bus) : (uint16_t)(x_ + fetch(bus));

    if (col == 0xE) {  // JMP -- indexed 4 cycles, extended 3
        pc_ = addr;
        return ext ? 3 : 4;
    }

    uint32_t t = ext ? 6 : 7;
    if (col == 0xD) {  // TST reads, sets flags, writes nothing back
        tst8(bus.memRead(addr));
        return t;
    }
    if (col == 0xF) {  // CLR writes zero without reading
        bus.memWrite(addr, clr8());
        return t;
    }

    uint8_t v = bus.memRead(addr);
    uint8_t r;
    switch (col) {
    case 0x0: r = neg8(v); break;
    case 0x3: r = com8(v); break;
    case 0x4: r = lsr8(v); break;
    case 0x6: r = ror8(v); break;
    case 0x7: r = asr8(v); break;
    case 0x8: r = asl8(v); break;
    case 0x9: r = rol8(v); break;
    case 0xA: r = dec8(v); break;
    default:  r = inc8(v); break;  // 0xC
    }
    bus.memWrite(addr, r);
    return t;
}

// ---- 80-FF: accumulator ALU / load / store, plus the irregular columns
// (CPX, BSR/JSR, LDS/LDX, STS/STX). accB picks the accumulator; the two middle
// address bits pick the mode. ----
uint32_t Cpu6800::aluBlock(Bus& bus, uint8_t op) {
    bool accB = (op & 0x40) != 0;
    int mode = (op >> 4) & 3;   // 0 imm, 1 direct, 2 indexed, 3 extended
    int col = op & 0x0F;
    uint8_t* acc = accB ? &b_ : &a_;

    switch (col) {
    case 0x3:
        return undefinedOp();

    case 0xC: {  // CPX (A-side only)
        if (accB) return undefinedOp();
        uint16_t m; uint32_t t;
        fetchOperand16(bus, mode, m, t);
        cpx(m);
        return t;
    }

    case 0xD: {  // BSR (imm slot) / JSR (indexed, extended). No B-side, no direct.
        if (accB) return undefinedOp();
        if (mode == 0) {  // BSR -- relative
            int8_t off = (int8_t)fetch(bus);
            push8(bus, (uint8_t)pc_);
            push8(bus, (uint8_t)(pc_ >> 8));
            pc_ = (uint16_t)(pc_ + off);
            return 8;
        }
        if (mode == 1) return undefinedOp();  // 0x9D is undefined on the 6800
        uint16_t addr;
        uint32_t t;
        if (mode == 2) { addr = (uint16_t)(x_ + fetch(bus)); t = 8; }
        else           { addr = fetch16(bus);               t = 9; }
        push8(bus, (uint8_t)pc_);
        push8(bus, (uint8_t)(pc_ >> 8));
        pc_ = addr;
        return t;
    }

    case 0xE: {  // LDS (A-side) / LDX (B-side) -- 16-bit load
        uint16_t m; uint32_t t;
        fetchOperand16(bus, mode, m, t);
        if (accB) x_ = m; else sp_ = m;
        ld16(m);
        return t;
    }

    case 0xF: {  // STS (A-side) / STX (B-side) -- 16-bit store, no immediate
        if (mode == 0) return undefinedOp();
        uint16_t addr; uint32_t t;
        addrOperand16store(bus, mode, addr, t);
        uint16_t v = accB ? x_ : sp_;
        bus.memWrite(addr, (uint8_t)(v >> 8));
        bus.memWrite((uint16_t)(addr + 1), (uint8_t)v);
        ld16(v);
        return t;
    }

    case 0x7: {  // STAA / STAB -- 8-bit store, no immediate
        if (mode == 0) return undefinedOp();
        uint16_t addr; uint32_t t;
        addrOperand8store(bus, mode, addr, t);
        bus.memWrite(addr, *acc);
        setNZ8(*acc);
        vf_ = false;
        return t;
    }

    default: break;  // the regular 8-bit ALU/load columns fall through
    }

    uint8_t m; uint32_t t;
    fetchOperand8(bus, mode, m, t);
    switch (col) {
    case 0x0: *acc = sub8(*acc, m, false); break;  // SUB
    case 0x1: sub8(*acc, m, false); break;         // CMP (flags only)
    case 0x2: *acc = sub8(*acc, m, cf_); break;    // SBC
    case 0x4: *acc = and8(*acc, m); break;         // AND
    case 0x5: and8(*acc, m); break;                // BIT (flags only)
    case 0x6: *acc = m; setNZ8(m); vf_ = false; break;  // LDA
    case 0x8: *acc = eor8(*acc, m); break;         // EOR
    case 0x9: *acc = add8(*acc, m, cf_); break;    // ADC
    case 0xA: *acc = or8(*acc, m); break;          // ORA
    default:  *acc = add8(*acc, m, false); break;  // 0xB ADD
    }
    return t;
}

// ---------------------------------------------------------------------------
// One instruction.
// ---------------------------------------------------------------------------
StepResult Cpu6800::step(Bus& bus) {
    // The deferred restart fetch: the first step after reset loads PC from the
    // reset vector, then goes on to run the first instruction there.
    if (fetchResetVector_) {
        fetchResetVector_ = false;
        pc_ = read16(bus, 0xFFFE);
    }

    // Interrupts, at the instruction boundary. NMI is non-maskable and edge
    // latched; IRQ is the level on the bus wire, gated by the I mask. Either one
    // wakes a WAI. NMI wins if both are up.
    if (nmiPending_) {
        nmiPending_ = false;
        return {takeInterrupt(bus, 0xFFFC), RunStatus::Ok};
    }
    if (!if_ && bus.intPending()) {
        return {takeInterrupt(bus, 0xFFF8), RunStatus::Ok};
    }
    if (waiting_) {
        // WAI with nothing pending: the frame is already stacked, so we simply
        // burn a cycle and stay parked, like the 8080 holding in HLT.
        return {1, RunStatus::Halted};
    }

    uint8_t op = fetch(bus);
    uint32_t t;

    switch (op) {
    // ---- 00-1F: flag ops, register transfers, INX/DEX ----
    case 0x01: t = 2; break;                                    // NOP
    case 0x06: setCc(a_); t = 2; break;                         // TAP
    case 0x07: a_ = cc(); t = 2; break;                         // TPA
    case 0x08: x_ = (uint16_t)(x_ + 1); zf_ = (x_ == 0); t = 4; break;  // INX
    case 0x09: x_ = (uint16_t)(x_ - 1); zf_ = (x_ == 0); t = 4; break;  // DEX
    case 0x0A: vf_ = false; t = 2; break;                       // CLV
    case 0x0B: vf_ = true;  t = 2; break;                       // SEV
    case 0x0C: cf_ = false; t = 2; break;                       // CLC
    case 0x0D: cf_ = true;  t = 2; break;                       // SEC
    case 0x0E: if_ = false; t = 2; break;                       // CLI
    case 0x0F: if_ = true;  t = 2; break;                       // SEI
    case 0x10: a_ = sub8(a_, b_, false); t = 2; break;          // SBA
    case 0x11: sub8(a_, b_, false); t = 2; break;               // CBA (flags only)
    case 0x16: b_ = a_; setNZ8(b_); vf_ = false; t = 2; break;  // TAB
    case 0x17: a_ = b_; setNZ8(a_); vf_ = false; t = 2; break;  // TBA
    case 0x19: daa(); t = 2; break;                             // DAA
    case 0x1B: a_ = add8(a_, b_, false); t = 2; break;          // ABA

    // ---- 20-2F: relative branches (all 4 cycles, taken or not) ----
    case 0x20: branch(bus, true);                 t = 4; break;  // BRA
    case 0x22: branch(bus, !cf_ && !zf_);          t = 4; break;  // BHI
    case 0x23: branch(bus, cf_ || zf_);            t = 4; break;  // BLS
    case 0x24: branch(bus, !cf_);                  t = 4; break;  // BCC
    case 0x25: branch(bus, cf_);                   t = 4; break;  // BCS
    case 0x26: branch(bus, !zf_);                  t = 4; break;  // BNE
    case 0x27: branch(bus, zf_);                   t = 4; break;  // BEQ
    case 0x28: branch(bus, !vf_);                  t = 4; break;  // BVC
    case 0x29: branch(bus, vf_);                   t = 4; break;  // BVS
    case 0x2A: branch(bus, !nf_);                  t = 4; break;  // BPL
    case 0x2B: branch(bus, nf_);                   t = 4; break;  // BMI
    case 0x2C: branch(bus, (nf_ ^ vf_) == 0);      t = 4; break;  // BGE
    case 0x2D: branch(bus, (nf_ ^ vf_) != 0);      t = 4; break;  // BLT
    case 0x2E: branch(bus, !zf_ && (nf_ ^ vf_) == 0); t = 4; break;  // BGT
    case 0x2F: branch(bus, zf_ || (nf_ ^ vf_) != 0);  t = 4; break;  // BLE

    // ---- 30-3F: stack / index / return / interrupt control ----
    case 0x30: x_ = (uint16_t)(sp_ + 1); t = 4; break;          // TSX
    case 0x31: sp_ = (uint16_t)(sp_ + 1); t = 4; break;         // INS
    case 0x32: a_ = pull8(bus); t = 4; break;                   // PULA
    case 0x33: b_ = pull8(bus); t = 4; break;                   // PULB
    case 0x34: sp_ = (uint16_t)(sp_ - 1); t = 4; break;         // DES
    case 0x35: sp_ = (uint16_t)(x_ - 1); t = 4; break;          // TXS
    case 0x36: push8(bus, a_); t = 4; break;                    // PSHA
    case 0x37: push8(bus, b_); t = 4; break;                    // PSHB
    case 0x39: {                                                // RTS
        uint8_t hi = pull8(bus), lo = pull8(bus);
        pc_ = (uint16_t)((hi << 8) | lo);
        t = 5;
        break;
    }
    case 0x3B: rti(bus); t = 10; break;                         // RTI
    case 0x3E: pushState(bus); waiting_ = true; t = 9; break;   // WAI
    case 0x3F:                                                  // SWI
        pushState(bus);
        if_ = true;
        pc_ = read16(bus, 0xFFFA);
        t = 12;
        break;

    default:
        if (op >= 0x40 && op <= 0x5F)      t = accInherentRmw(op);
        else if (op >= 0x60 && op <= 0x7F) t = memRmw(bus, op);
        else if (op >= 0x80)               t = aluBlock(bus, op);
        else                               t = undefinedOp();  // 00-3F holes
        break;
    }

    return {t, waiting_ ? RunStatus::Halted : RunStatus::Ok};
}

// ---------------------------------------------------------------------------
// SNAPSHOT/RESTORE (DESIGN.md 13). The flags travel as the CCR byte -- the same
// 1 1 H I N Z V C an interrupt stacks -- and the three hidden latches (WAI wait,
// armed reset fetch, pending NMI edge) go alongside, because registers() alone is
// not enough to resume cycle-for-cycle.
// ---------------------------------------------------------------------------
void Cpu6800::serialize(StateWriter& w) const {
    w.u8(a_);
    w.u8(b_);
    w.u16(x_);
    w.u16(sp_);
    w.u16(pc_);
    w.u8(cc());
    w.boolean(waiting_);
    w.boolean(fetchResetVector_);
    w.boolean(nmiPending_);
}

void Cpu6800::deserialize(StateReader& r) {
    a_ = r.u8();
    b_ = r.u8();
    x_ = r.u16();
    sp_ = r.u16();
    pc_ = r.u16();
    setCc(r.u8());
    waiting_ = r.boolean();
    fetchResetVector_ = r.boolean();
    nmiPending_ = r.boolean();
}

} // namespace altair
