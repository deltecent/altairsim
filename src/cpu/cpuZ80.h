#pragma once
//
// The Zilog Z80 core -- the chip, not the card (DESIGN.md 3).
//
// Sources: Zilog Z80 CPU User's Manual (UM008) for the documented behavior, and
// Sean Young's "The Undocumented Z80 Documented" for the F5/F3 result bits, the
// MEMPTR/WZ internal register they leak, and the DD/FD half-register and DDCB
// reg-copy forms. Opcodes, flags and register rules come from those and nowhere
// else (DESIGN.md 0.1).
//
// AND, EXACTLY AS FOR THE 8080, LOOKING RIGHT IS NOT ENOUGH. A core that runs
// plausibly for ten thousand instructions and then puts one bit in the wrong
// place is the most dangerous thing in this program. So this is not "done" until
// ZEXDOC and ZEXALL pass (the plan's Phase 4) -- ZEXALL in particular CRCs every
// flag of every ALU op, including the undocumented F5/F3, against a table
// captured from real silicon and does not care how confident the author was.
//
// The core is self-contained: it includes only the bus and the CpuCore contract,
// and executes from its own switch. It shares nothing with isaZ80.cpp -- the
// decoder is kept honest by the same test suites, separately, so a bug cannot
// hide by agreeing with itself (DESIGN.md 3.0.2).

#include "core/bus.h"
#include "cpu/cpu.h"

#include <cstdint>

namespace altair {

class CpuZ80 : public CpuCore {
public:
    const char* isa() const override { return "z80"; }

    std::vector<RegDef> registers() override;
    void reset(Reset) override;
    StepResult step(Bus& bus) override;

    uint16_t pc() const override { return pc_; }
    void setPc(uint16_t v) override { pc_ = v; }
    bool halted() const override { return halted_; }
    bool interruptsEnabled() const override { return iff1_; }

private:
    // ---- The F register bit layout: S Z F5 H F3 P/V N C. F5 and F3 are the
    // undocumented copies of result bits 5 and 3; the whole reason ZEXALL is the
    // bar is that they, and the MEMPTR register that feeds them, must be right.
    enum : uint8_t {
        FC = 0x01, FN = 0x02, FPV = 0x04, FF3 = 0x08,
        FH = 0x10, FF5 = 0x20, FZ = 0x40, FS = 0x80,
    };

    // Which 16-bit register the HL slot means for THIS instruction. A DD prefix
    // makes it IX, an FD prefix IY; a displacement is read once and cached so an
    // instruction that touches (IX+d) twice (INC (IX+d): read then write) advances
    // PC over the displacement exactly once.
    enum class Idx { HL, IX, IY };
    struct Ctx {
        Idx idx = Idx::HL;
        bool mem = false;   // does this opcode reach memory via (HL)/(IX+d)?
        int8_t disp = 0;    // the (IX+d) displacement, pre-fetched when mem && idx!=HL
    };

    // ---- fetch/store. EVERYTHING is a real bus cycle -- no pointer to RAM. ----
    uint8_t fetchOp(Bus& bus);   // an M1 opcode fetch: bumps R, or reads the INTA byte
    uint8_t fetchByte(Bus& bus); // an operand byte: no R bump
    uint16_t fetch16(Bus& bus);
    void push(Bus& bus, uint16_t v);
    uint16_t pop(Bus& bus);

    // Register-file plumbing (Idx-aware -- see Ctx).
    uint8_t* reg8(int i);                       // 0=B 1=C 2=D 3=E 4=H 5=L 6=(none) 7=A
    uint8_t getR(Bus& bus, int i, const Ctx& c);
    void setR(Bus& bus, int i, const Ctx& c, uint8_t v);
    uint16_t idxAddr(const Ctx& c);             // (HL) or (IX+d), and sets WZ for the indexed case
    uint16_t& idxReg(const Ctx& c);             // ix_ or iy_
    uint16_t getRP(int i, const Ctx& c);        // 0=BC 1=DE 2=HL/IX 3=SP
    void setRP(int i, const Ctx& c, uint16_t v);
    uint16_t hl16(const Ctx& c) { return c.idx == Idx::HL ? hl() : idxReg(c); }
    bool cond(int i) const;                     // 0=NZ 1=Z 2=NC 3=C 4=PO 5=PE 6=P 7=M

    // Dispatch.
    StepResult execMain(Bus& bus, uint8_t op, Ctx c);
    StepResult execCB(Bus& bus, const Ctx& c);
    StepResult execDDCB(Bus& bus, const Ctx& c);
    StepResult execED(Bus& bus);
    StepResult execIndexed(Bus& bus, Idx idx);
    static bool memFormMain(uint8_t op);

    // ---- ALU / flag helpers. Each writes f_ in full. ----
    void addA(uint8_t v, uint8_t carry);
    void subA(uint8_t v, uint8_t carry, bool store);  // store=false is CP
    void andA(uint8_t v);
    void orA(uint8_t v);
    void xorA(uint8_t v);
    uint8_t incR(uint8_t v);
    uint8_t decR(uint8_t v);
    void addHL(const Ctx& c, uint16_t v);
    void adcHL(uint16_t v);
    void sbcHL(uint16_t v);
    void daa();
    void neg();
    void rlca(); void rrca(); void rla(); void rra();
    uint8_t rot(int kind, uint8_t v);   // 0..7: RLC RRC RL RR SLA SRA SLL SRL
    void bit(uint8_t v, int n, uint8_t x53);
    void rld(Bus& bus, const Ctx& c);
    void rrd(Bus& bus, const Ctx& c);
    void blockLd(Bus& bus, int dir, bool repeat);
    void blockCp(Bus& bus, int dir, bool repeat);
    void blockIn(Bus& bus, int dir, bool repeat);
    void blockOut(Bus& bus, int dir, bool repeat);

    uint8_t szxp(uint8_t v) const;  // S Z F5 F3 P(arity) -- the logical-op flag body
    uint8_t szx(uint8_t v) const;   // S Z F5 F3          -- no parity

    void setFlag(uint8_t m, bool on) { f_ = on ? (uint8_t)(f_ | m) : (uint8_t)(f_ & ~m); }
    bool flag(uint8_t m) const { return (f_ & m) != 0; }

    uint16_t hl() const { return (uint16_t)((h_ << 8) | l_); }
    uint16_t bc() const { return (uint16_t)((b_ << 8) | c_); }
    uint16_t de() const { return (uint16_t)((d_ << 8) | e_); }
    uint16_t af() const { return (uint16_t)((a_ << 8) | f_); }
    void setHL(uint16_t v) { h_ = (uint8_t)(v >> 8); l_ = (uint8_t)v; }
    void setBC(uint16_t v) { b_ = (uint8_t)(v >> 8); c_ = (uint8_t)v; }
    void setDE(uint16_t v) { d_ = (uint8_t)(v >> 8); e_ = (uint8_t)v; }

    // Main register file.
    uint8_t a_ = 0, f_ = 0, b_ = 0, c_ = 0, d_ = 0, e_ = 0, h_ = 0, l_ = 0;
    // The alternate bank. EXX swaps BC/DE/HL; EX AF,AF' swaps AF.
    uint8_t a2_ = 0, f2_ = 0, b2_ = 0, c2_ = 0, d2_ = 0, e2_ = 0, h2_ = 0, l2_ = 0;
    uint16_t ix_ = 0, iy_ = 0, sp_ = 0, pc_ = 0;
    uint8_t i_ = 0, r_ = 0;

    // MEMPTR/WZ -- the internal address latch. Invisible to the programmer except
    // that its high byte leaks into F5/F3 for BIT n,(HL) and the block/RETI paths.
    uint16_t wz_ = 0;

    uint8_t im_ = 0;         // interrupt mode 0/1/2
    bool iff1_ = false;      // the master enable, cleared on accept, restored by RETI/RETN
    bool iff2_ = false;      // the shadow, copied into P/V by LD A,I / LD A,R
    bool halted_ = false;

    // EI enables interrupts only AFTER the following instruction (UM008 6). Modeled
    // exactly as the 8080 core models it: EI arms this, and the instruction after EI
    // is the one that actually sets iff1_ -- so `EI / RET` returns before an
    // interrupt can land on the freshly-popped address.
    bool eiPending_ = false;

    // Inside an IM 0 interrupt acknowledge the opcode comes from the bus, not memory,
    // and PC does not move -- same seam as the 8080's INTA (bus.intAck()).
    bool intFetch_ = false;
};

} // namespace altair
