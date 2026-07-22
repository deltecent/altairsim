#pragma once
//
// The Intel 8080A core -- the chip, not the card (DESIGN.md 3).
//
// Sources: Intel 8080 Assembly Language Programming Manual (1975) and the 8080
// Microcomputer Systems User's Manual. Opcodes, T-states and flag rules come from
// those and from nowhere else (DESIGN.md 0.1).
//
// AND THAT IS NOT ENOUGH ON ITS OWN. A core that merely looks right is the single
// most dangerous thing in this program: it runs plausibly for ten thousand
// instructions and then puts one bit in the wrong place in a directory sector.
// So the core is not "done" until TST8080, 8080PRE, CPUTEST and 8080EXM pass
// (DESIGN.md 3.2) -- 8080EXM in particular checks every flag of every ALU op
// against a table of CRCs and does not care how confident you were.
//
// The Python prototype's own notes say those were never run and that DAA is "not
// fully tested." That debt is not inherited.

#include "core/bus.h"
#include "cpu/cpu.h"

#include <cstdint>

namespace altair {

class Cpu8080 : public CpuCore {
public:
    const char* isa() const override { return "8080"; }

    std::vector<RegDef> registers() override;
    void reset(Reset) override;
    StepResult step(Bus& bus) override;

    uint16_t pc() const override { return pc_; }
    void setPc(uint16_t v) override { pc_ = v; }
    bool halted() const override { return halted_; }
    bool interruptsEnabled() const override { return ie_; }

    // The flag byte, as PUSH PSW pushes it: S Z 0 AC 0 P 1 CY.
    uint8_t psw() const;
    void setPsw(uint8_t f);

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

private:
    // ---- fetch/store. EVERYTHING goes through the Bus -- there is no pointer to
    // RAM and no back door, which is why a fetch from an empty socket reads a
    // floating 0xFF and executes as RST 7 with no special case anywhere.
    uint8_t fetch(Bus& bus);
    uint16_t fetch16(Bus& bus);
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

    uint16_t hl() const { return (uint16_t)((h_ << 8) | l_); }
    uint16_t bc() const { return (uint16_t)((b_ << 8) | c_); }
    uint16_t de() const { return (uint16_t)((d_ << 8) | e_); }

    uint8_t a_ = 0, b_ = 0, c_ = 0, d_ = 0, e_ = 0, h_ = 0, l_ = 0;
    uint16_t sp_ = 0, pc_ = 0;
    bool s_ = false, z_ = false, ac_ = false, p_ = false, cy_ = false;

    bool ie_ = false;       // INTE. Cleared by reset, DI, and by taking an interrupt.
    bool halted_ = false;

    // EI DOES NOT TAKE EFFECT UNTIL AFTER THE FOLLOWING INSTRUCTION. That one
    // instruction of grace is the whole reason `EI / RET` at the end of an
    // interrupt handler is safe: the RET pops the return address BEFORE another
    // interrupt can land on it. Model EI as immediate and a busy interrupt source
    // eats the stack, slowly, and the crash lands nowhere near the cause.
    bool eiPending_ = false;

    // Inside an interrupt acknowledge, the instruction comes from the BUS, not
    // from memory, and the PC does not move. The 8080 runs an INTA cycle for each
    // byte of it -- so a device driving a 3-byte CALL is fetched with three INTAs
    // and the PC is still pointing at the interrupted instruction afterwards.
    bool intFetch_ = false;
};

} // namespace altair
