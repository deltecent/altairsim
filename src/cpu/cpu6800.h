#pragma once
//
// The Motorola M6800 core -- the chip of the Altair 680b, not the card (DESIGN.md 3).
//
// Sources: the M6800 programming reprint in the MITS *Programming Manual -- altair
// 680b* (reference/Altair 680b Programming Manual.md) and the standard Motorola
// M6800 databook. Opcodes, cycle counts and -- the part that bites -- the flag
// rules come from those and from nowhere else (DESIGN.md 0.1).
//
// THIS IS A DIFFERENT ANIMAL FROM THE 8080, and two differences run through every
// line below:
//
//   * 16-bit operands are BIG-ENDIAN. A word at an address is (hi << 8) | lo with
//     hi at the LOWER address, and the interrupt stack frame keeps PC/IX high byte
//     at the lower address too. Read one low-first and the transposition is
//     invisible until a JMP lands in the weeds.
//
//   * I/O is MEMORY-MAPPED -- there is no IN/OUT space and this core never calls
//     bus.ioRead/ioWrite. A UART is just an address, and the vectors live in the
//     PROM at the very top of memory (FFF8-FFFF).
//
// A core that merely looks right is the most dangerous thing here (see cpu8080.h):
// the 6800 has no bundled EXM-equivalent, so the flag primitives below are written
// straight from the databook condition-code tables -- half-carry and V on ADD, the
// unaffected-H rule on SUB/NEG, the N^C overflow on every shift, and DAA's table --
// and the real MON680 boot in a later step is the integration oracle
// (altairsim-plausible-but-wrong-timing).

#include "core/bus.h"
#include "cpu/cpu.h"

#include <cstdint>

namespace altair {

class Cpu6800 : public CpuCore {
public:
    const char* isa() const override { return "6800"; }

    std::vector<RegDef> registers() override;
    void reset(Reset) override;
    StepResult step(Bus& bus) override;

    uint16_t pc() const override { return pc_; }
    void setPc(uint16_t v) override { pc_ = v; }

    // WAI holds the processor the way HLT holds an 8080 -- powered, watching for an
    // interrupt -- so it reads back as halted() for the run loop and the idle policy.
    bool halted() const override { return waiting_; }

    // The I flag is the interrupt MASK: set means masked. "Enabled" is its inverse.
    bool interruptsEnabled() const override { return !if_; }

    // NMI is a dedicated edge-triggered pin, not the shared IRQ wire the bus carries.
    // The board that owns this core pulses it; the core latches the edge and takes it
    // at the next instruction boundary, regardless of the I mask.
    void signalNmi() { nmiPending_ = true; }

    // The CCR byte, as TPA reads it and an interrupt stacks it: 1 1 H I N Z V C.
    uint8_t cc() const;
    void setCc(uint8_t v);

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

private:
    // ---- fetch/store. EVERYTHING goes through the Bus (no back door to RAM) ----
    uint8_t fetch(Bus& bus) { return bus.memRead(pc_++); }
    uint16_t fetch16(Bus& bus);                 // big-endian: hi first
    uint16_t read16(Bus& bus, uint16_t a) const;
    void push8(Bus& bus, uint8_t v);
    uint8_t pull8(Bus& bus);
    void pushState(Bus& bus);                   // the 7-byte interrupt frame
    void rti(Bus& bus);

    // ---- flags ----
    void setNZ8(uint8_t r) { nf_ = (r & 0x80) != 0; zf_ = (r == 0); }

    // ALU primitives -- each sets exactly the flags its instruction touches. ADD
    // family sets H; SUB family and NEG do NOT (a 6800 fact 8080 habits get wrong).
    uint8_t add8(uint8_t a, uint8_t m, bool carry);
    uint8_t sub8(uint8_t a, uint8_t m, bool borrow);
    uint8_t and8(uint8_t a, uint8_t m);
    uint8_t or8(uint8_t a, uint8_t m);
    uint8_t eor8(uint8_t a, uint8_t m);
    uint8_t neg8(uint8_t m);
    uint8_t com8(uint8_t m);
    uint8_t inc8(uint8_t m);
    uint8_t dec8(uint8_t m);
    uint8_t clr8();
    void tst8(uint8_t m);
    uint8_t asl8(uint8_t m);
    uint8_t asr8(uint8_t m);
    uint8_t lsr8(uint8_t m);
    uint8_t rol8(uint8_t m);
    uint8_t ror8(uint8_t m);
    void ld16(uint16_t v);                       // N/Z/V for LDX/LDS/STX/STS
    void cpx(uint16_t m);                         // CPX: N/Z/V, C untouched
    void daa();

    // ---- decode helpers, each returns the instruction's cycle count ----
    void branch(Bus& bus, bool take);
    uint32_t accInherentRmw(uint8_t op);            // 40-5F (accumulator only, no bus)
    uint32_t memRmw(Bus& bus, uint8_t op);          // 60-7F
    uint32_t aluBlock(Bus& bus, uint8_t op);        // 80-FF
    uint32_t takeInterrupt(Bus& bus, uint16_t vector);
    uint32_t undefinedOp();                         // an unmapped opcode

    // operand fetch by mode selector (0=imm 1=direct 2=indexed 3=extended)
    void fetchOperand8(Bus& bus, int mode, uint8_t& m, uint32_t& t);
    void fetchOperand16(Bus& bus, int mode, uint16_t& m, uint32_t& t);
    void addrOperand8store(Bus& bus, int mode, uint16_t& addr, uint32_t& t);
    void addrOperand16store(Bus& bus, int mode, uint16_t& addr, uint32_t& t);

    uint8_t a_ = 0, b_ = 0;
    uint16_t x_ = 0, sp_ = 0, pc_ = 0;
    bool hf_ = false, if_ = false, nf_ = false, zf_ = false, vf_ = false, cf_ = false;

    // WAI has already stacked the machine state and is spinning until an interrupt;
    // when one arrives the frame is reused, which is why WAI shortens latency.
    bool waiting_ = false;

    // The 6800 restart sequence reads the reset vector from FFFE/FFFF. reset() only
    // ARMS this (so reset touches no memory, DESIGN.md 6); the first step() does the
    // fetch and clears the latch.
    bool fetchResetVector_ = false;

    // A pending NMI edge, latched by signalNmi(), serviced at the next boundary.
    bool nmiPending_ = false;
};

} // namespace altair
