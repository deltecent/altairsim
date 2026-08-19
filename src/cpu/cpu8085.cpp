#include "cpu/cpu8085.h"

#include "core/statefile.h"

#include <array>

namespace altair {

// ---------------------------------------------------------------------------
// Fetch and store. Every one of these is a REAL bus cycle, and the status words
// are the 8080's -- the 8085 drives the same machine-cycle status the backplane
// carries (see cpu8080.cpp for the full account of the status word). This core is
// a copy of the 8080's plumbing, deliberately (cpu8085.h).
// ---------------------------------------------------------------------------
uint8_t Cpu8085::readOp(Bus& bus) {
    if (intFetch_) return bus.intAck(StM1 | StInta | StWo);  // and the PC does not move
    return bus.memRead(pc_++, StM1 | StMemR | StWo);
}

uint8_t Cpu8085::fetch(Bus& bus) {
    if (intFetch_) return bus.intAck();  // operand INTA -- not M1; and the PC does not move
    return bus.memRead(pc_++, StMemR | StWo);
}

uint16_t Cpu8085::fetch16(Bus& bus) {
    uint8_t lo = fetch(bus);
    uint8_t hi = fetch(bus);
    return (uint16_t)(lo | (hi << 8));  // low byte first: the 8085 stores it that way
}

uint8_t Cpu8085::readMem(Bus& bus, uint16_t addr) {
    return bus.memRead(addr, StMemR | StWo);
}
void Cpu8085::writeMem(Bus& bus, uint16_t addr, uint8_t v) {
    bus.memWrite(addr, v, 0);
}

uint8_t Cpu8085::readStack(Bus& bus, uint16_t addr) {
    return bus.memRead(addr, StMemR | StStack | StWo);
}
void Cpu8085::writeStack(Bus& bus, uint16_t addr, uint8_t v) {
    bus.memWrite(addr, v, StStack);
}

void Cpu8085::push(Bus& bus, uint16_t v) {
    writeStack(bus, --sp_, (uint8_t)(v >> 8));
    writeStack(bus, --sp_, (uint8_t)(v & 0xFF));
}

uint16_t Cpu8085::pop(Bus& bus) {
    uint8_t lo = readStack(bus, sp_++);
    uint8_t hi = readStack(bus, sp_++);
    return (uint16_t)(lo | (hi << 8));
}

uint8_t Cpu8085::getR(Bus& bus, int i) {
    switch (i) {
    case 0: return b_;
    case 1: return c_;
    case 2: return d_;
    case 3: return e_;
    case 4: return h_;
    case 5: return l_;
    case 6: return readMem(bus, hl());  // M -- a memory operand, through the bus
    default: return a_;
    }
}

void Cpu8085::setR(Bus& bus, int i, uint8_t v) {
    switch (i) {
    case 0: b_ = v; break;
    case 1: c_ = v; break;
    case 2: d_ = v; break;
    case 3: e_ = v; break;
    case 4: h_ = v; break;
    case 5: l_ = v; break;
    case 6: writeMem(bus, hl(), v); break;
    default: a_ = v; break;
    }
}

bool Cpu8085::cond(int i) const {
    switch (i) {
    case 0: return !z_;
    case 1: return z_;
    case 2: return !cy_;
    case 3: return cy_;
    case 4: return !p_;   // PO -- parity odd
    case 5: return p_;    // PE -- parity even
    case 6: return !s_;   // P  -- plus
    default: return s_;   // M  -- minus
    }
}

// ---------------------------------------------------------------------------
// The flags: S Z 0 AC 0 P 1 CY -- the 8080 layout, unchanged. The 8085's V and K
// bits are not modeled in this landing (cpu8085.h), so parity stays EVEN parity
// and the PSW constants stay as the 8080 defines them.
// ---------------------------------------------------------------------------
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

void Cpu8085::setSZP(uint8_t v) {
    s_ = (v & 0x80) != 0;
    z_ = (v == 0);
    p_ = kEvenParity[v];
}

uint8_t Cpu8085::psw() const {
    uint8_t f = 0x02;  // bit1 reads 1, bits 3/5 read 0 -- the 8080 constants (cpu8080.cpp)
    if (cy_) f |= 0x01;
    if (p_)  f |= 0x04;
    if (ac_) f |= 0x10;
    if (z_)  f |= 0x40;
    if (s_)  f |= 0x80;
    return f;
}

void Cpu8085::setPsw(uint8_t f) {
    cy_ = (f & 0x01) != 0;
    p_  = (f & 0x04) != 0;
    ac_ = (f & 0x10) != 0;
    z_  = (f & 0x40) != 0;
    s_  = (f & 0x80) != 0;
}

void Cpu8085::add(uint8_t v, bool carryIn) {
    unsigned r = (unsigned)a_ + v + (carryIn ? 1 : 0);
    ac_ = ((a_ & 0x0F) + (v & 0x0F) + (carryIn ? 1 : 0)) > 0x0F;
    cy_ = r > 0xFF;
    a_ = (uint8_t)r;
    setSZP(a_);
}

void Cpu8085::sub(uint8_t v, bool borrowIn) {
    uint8_t nv = (uint8_t)~v;
    unsigned carryIn = borrowIn ? 0 : 1;
    unsigned r = (unsigned)a_ + nv + carryIn;
    ac_ = ((a_ & 0x0F) + (nv & 0x0F) + carryIn) > 0x0F;
    cy_ = !(r > 0xFF);
    a_ = (uint8_t)r;
    setSZP(a_);
}

void Cpu8085::cmp(uint8_t v) {
    uint8_t save = a_;
    sub(v, false);
    a_ = save;
}

// ANA -- KEPT AT THE 8080 RULE ON PURPOSE. AC = OR of bit 3 of the two operands.
// The real 8085 differs here (it does not use the OR-of-bit-3 rule), but modeling
// that would break 8080EXM, which CRCs this exact behavior at full flag mask and
// is this core's gate. The 8085 divergence is a documented known-gap, deferred to
// the faithful follow-up and pinned by tests/test_8085_cpu.cpp. See cpu8085.h.
void Cpu8085::ana(uint8_t v) {
    ac_ = ((a_ | v) & 0x08) != 0;
    a_ &= v;
    cy_ = false;
    setSZP(a_);
}

void Cpu8085::xra(uint8_t v) {
    a_ ^= v;
    cy_ = false;
    ac_ = false;
    setSZP(a_);
}

void Cpu8085::ora(uint8_t v) {
    a_ |= v;
    cy_ = false;
    ac_ = false;
    setSZP(a_);
}

uint8_t Cpu8085::inr(uint8_t v) {
    uint8_t r = (uint8_t)(v + 1);
    ac_ = (r & 0x0F) == 0;
    setSZP(r);
    return r;
}

uint8_t Cpu8085::dcr(uint8_t v) {
    uint8_t r = (uint8_t)(v - 1);
    ac_ = (r & 0x0F) != 0x0F;
    setSZP(r);
    return r;
}

void Cpu8085::dad(uint16_t v) {
    unsigned r = (unsigned)hl() + v;
    cy_ = r > 0xFFFF;
    h_ = (uint8_t)(r >> 8);
    l_ = (uint8_t)(r & 0xFF);
}

void Cpu8085::daa() {
    uint8_t add = 0;
    bool carry = cy_;

    if (ac_ || (a_ & 0x0F) > 9) add = 0x06;
    if (carry || (a_ >> 4) > 9 || ((a_ >> 4) == 9 && (a_ & 0x0F) > 9)) {
        add |= 0x60;
        carry = true;
    }

    ac_ = ((a_ & 0x0F) + (add & 0x0F)) > 0x0F;
    a_ = (uint8_t)(a_ + add);
    setSZP(a_);
    cy_ = carry;
}

// A hardware restart (TRAP / RST n.5). Push PC, jump to the fixed vector, disable
// further interrupts, and come out of HLT. No bus INTA cycle -- the vector is on
// the chip, not the bus -- so intFetch_ stays clear.
void Cpu8085::vector(Bus& bus, uint16_t addr) {
    push(bus, pc_);
    pc_ = addr;
    ie_ = false;
    eiPending_ = false;
    halted_ = false;
}

// ---------------------------------------------------------------------------
// Reflection (DESIGN.md 3.0.3). Same DDT/SID status line as the 8080; the 8085's
// interrupt latches and serial pins are added as Off registers -- reachable by
// name (SET REG, breakpoints, and the unit tests' setReg poke path) but not on the
// one-line status, where the 8080 lamps and fields already say all an operator reads.
// ---------------------------------------------------------------------------
std::vector<RegDef> Cpu8085::registers() {
    auto flag = [](const char* n, const char* lbl, const char* help, bool* p) {
        return RegDef{n, 1, lbl, RegShow::Flag, help, [p] { return (uint32_t)(*p ? 1 : 0); },
                      [p](uint32_t v) { *p = v != 0; }};
    };
    auto half = [](const char* n, uint8_t* p) {
        return RegDef{n, 8, "", RegShow::Off, "", [p] { return (uint32_t)*p; },
                      [p](uint32_t v) { *p = (uint8_t)v; }};
    };
    auto pair = [](const char* n, const char* lbl, const char* help, uint8_t* hi, uint8_t* lo) {
        return RegDef{n, 16, lbl, RegShow::Field, help,
                      [hi, lo] { return (uint32_t)((*hi << 8) | *lo); },
                      [hi, lo](uint32_t v) {
                          *hi = (uint8_t)(v >> 8);
                          *lo = (uint8_t)v;
                      }};
    };
    // The 8085-only latches: 1-bit, reachable by name, off the status line.
    auto off1 = [](const char* n, const char* help, bool* p) {
        return RegDef{n, 1, "", RegShow::Off, help, [p] { return (uint32_t)(*p ? 1 : 0); },
                      [p](uint32_t v) { *p = v != 0; }};
    };

    return {
        flag("CY", "C", "carry", &cy_),
        flag("Z", "Z", "zero", &z_),
        flag("S", "M", "sign -- minus", &s_),
        flag("P", "E", "parity -- EVEN parity", &p_),
        flag("AC", "I", "auxiliary (half) carry -- interdigit", &ac_),

        {"A", 8, "", RegShow::Field, "accumulator", [this] { return (uint32_t)a_; },
         [this](uint32_t v) { a_ = (uint8_t)v; }},
        pair("BC", "B", "the B,C pair", &b_, &c_),
        pair("DE", "D", "the D,E pair", &d_, &e_),
        pair("HL", "H", "the H,L pair", &h_, &l_),
        {"SP", 16, "S", RegShow::Field, "stack pointer", [this] { return (uint32_t)sp_; },
         [this](uint32_t v) { sp_ = (uint16_t)v; }},
        {"IE", 1, "IE", RegShow::Field, "interrupts enabled (INTE)",
         [this] { return (uint32_t)(ie_ ? 1 : 0); }, [this](uint32_t v) { ie_ = v != 0; }},
        {"PC", 16, "P", RegShow::Field, "program counter", [this] { return (uint32_t)pc_; },
         [this](uint32_t v) { pc_ = (uint16_t)v; }},

        half("B", &b_), half("C", &c_),
        half("D", &d_), half("E", &e_),
        half("H", &h_), half("L", &l_),
        {"F", 8, "", RegShow::Off, "flags: S Z 0 AC 0 P 1 CY", [this] { return (uint32_t)psw(); },
         [this](uint32_t v) { setPsw((uint8_t)v); }},

        // 8085 interrupt system: masks, pending latches, and the serial pins.
        off1("M55", "RST 5.5 masked", &m55_),
        off1("M65", "RST 6.5 masked", &m65_),
        off1("M75", "RST 7.5 masked", &m75_),
        off1("I55", "RST 5.5 pending", &p55_),
        off1("I65", "RST 6.5 pending", &p65_),
        off1("I75", "RST 7.5 pending (edge latch)", &p75_),
        off1("TRAP", "TRAP pending (non-maskable)", &ptrap_),
        off1("SID", "serial input data pin", &sid_),
        off1("SOD", "serial output data pin", &sod_),
    };
}

// Both resets: PC to zero, interrupts off, out of HLT (the 8080 set), plus the
// 8085's own reset state -- RESET sets all three RST masks (nothing fires until
// SIM unmasks), clears the pending latches and TRAP, and clears SOD (MCS-85 manual).
// The general registers are NOT cleared.
void Cpu8085::reset(Reset) {
    pc_ = 0;
    ie_ = false;
    eiPending_ = false;
    halted_ = false;
    intFetch_ = false;

    m55_ = m65_ = m75_ = true;
    p55_ = p65_ = p75_ = false;
    ptrap_ = false;
    sod_ = false;
}

// ---------------------------------------------------------------------------
// One instruction.
//
// The body is the 8080's switch (cpu8080.cpp) verbatim, with two changes: the
// on-chip interrupt block below replaces the 8080's single-line INTR check, and
// the 0x20 / 0x30 NOP slots become RIM / SIM. Everything else is byte-identical --
// which is exactly why the stock 8080 exercisers are this core's gate.
// ---------------------------------------------------------------------------
StepResult Cpu8085::step(Bus& bus) {
    // ---- The 8085 interrupt system, at the instruction boundary ----
    //
    // Priority, highest first: TRAP > RST7.5 > RST6.5 > RST5.5 > INTR. TRAP is
    // non-maskable -- it ignores INTE and the masks. The RST n.5 lines fire only
    // when INTE is set AND their mask bit is clear AND they are pending. All four
    // vector INTERNALLY (vector(), no bus cycle); only INTR fetches from the bus.
    if (ptrap_) {
        vector(bus, 0x0024);
        return {12, RunStatus::Ok};
    }
    if (ie_ && p75_ && !m75_) {
        p75_ = false;  // the 7.5 EDGE latch resets when it is serviced
        vector(bus, 0x003C);
        return {12, RunStatus::Ok};
    }
    if (ie_ && p65_ && !m65_) {
        vector(bus, 0x0034);  // 6.5 is level-sensitive: INTE going false stops the re-fire
        return {12, RunStatus::Ok};
    }
    if (ie_ && p55_ && !m55_) {
        vector(bus, 0x002C);
        return {12, RunStatus::Ok};
    }

    // ---- INTR: the 8080-style maskable line (S-100 pin 73), unchanged ----
    if (ie_ && bus.intPending()) {
        ie_ = false;      // disabled on acknowledge; the handler re-enables
        eiPending_ = false;
        halted_ = false;  // an interrupt is the way out of HLT
        intFetch_ = true;
        // Fall through into the decode below: the opcode comes from the bus.
    } else if (halted_) {
        return {4, RunStatus::Halted};
    }

    bool takingInterrupt = intFetch_;
    bool eiWasPending = eiPending_;

    uint8_t op = readOp(bus);  // the M1 opcode fetch -- the one cycle that asserts M1
    uint32_t t = 4;

    // ---- MOV dst,src -- 01dddsss, with 76 punched out for HLT ----
    if (op >= 0x40 && op <= 0x7F) {
        if (op == 0x76) {
            halted_ = true;
            intFetch_ = false;
            if (eiWasPending) { ie_ = true; eiPending_ = false; }
            return {7, RunStatus::Halted};
        }
        int dst = (op >> 3) & 7, src = op & 7;
        setR(bus, dst, getR(bus, src));
        t = (dst == 6 || src == 6) ? 7 : 5;
    }
    // ---- ALU A,src -- 10ppp sss ----
    else if (op >= 0x80 && op <= 0xBF) {
        int kind = (op >> 3) & 7, src = op & 7;
        uint8_t v = getR(bus, src);
        switch (kind) {
        case 0: add(v, false); break;   // ADD
        case 1: add(v, cy_);   break;   // ADC
        case 2: sub(v, false); break;   // SUB
        case 3: sub(v, cy_);   break;   // SBB
        case 4: ana(v);        break;
        case 5: xra(v);        break;
        case 6: ora(v);        break;
        default: cmp(v);       break;
        }
        t = (src == 6) ? 7 : 4;
    } else {
        switch (op) {
        // ---- NOP, and the deferred undocumented-opcode slots. 0x20/0x30 are NOT
        // here any more -- they are RIM/SIM below. The rest stay NOP until the
        // faithful follow-up (cpu8085.h). ----
        case 0x00: case 0x08: case 0x10: case 0x18:
        case 0x28: case 0x38:
            t = 4;  // NOP
            break;

        // ---- RIM / SIM -- the two documented 8085 additions ----
        case 0x20: {  // RIM: read interrupt mask + serial input
            uint8_t v = 0;
            if (sid_) v |= 0x80;   // bit7: SID pin
            if (p75_) v |= 0x40;   // bit6: RST7.5 pending
            if (p65_) v |= 0x20;   // bit5: RST6.5 pending
            if (p55_) v |= 0x10;   // bit4: RST5.5 pending
            if (ie_)  v |= 0x08;   // bit3: INTE
            if (m75_) v |= 0x04;   // bit2: M7.5 mask
            if (m65_) v |= 0x02;   // bit1: M6.5 mask
            if (m55_) v |= 0x01;   // bit0: M5.5 mask
            a_ = v;
            t = 4;
            break;
        }
        case 0x30: {  // SIM: set interrupt mask + serial output
            if (a_ & 0x08) {                     // bit3 MSE: load the masks
                m55_ = (a_ & 0x01) != 0;
                m65_ = (a_ & 0x02) != 0;
                m75_ = (a_ & 0x04) != 0;
            }
            if (a_ & 0x10) p75_ = false;         // bit4 R7.5: reset the 7.5 edge latch
            if (a_ & 0x40) sod_ = (a_ & 0x80) != 0;  // bit6 SOE: latch SOD from bit7
            t = 4;
            break;
        }

        // ---- 16-bit loads ----
        case 0x01: { uint16_t v = fetch16(bus); b_ = (uint8_t)(v >> 8); c_ = (uint8_t)v; t = 10; break; }
        case 0x11: { uint16_t v = fetch16(bus); d_ = (uint8_t)(v >> 8); e_ = (uint8_t)v; t = 10; break; }
        case 0x21: { uint16_t v = fetch16(bus); h_ = (uint8_t)(v >> 8); l_ = (uint8_t)v; t = 10; break; }
        case 0x31: sp_ = fetch16(bus); t = 10; break;

        case 0x02: writeMem(bus, bc(), a_); t = 7; break;   // STAX B
        case 0x12: writeMem(bus, de(), a_); t = 7; break;   // STAX D
        case 0x0A: a_ = readMem(bus, bc()); t = 7; break;   // LDAX B
        case 0x1A: a_ = readMem(bus, de()); t = 7; break;   // LDAX D

        case 0x32: { uint16_t a = fetch16(bus); writeMem(bus, a, a_); t = 13; break; }  // STA
        case 0x3A: { uint16_t a = fetch16(bus); a_ = readMem(bus, a); t = 13; break; }  // LDA

        case 0x22: {  // SHLD -- L first, then H.
            uint16_t a = fetch16(bus);
            writeMem(bus, a, l_);
            writeMem(bus, (uint16_t)(a + 1), h_);
            t = 16;
            break;
        }
        case 0x2A: {  // LHLD
            uint16_t a = fetch16(bus);
            l_ = readMem(bus, a);
            h_ = readMem(bus, (uint16_t)(a + 1));
            t = 16;
            break;
        }

        // ---- INX / DCX -- NO FLAGS AT ALL ----
        case 0x03: { uint16_t v = (uint16_t)(bc() + 1); b_ = (uint8_t)(v >> 8); c_ = (uint8_t)v; t = 5; break; }
        case 0x13: { uint16_t v = (uint16_t)(de() + 1); d_ = (uint8_t)(v >> 8); e_ = (uint8_t)v; t = 5; break; }
        case 0x23: { uint16_t v = (uint16_t)(hl() + 1); h_ = (uint8_t)(v >> 8); l_ = (uint8_t)v; t = 5; break; }
        case 0x33: ++sp_; t = 5; break;
        case 0x0B: { uint16_t v = (uint16_t)(bc() - 1); b_ = (uint8_t)(v >> 8); c_ = (uint8_t)v; t = 5; break; }
        case 0x1B: { uint16_t v = (uint16_t)(de() - 1); d_ = (uint8_t)(v >> 8); e_ = (uint8_t)v; t = 5; break; }
        case 0x2B: { uint16_t v = (uint16_t)(hl() - 1); h_ = (uint8_t)(v >> 8); l_ = (uint8_t)v; t = 5; break; }
        case 0x3B: --sp_; t = 5; break;

        case 0x09: dad(bc());  t = 10; break;
        case 0x19: dad(de());  t = 10; break;
        case 0x29: dad(hl());  t = 10; break;
        case 0x39: dad(sp_);   t = 10; break;

        // ---- INR / DCR -- 00rrr10x ----
        case 0x04: case 0x0C: case 0x14: case 0x1C:
        case 0x24: case 0x2C: case 0x34: case 0x3C: {
            int r = (op >> 3) & 7;
            setR(bus, r, inr(getR(bus, r)));
            t = (r == 6) ? 10 : 5;
            break;
        }
        case 0x05: case 0x0D: case 0x15: case 0x1D:
        case 0x25: case 0x2D: case 0x35: case 0x3D: {
            int r = (op >> 3) & 7;
            setR(bus, r, dcr(getR(bus, r)));
            t = (r == 6) ? 10 : 5;
            break;
        }

        // ---- MVI r,d8 -- 00rrr110 ----
        case 0x06: case 0x0E: case 0x16: case 0x1E:
        case 0x26: case 0x2E: case 0x36: case 0x3E: {
            int r = (op >> 3) & 7;
            setR(bus, r, fetch(bus));
            t = (r == 6) ? 10 : 7;
            break;
        }

        // ---- rotates. CARRY ONLY. ----
        case 0x07: cy_ = (a_ & 0x80) != 0; a_ = (uint8_t)((a_ << 1) | (cy_ ? 1 : 0)); break;      // RLC
        case 0x0F: cy_ = (a_ & 0x01) != 0; a_ = (uint8_t)((a_ >> 1) | (cy_ ? 0x80 : 0)); break;   // RRC
        case 0x17: { bool c = cy_; cy_ = (a_ & 0x80) != 0; a_ = (uint8_t)((a_ << 1) | (c ? 1 : 0)); break; }     // RAL
        case 0x1F: { bool c = cy_; cy_ = (a_ & 0x01) != 0; a_ = (uint8_t)((a_ >> 1) | (c ? 0x80 : 0)); break; }  // RAR

        case 0x27: daa(); break;
        case 0x2F: a_ = (uint8_t)~a_; break;        // CMA -- no flags
        case 0x37: cy_ = true;  break;              // STC
        case 0x3F: cy_ = !cy_;  break;              // CMC

        // ---- immediate ALU ----
        case 0xC6: add(fetch(bus), false); t = 7; break;   // ADI
        case 0xCE: add(fetch(bus), cy_);   t = 7; break;   // ACI
        case 0xD6: sub(fetch(bus), false); t = 7; break;   // SUI
        case 0xDE: sub(fetch(bus), cy_);   t = 7; break;   // SBI
        case 0xE6: ana(fetch(bus)); t = 7; break;          // ANI
        case 0xEE: xra(fetch(bus)); t = 7; break;          // XRI
        case 0xF6: ora(fetch(bus)); t = 7; break;          // ORI
        case 0xFE: cmp(fetch(bus)); t = 7; break;          // CPI

        // ---- jumps ----
        case 0xC3: case 0xCB: pc_ = fetch16(bus); t = 10; break;   // JMP (CB: undocumented)
        case 0xC2: case 0xCA: case 0xD2: case 0xDA:
        case 0xE2: case 0xEA: case 0xF2: case 0xFA: {
            uint16_t a = fetch16(bus);
            if (cond((op >> 3) & 7)) pc_ = a;
            t = 10;
            break;
        }

        // ---- calls: 17 taken, 11 not ----
        case 0xCD: case 0xDD: case 0xED: case 0xFD: {  // CALL (DD/ED/FD: undocumented)
            uint16_t a = fetch16(bus);
            push(bus, pc_);
            pc_ = a;
            t = 17;
            break;
        }
        case 0xC4: case 0xCC: case 0xD4: case 0xDC:
        case 0xE4: case 0xEC: case 0xF4: case 0xFC: {
            uint16_t a = fetch16(bus);
            if (cond((op >> 3) & 7)) {
                push(bus, pc_);
                pc_ = a;
                t = 17;
            } else {
                t = 11;
            }
            break;
        }

        // ---- returns: 11 taken, 5 not ----
        case 0xC9: case 0xD9: pc_ = pop(bus); t = 10; break;  // RET (D9: undocumented)
        case 0xC0: case 0xC8: case 0xD0: case 0xD8:
        case 0xE0: case 0xE8: case 0xF0: case 0xF8:
            if (cond((op >> 3) & 7)) {
                pc_ = pop(bus);
                t = 11;
            } else {
                t = 5;
            }
            break;

        // ---- RST n ----
        case 0xC7: case 0xCF: case 0xD7: case 0xDF:
        case 0xE7: case 0xEF: case 0xF7: case 0xFF:
            push(bus, pc_);
            pc_ = (uint16_t)(op & 0x38);
            t = 11;
            break;

        // ---- stack ----
        case 0xC5: push(bus, bc()); t = 11; break;
        case 0xD5: push(bus, de()); t = 11; break;
        case 0xE5: push(bus, hl()); t = 11; break;
        case 0xF5: push(bus, (uint16_t)((a_ << 8) | psw())); t = 11; break;
        case 0xC1: { uint16_t v = pop(bus); b_ = (uint8_t)(v >> 8); c_ = (uint8_t)v; t = 10; break; }
        case 0xD1: { uint16_t v = pop(bus); d_ = (uint8_t)(v >> 8); e_ = (uint8_t)v; t = 10; break; }
        case 0xE1: { uint16_t v = pop(bus); h_ = (uint8_t)(v >> 8); l_ = (uint8_t)v; t = 10; break; }
        case 0xF1: { uint16_t v = pop(bus); a_ = (uint8_t)(v >> 8); setPsw((uint8_t)v); t = 10; break; }

        case 0xE3: {  // XTHL
            uint8_t lo = readStack(bus, sp_);
            uint8_t hi = readStack(bus, (uint16_t)(sp_ + 1));
            writeStack(bus, sp_, l_);
            writeStack(bus, (uint16_t)(sp_ + 1), h_);
            l_ = lo;
            h_ = hi;
            t = 18;
            break;
        }
        case 0xEB: {  // XCHG
            uint8_t th = h_, tl = l_;
            h_ = d_; l_ = e_;
            d_ = th; e_ = tl;
            break;
        }
        case 0xE9: pc_ = hl(); t = 5; break;   // PCHL
        case 0xF9: sp_ = hl(); t = 5; break;   // SPHL

        // ---- I/O ----
        case 0xDB: a_ = bus.ioRead(fetch(bus), StInp | StWo); t = 10; break;   // IN
        case 0xD3: bus.ioWrite(fetch(bus), a_, StOut);        t = 10; break;   // OUT

        case 0xF3: ie_ = false; eiPending_ = false; break;  // DI -- immediate
        case 0xFB: eiPending_ = true; break;                // EI -- one instruction later

        default:
            // Unreachable: all 256 opcodes are accounted for above.
            t = 4;
            break;
        }
    }

    // EI's one-instruction delay (cpu8080.cpp).
    if (eiWasPending && op != 0xFB) {
        ie_ = true;
        eiPending_ = false;
    }

    if (takingInterrupt) intFetch_ = false;
    return {t, halted_ ? RunStatus::Halted : RunStatus::Ok};
}

// SNAPSHOT/RESTORE (DESIGN.md 13). The 8080 field set (flags travel as the PSW
// byte), then the 8085's interrupt system -- masks, pending latches, TRAP, and the
// serial pins. Independent core, independent format: no 8080 compatibility to keep.
void Cpu8085::serialize(StateWriter& w) const {
    w.u8(a_); w.u8(b_); w.u8(c_); w.u8(d_); w.u8(e_); w.u8(h_); w.u8(l_);
    w.u16(sp_); w.u16(pc_);
    w.u8(psw());
    w.boolean(ie_);
    w.boolean(halted_);
    w.boolean(eiPending_);
    w.boolean(intFetch_);

    w.boolean(m55_); w.boolean(m65_); w.boolean(m75_);
    w.boolean(p55_); w.boolean(p65_); w.boolean(p75_);
    w.boolean(ptrap_);
    w.boolean(sid_); w.boolean(sod_);
}

void Cpu8085::deserialize(StateReader& r) {
    a_ = r.u8(); b_ = r.u8(); c_ = r.u8(); d_ = r.u8(); e_ = r.u8(); h_ = r.u8(); l_ = r.u8();
    sp_ = r.u16(); pc_ = r.u16();
    setPsw(r.u8());
    ie_        = r.boolean();
    halted_    = r.boolean();
    eiPending_ = r.boolean();
    intFetch_  = r.boolean();

    m55_ = r.boolean(); m65_ = r.boolean(); m75_ = r.boolean();
    p55_ = r.boolean(); p65_ = r.boolean(); p75_ = r.boolean();
    ptrap_ = r.boolean();
    sid_ = r.boolean(); sod_ = r.boolean();
}

} // namespace altair
