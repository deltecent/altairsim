#pragma once
//
// CpuCore -- registers and execute. THE CHIP, NOT THE CARD (DESIGN.md 3).
//
// A core is a plain object. It is NOT a Board: it has no slot, decodes no
// address, owns no clock crystal, and cannot be pulled out of the backplane with
// your hand. All of that belongs to the card that carries it (src/boards/), and
// the separation is what lets two different 8080 cards -- one with an onboard
// serial port, one without -- share this file completely.
//
// It is also what makes the validation gate (DESIGN.md 3.2) easy: TST8080 and
// 8080EXM test the CHIP, and here the chip is a thing you can hold on its own.

#include "core/bus.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace altair {

// ---------------------------------------------------------------------------
// REGISTERS ARE REFLECTION -- the same trick as Board::properties() (DESIGN.md
// 3.0.3, 5).
//
// A core describes its own registers; it does NOT expose a struct the monitor
// knows the shape of. So REGS, SET REG A=3F, breakpoint conditions, SNAPSHOT and
// the MCP schema are written ONCE, generically, and a Z80 or a 6502 gets every
// one of them on the day it lands with no change to the monitor.
//
// This is the same bet that already paid for SET/SHOW/TOML/MCP. There is one
// schema and no second copy to drift.
// ---------------------------------------------------------------------------
struct RegDef {
    std::string name;   // "A", "PC", "SP", "F"
    int bits = 8;       // 8 or 16 -- decides the width SHOW prints and what fits
    std::string help;   // "accumulator", "program counter"
    std::function<uint32_t()> get;
    std::function<void(uint32_t)> set;
};

class CpuCore {
public:
    virtual ~CpuCore() = default;

    // The INSTRUCTION SET this core speaks -- a key into disassemblerFor().
    // DISASM asks the machine, the machine asks the active core, and so you never
    // type CPU= (DESIGN.md 3.0.2). On the dual-CPU card, when the guest OUTs to
    // switch from the 8080 to the 8085, DISASM follows automatically -- because
    // "which instruction set" and "which core is active" are the same question,
    // and it is already being asked.
    virtual const char* isa() const = 0;

    virtual std::vector<RegDef> registers() = 0;

    // Both resets: PC<-0 and interrupts off. NEITHER TOUCHES MEMORY (DESIGN.md 6)
    // -- a core has no memory to touch, which is the tidiest possible proof.
    virtual void reset(Reset) = 0;

    // Run ONE instruction, originating real bus cycles. Everything the core knows
    // about the outside world arrives through that Bus& and nothing else: no
    // pointer to RAM, no back door. So a fetch from an empty socket reads 0xFF and
    // executes as RST 7, exactly as the metal does, with no special case.
    virtual StepResult step(Bus&) = 0;

    // PC gets named accessors on top of the reflection layer because the debugger
    // asks for it on EVERY step and every core has one. Everything else goes
    // through registers().
    virtual uint16_t pc() const = 0;
    virtual void setPc(uint16_t) = 0;

    virtual bool halted() const = 0;
    virtual bool interruptsEnabled() const = 0;
};

// ---------------------------------------------------------------------------
// A CARD THAT CARRIES PROCESSORS. This is how the debugger finds the CPU without
// knowing what board it is on, or what chip it is.
//
// It is deliberately not "the CPU board" -- a card may carry more than one core
// (DESIGN.md 3.0.1), and it may carry a great deal that is not a core at all: a
// serial port, a boot PROM. Those are the card's business. This interface asks
// exactly one question, which is the only one the debugger has any right to ask.
// ---------------------------------------------------------------------------
class CpuCard {
public:
    virtual ~CpuCard() = default;

    // The core that is RUNNING. On a dual-processor card the guest can change the
    // answer with an OUT, and everything downstream -- REGS, DISASM, breakpoints --
    // follows automatically, because they all ask this same question every time
    // rather than caching what they found once.
    virtual CpuCore* activeCore() = 0;
};

} // namespace altair
