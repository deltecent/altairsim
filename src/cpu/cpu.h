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

class StateWriter;  // core/statefile.h -- SNAPSHOT/RESTORE
class StateReader;

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
// HOW a register wants to APPEAR. The monitor prints a one-line, DDT/SID-style
// status at every stop:
//
//     C0Z1M0E1I0 A=3F B=0000 D=00FF H=8000 S=0100 IE=1 P=0102  MOV A,B
//
// The LAYOUT of that line -- which registers pair up, which ones are lamps, what
// each is called, and in what order -- is the CORE's to describe, because it is
// genuinely different for a Z80 or a 6502. But it is the MONITOR's to render, and
// the monitor still knows nothing: it reads `show`, `label` and `bits`, and that
// is all. Reflection holds (3.0.3); we did not hand the core a printf.
enum class RegShow {
    Flag,   // a lamp: LABEL then one digit, no spaces, clustered at the FRONT
    Field,  // LABEL=hex, printed in the order the core listed it
    Off,    // reachable BY NAME (SET REG, breakpoints, MCP) but not on the line
};

struct RegDef {
    // NAME is identity -- what you TYPE, and what SET REG and a breakpoint
    // condition look up. LABEL is presentation -- what the status line CALLS it.
    // Keeping them apart is what lets the 8080 print DDT's `M` for the sign flag
    // without `SET REG M=1` quietly becoming a thing.
    std::string name;   // "A", "PC", "SP", "F", "HL", "CY"
    int bits = 8;       // 1, 8 or 16 -- decides the width SHOW prints and what fits
    std::string label;  // what the status line calls it; empty -> name
    RegShow show = RegShow::Field;
    std::string help;   // "accumulator", "program counter"
    std::function<uint32_t()> get;
    std::function<void(uint32_t)> set;

    const std::string& shown() const { return label.empty() ? name : label; }
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

    // SNAPSHOT/RESTORE (DESIGN.md 13). Every architectural register AND the hidden
    // micro-state that registers() does not expose -- the EI-after-next latch, the
    // mid-INTA fetch flag, and on a Z80 the WZ/MEMPTR latch, IFF2, the interrupt
    // mode and the alternate bank. registers() is enough for REGS and SET REG; it
    // is NOT enough to resume execution cycle-for-cycle, so a core writes its state
    // explicitly here rather than being walked generically.
    virtual void serialize(StateWriter& w) const = 0;
    virtual void deserialize(StateReader& r) = 0;
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

    // ---- THE ACHIEVED CRYSTAL. A DIAGNOSTIC, AND IT IS HOST-MEASURED. ----
    //
    // clock_hz is the crystal you ASKED for; this is the one the machine actually
    // turned -- T-states retired per REAL second, last time the run loop ran. It is
    // what made bug #6 legible in one line: "asked 8 MHz, reached 3.9". SHOW reads it
    // back as a read-only companion to clock_hz on the same card.
    //
    // The run loop times ITSELF against the host wall clock and reports the number
    // here; nothing in the simulator ever asks the host what time it is (core/clock.h),
    // and the card only stores what it was handed. So this stays off the Clock, whose
    // whole invariant is that emulated time is a pure function of the instruction
    // stream -- a host-measured rate living there would be a determinism trap waiting
    // for the first board that read it.
    //
    // Defaulted, not pure: a CpuCard that carries no crystal (or that nobody paces)
    // simply never reports, and SHOW reads 0 -- "it has not run", which is the truth.
    virtual void      reportAchievedHz(long long) {}
    virtual long long achievedHz() const { return 0; }
};

} // namespace altair
