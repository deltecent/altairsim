#include "test.h"

#include "boards/cpu8080.h"
#include "boards/memory.h"
#include "core/machine.h"
#include "isa/isa.h"

using namespace altair;

namespace {

// A machine with 64K of RAM and an 8080 in it. Built by hand rather than from a
// .toml, because these tests are about the CHIP, and a config bug should not be
// able to make them go red.
struct Rig {
    Machine m;
    MemoryBoard* mem = nullptr;
    Cpu8080* cpu = nullptr;

    Rig() {
        std::string err;
        Board* b = m.add("memory", "mem0", err);
        mem = dynamic_cast<MemoryBoard*>(b);
        Region r;
        r.kind = RegionKind::Ram;
        r.at = 0;
        r.size = 0x10000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", false, err);
        mem->power();

        m.add("8080", "cpu0", err);
        cpu = dynamic_cast<Cpu8080*>(m.cpu());
        cpu->reset(Reset::PowerOn);
    }

    // Assemble by hand at 0, run exactly n instructions, from a clean PC.
    void load(std::initializer_list<uint8_t> code, uint16_t at = 0) {
        uint16_t a = at;
        for (uint8_t b : code) m.bus.memWrite(a++, b);
    }
    void run(int n) {
        for (int i = 0; i < n; ++i) m.master()->step(m.bus);
    }
    uint8_t reg(const char* name) {
        for (const RegDef& r : cpu->registers())
            if (r.name == name) return (uint8_t)r.get();
        return 0xEE;
    }
    uint16_t reg16(const char* name) {
        for (const RegDef& r : cpu->registers())
            if (r.name == name) return (uint16_t)r.get();
        return 0xEEEE;
    }
    void setReg(const char* name, uint32_t v) {
        for (const RegDef& r : cpu->registers())
            if (r.name == name) r.set(v);
    }
};

} // namespace

void test_cpu() {
    SECTION("the 8080 -- the card, and what it is not");

    Rig g;
    CHECK(g.cpu != nullptr, "the machine found a core without being told which board has it");
    CHECK(g.m.master() != nullptr, "and a bus master");
    CHECK(g.m.isa() == "8080", "which says, itself, what instruction set it speaks");

    // THE CPU CARD DECODES NOTHING. It originates cycles; it does not answer them.
    // Ask it about any address or port and it is honestly not there.
    Board* cpuBoard = g.m.find("cpu0");
    BusCycle c{Cycle::MemRead, 0x0100, 0, false};
    CHECK(!cpuBoard->decodes(c), "the CPU card does not decode memory -- it is not a slave");
    c.type = Cycle::IoRead;
    c.addr = 0x10;
    CHECK(!cpuBoard->decodes(c), "nor any port. A plain 88-CPU has nothing on it to reach.");

    CHECK(cpuBoard->units().size() == 1, "one core, and it is a UNIT");
    CHECK(cpuBoard->units()[0].kind == UnitKind::Cpu, "of kind cpu -- neither mounted nor connected");

    SECTION("the 8080 -- flags, which is where a plausible-looking core goes wrong");

    // ANA's half-carry is the OR of bit 3 of the operands. Not zero, not the AND.
    // This single rule is a documented 8080 quirk (the 8085 differs), it is
    // invisible in ordinary code, and 8080EXM fails without it.
    g.setReg("A", 0x08);
    g.setReg("B", 0x00);
    g.load({0xA0});  // ANA B
    g.setReg("PC", 0);
    g.run(1);
    CHECK(g.reg("A") == 0x00, "ANA B: 08 & 00 = 00");
    CHECK(g.reg("AC") == 1, "but AC is SET -- it is (A|B) & 08, not the AND of the results");
    CHECK(g.reg("Z") == 1 && g.reg("CY") == 0, "Z from the result, CY always cleared");

    g.setReg("A", 0x04);
    g.setReg("B", 0x01);
    g.setReg("PC", 0);
    g.run(1);
    CHECK(g.reg("AC") == 0, "and CLEAR when neither operand has bit 3");

    // SUB is A + ~B + 1 through the one adder the chip has. CY is the INVERTED
    // carry-out, and AC comes from that same addition -- write it as a borrow rule
    // that "looks right" and it is wrong on exactly the operands nobody tries.
    g.setReg("A", 0x00);
    g.setReg("B", 0x01);
    g.load({0x90});  // SUB B
    g.setReg("PC", 0);
    g.run(1);
    CHECK(g.reg("A") == 0xFF, "SUB: 00 - 01 = FF");
    CHECK(g.reg("CY") == 1, "and it borrowed, so CY is set");
    CHECK(g.reg("AC") == 0, "AC is the carry out of bit 3 of A + ~B + 1 -- here, none");
    CHECK(g.reg("S") == 1 && g.reg("Z") == 0 && g.reg("P") == 1, "S Z P from the difference");

    // Parity is EVEN parity over all 8 bits -- not the low bit, and not odd.
    g.setReg("A", 0x03);
    g.setReg("B", 0x00);
    g.load({0xB0});  // ORA B
    g.setReg("PC", 0);
    g.run(1);
    CHECK(g.reg("P") == 1, "03 has two bits set: EVEN parity, so P is SET");
    g.setReg("A", 0x07);
    g.setReg("PC", 0);
    g.run(1);
    CHECK(g.reg("P") == 0, "07 has three: odd, so P is CLEAR");

    // INR/DCR touch every flag BUT carry. That exception is the entire point of
    // them: it lets a loop counter be decremented inside a multi-byte add without
    // destroying the carry being propagated.
    g.setReg("A", 0xFF);
    g.setReg("CY", 1);
    g.setReg("B", 0x0F);
    g.load({0x04});  // INR B
    g.setReg("PC", 0);
    g.run(1);
    CHECK(g.reg("B") == 0x10, "INR B: 0F -> 10");
    CHECK(g.reg("AC") == 1, "the low nibble wrapped, so AC is set");
    CHECK(g.reg("CY") == 1, "and CARRY IS UNTOUCHED -- INR does not have one");

    // DAA, the one instruction where AC is load-bearing rather than decorative.
    g.setReg("A", 0x9B);
    g.setReg("CY", 0);
    g.setReg("AC", 0);
    g.load({0x27});  // DAA
    g.setReg("PC", 0);
    g.run(1);
    CHECK(g.reg("A") == 0x01, "DAA 9B -> 01 (+06 then +60)");
    CHECK(g.reg("CY") == 1, "and it carried out of the tens digit");

    SECTION("the 8080 -- T-states, which are load-bearing and not decoration");

    // These drive the clock throttle, the baud rate, and disk rotation. A core
    // whose T-states are "about right" gives you a machine whose serial port is
    // subtly the wrong speed, which presents as garbage characters and gets blamed
    // on the UART for a week.
    g.setReg("PC", 0);
    g.load({0x00});  // NOP
    StepResult r = g.m.master()->step(g.m.bus);
    CHECK(r.tStates == 4, "NOP is 4");

    g.setReg("PC", 0);
    g.load({0x21, 0x00, 0x20});  // LXI H,2000
    r = g.m.master()->step(g.m.bus);
    CHECK(r.tStates == 10, "LXI is 10");

    // A conditional CALL costs 17 taken and 11 not -- the ONLY place the 8080
    // charges differently for a branch. A conditional JUMP is 10 either way,
    // because the address has already been fetched by the time it decides.
    g.setReg("PC", 0);
    g.setReg("SP", 0x1000);
    g.setReg("Z", 1);
    g.load({0xC4, 0x00, 0x30});  // CNZ 3000 -- not taken, Z is set
    r = g.m.master()->step(g.m.bus);
    CHECK(r.tStates == 11, "a conditional CALL not taken is 11");
    CHECK(g.reg16("PC") == 3, "and it falls through");

    g.setReg("PC", 0);
    g.setReg("Z", 0);
    r = g.m.master()->step(g.m.bus);  // now it IS taken
    CHECK(r.tStates == 17, "and 17 when taken");
    CHECK(g.reg16("PC") == 0x3000, "at the target");
    CHECK(g.reg16("SP") == 0x0FFE, "with the return address pushed");
    CHECK(g.m.bus.memRead(0x0FFE) == 0x03 && g.m.bus.memRead(0x0FFF) == 0x00,
          "and the return address is the NEXT instruction, low byte first");

    SECTION("the 8080 -- EI's one-instruction delay, and HLT");

    // EI DOES NOT TAKE EFFECT UNTIL AFTER THE FOLLOWING INSTRUCTION. That grace
    // period is why `EI / RET` at the end of a handler is safe: the RET pops the
    // return address before another interrupt can land on it. Model EI as
    // immediate and a busy interrupt source eats the stack, slowly, and the crash
    // lands nowhere near the cause.
    g.cpu->reset(Reset::PowerOn);
    g.setReg("PC", 0);
    g.load({0xFB, 0x00, 0x00});  // EI ; NOP ; NOP
    g.m.master()->step(g.m.bus);
    CHECK(g.reg("IE") == 0, "after EI itself, interrupts are still OFF");
    g.m.master()->step(g.m.bus);
    CHECK(g.reg("IE") == 1, "and they come on only after the NEXT instruction");

    // HLT holds the processor, but TIME STILL PASSES -- which matters, because the
    // interrupt that will wake it comes from a board clocked by these very
    // T-states. A HLT that stopped the clock would be a machine that could never
    // be woken.
    g.setReg("PC", 0);
    g.load({0x76});  // HLT
    r = g.m.master()->step(g.m.bus);
    CHECK(r.status == RunStatus::Halted, "HLT halts");
    CHECK(r.tStates == 7, "and costs 7");
    r = g.m.master()->step(g.m.bus);
    CHECK(r.status == RunStatus::Halted, "and it STAYS halted");
    CHECK(r.tStates > 0, "while time keeps passing, so a board can still interrupt it");

    SECTION("the 8080 -- a fetch from an empty socket is RST 7, and nobody wrote that rule");

    // There is no code at E000 in this rig... except there is: it is 64K of RAM.
    // So build the real case -- a machine whose backplane has a HOLE in it -- and
    // let the floating bus do what it does. The CPU has no special case for this
    // and neither does the bus: an unclaimed read is 0xFF, 0xFF is RST 7, and a
    // machine that jumps into empty space lands at 0038. That is what the metal
    // does.
    Machine h;
    std::string err;
    Board* hm = h.add("memory", "mem0", err);
    Region r4;
    r4.kind = RegionKind::Ram;
    r4.at = 0;
    r4.size = 0x1000;  // 4K, and NOTHING above it
    dynamic_cast<MemoryBoard*>(hm)->addRegion(r4, err);
    setProperty(*hm, "fill", "zero", false, err);
    hm->power();
    h.add("8080", "cpu0", err);
    CpuCore* hc = h.cpu();
    hc->reset(Reset::PowerOn);
    hc->setPc(0xE000);   // into the hole
    h.bus.memWrite(0x0FFF, 0x00);
    StepResult hs = h.master()->step(h.bus);
    CHECK(hc->pc() == 0x0038, "a fetch from an empty backplane floats to FF = RST 7 -> 0038");
    CHECK(hs.tStates == 11, "charged as the RST it is");
}
