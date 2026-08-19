#pragma once
//
// The Intel 8085A core -- the chip, not the card (DESIGN.md 3).
//
// Sources: Intel 8085 Microprocessor Users Manual and the MCS-85 Family User's
// Manual. Opcodes, T-states and flag rules come from those and nowhere else
// (DESIGN.md 0.1).
//
// THE 8085 IS AN 8080 SUPERSET, AND THIS CORE IS A SELF-CONTAINED COPY of the
// 8080 (cpu8080.h) with its additions folded in -- NOT a subclass. That is the
// same call the Z80 core made (cpuZ80.h): a core "shares nothing" so a bug cannot
// hide by agreeing with itself (DESIGN.md 3.0.2), and the validated 8080 core is
// left completely untouched. The cost is duplication; the payoff is that the same
// CP/M exercisers, re-run against THIS core, re-prove the shared subset from
// scratch (see tests/cputest.cpp).
//
// WHAT THIS LANDING MODELS (the documented 8085 that the 8080 gate can validate):
//   - RIM / SIM -- read/set the interrupt mask and the SID/SOD serial pins.
//   - TRAP (non-maskable) + RST 5.5 / 6.5 / 7.5, each with its own mask/pending,
//     layered on top of the 8080-style INTR line.
//   - The whole documented 8080 instruction set, byte-identical and flag-identical
//     EXCEPT ANA/ANI's auxiliary carry -- the 8085 always SETS it, where the 8080
//     derives it from the operands. Validated against real 8085 silicon: the core
//     is gated by 8085EXM, not 8080EXM (tests/cputest.cpp, tests/cpu/PROVENANCE.md).
//   - The two undocumented condition bits V (PSW bit 1) and K/X5 (PSW bit 5), which
//     the 8080 nails to constants (1 and 0). Their rules are transcribed from Ken
//     Shirriff's reverse-engineering of the actual 8085 silicon die (issue #347;
//     reference/Intel 8085 undocumented instructions and flags.md): V is the
//     carry-into-bit7 XOR carry-out-of-bit7 overflow bit; K is V XOR the result's
//     sign, EXCEPT for INX/DCX where K is the carry out of the 16-bit incrementer.
//     8085EXM masks both out ([S Z X AC X P X C]) so the stock gate is blind to them
//     -- they are pinned by hand-derived unit tests (tests/test_8085_cpu.cpp) whose
//     oracle is the silicon analysis, not this core (DESIGN.md 3.2).
//
// WHAT IT DELIBERATELY DOES NOT (deferred to a faithful follow-up, gated on a
// GENUINE 8085 exerciser -- a core may not grade its own homework, DESIGN.md 3.2):
//   - The undocumented opcodes (DSUB/ARHL/RDEL/LDHI/LDSI/RSTV/SHLX/LHLX/JK/JNK) --
//     their slots stay NOP, exactly as the 8080 leaves them. RSTV/JK/JNK branch on
//     the V/K bits above, so those become implementable next.

#include "core/bus.h"
#include "cpu/cpu.h"

#include <cstdint>

namespace altair {

class Cpu8085 : public CpuCore {
public:
    const char* isa() const override { return "8085"; }

    std::vector<RegDef> registers() override;
    void reset(Reset) override;
    StepResult step(Bus& bus) override;

    uint16_t pc() const override { return pc_; }
    void setPc(uint16_t v) override { pc_ = v; }
    bool halted() const override { return halted_; }
    bool interruptsEnabled() const override { return ie_; }

    // The flag byte, as PUSH PSW pushes it: S Z K AC 0 P V CY. The 8085 puts its two
    // computed condition bits K (bit 5) and V (bit 1) where the 8080 keeps constants.
    uint8_t psw() const;
    void setPsw(uint8_t f);

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

private:
    // ---- fetch/store. EVERYTHING goes through the Bus -- there is no pointer to
    // RAM and no back door (see cpu8080.h for the full account). ----
    uint8_t readOp(Bus& bus);
    uint8_t fetch(Bus& bus);
    uint16_t fetch16(Bus& bus);
    uint8_t readMem(Bus& bus, uint16_t addr);
    void writeMem(Bus& bus, uint16_t addr, uint8_t v);
    uint8_t readStack(Bus& bus, uint16_t addr);
    void writeStack(Bus& bus, uint16_t addr, uint8_t v);
    void push(Bus& bus, uint16_t v);
    uint16_t pop(Bus& bus);

    uint8_t getR(Bus& bus, int i);          // 0=B 1=C 2=D 3=E 4=H 5=L 6=M 7=A
    void setR(Bus& bus, int i, uint8_t v);
    bool cond(int i) const;                 // 0=NZ 1=Z 2=NC 3=C 4=PO 5=PE 6=P 7=M

    void setSZP(uint8_t v);
    void add(uint8_t v, bool carryIn);
    void sub(uint8_t v, bool borrowIn);
    void ana(uint8_t v);
    void xra(uint8_t v);
    void ora(uint8_t v);
    void cmp(uint8_t v);
    uint8_t inr(uint8_t v);
    uint8_t dcr(uint8_t v);
    void dad(uint16_t v);
    void daa();

    // A HARDWARE RESTART -- how TRAP and RST 5.5/6.5/7.5 vector. Unlike the RST
    // instruction it comes from a pin, not an opcode, but the effect is the same:
    // push PC, jump to the fixed vector, and disable further interrupts (the
    // handler re-enables with EI). It runs no bus INTA cycle, so intFetch_ stays clear.
    void vector(Bus& bus, uint16_t addr);

    uint16_t hl() const { return (uint16_t)((h_ << 8) | l_); }
    uint16_t bc() const { return (uint16_t)((b_ << 8) | c_); }
    uint16_t de() const { return (uint16_t)((d_ << 8) | e_); }

    uint8_t a_ = 0, b_ = 0, c_ = 0, d_ = 0, e_ = 0, h_ = 0, l_ = 0;
    uint16_t sp_ = 0, pc_ = 0;
    bool s_ = false, z_ = false, ac_ = false, p_ = false, cy_ = false;
    // The two 8085-only condition bits (PSW bits 1 and 5). See the header banner and
    // reference/Intel 8085 undocumented instructions and flags.md for their rules.
    bool v_ = false, k_ = false;

    bool ie_ = false;       // INTE. Cleared by reset, DI, and by taking any interrupt.
    bool halted_ = false;
    bool eiPending_ = false; // EI takes effect after the FOLLOWING instruction (cpu8080.h).
    bool intFetch_ = false;  // inside an INTR acknowledge the opcode comes from the bus.

    // ---- The 8085's on-chip interrupts. These are CHIP PINS, not S-100 bus lines,
    // so board wiring is deferred (issue #233); here they are internal latches the
    // unit tests drive directly, exactly as a real board eventually would. ----
    //
    // Masks (SIM sets them, RIM reads them): true = that RST is MASKED (disabled).
    // RESET sets all three (MCS-85 manual), so nothing fires until SIM unmasks.
    bool m55_ = true, m65_ = true, m75_ = true;

    // Pending. 5.5/6.5 are level-sensitive -- they track the pin and are NOT cleared
    // by being serviced (INTE going false is what stops them re-firing). 7.5 is
    // EDGE-triggered: an internal flip-flop that latches until it is serviced or SIM
    // bit 4 (R7.5) clears it. TRAP is non-maskable and ignores INTE and the masks.
    bool p55_ = false, p65_ = false, p75_ = false, ptrap_ = false;

    // The serial pins. RIM reads SID into bit 7; SIM writes SOD from bit 7 when its
    // SOE (bit 6) is set. Held here for the unit tests and the eventual board.
    bool sid_ = false, sod_ = false;
};

} // namespace altair
