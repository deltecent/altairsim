#include "test.h"

#include "boards/s100-memory.h"
#include "core/machine.h"
#include "cpu/cpu8085.h"

using namespace altair;

// THE GATE IS THE CP/M EXERCISERS (tests/cputest.cpp), not this file -- the four
// stock 8080 suites re-run against the 8085 board re-prove the whole shared subset.
// These are the targeted CHECKs on the 8085-ONLY surface those suites cannot reach:
// RIM/SIM, the TRAP + RST 5.5/6.5/7.5 interrupt system with its masks/latches and
// priority, the SID/SOD pins, and the one documented flag divergence from the 8080
// (ANA's half-carry, which the 8085 always sets). The core is driven directly
// against a Bus -- there is no 8085 machine here.

namespace {

// The same test interrupt source the Z80 test uses: it pulls pINT and drives the
// vector byte onto the IntAck cycle, exactly as a real vectored-interrupt card does
// (DESIGN.md 4.4). This exercises the 8085's INTR line -- its on-chip TRAP/RST n.5
// are separate internal latches, poked through registers() below.
class IntSource : public Board {
public:
    uint8_t vector = 0xFF;
    bool asserting = false;

    std::string type() const override { return "test-int"; }
    bool assertsInt() const override { return asserting; }
    bool decodes(const BusCycle& c) const override { return c.type == Cycle::IntAck; }
    uint8_t read(const BusCycle&) override { return vector; }
    std::vector<Property> properties() override { return {}; }

    void raise(uint8_t v) { vector = v; asserting = true; intChanged(); }
    void drop() { asserting = false; intChanged(); }
};

struct Rig {
    Machine m;
    MemoryBoard* mem = nullptr;
    Cpu8085 cpu;
    IntSource irq;

    Rig() {
        std::string err;
        Board* b = m.add("memory", "mem0", err);
        mem = dynamic_cast<MemoryBoard*>(b);
        Region r;
        r.kind = RegionKind::Ram;
        r.at = 0;
        r.size = 0x10000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);
        mem->power();

        irq.id = "irq0";
        m.bus.attach(&irq);

        cpu.reset(Reset::PowerOn);
    }
    ~Rig() { m.bus.detach(&irq); }

    void load(std::initializer_list<uint8_t> code, uint16_t at = 0) {
        uint16_t a = at;
        for (uint8_t byte : code) m.bus.memWrite(a++, byte);
    }
    void poke(uint16_t a, uint8_t v) { m.bus.memWrite(a, v); }
    uint8_t peek(uint16_t a) { return m.bus.peek(a); }
    StepResult step() { return cpu.step(m.bus); }
    void run(int n) { for (int i = 0; i < n; ++i) cpu.step(m.bus); }

    uint32_t reg(const char* name) {
        for (const RegDef& r : cpu.registers())
            if (r.name == name) return r.get();
        return 0xEEEEEEEE;
    }
    void setReg(const char* name, uint32_t v) {
        for (const RegDef& r : cpu.registers())
            if (r.name == name) r.set(v);
    }
    bool flag(const char* name) { return reg(name) != 0; }
};

} // namespace

void test_8085_cpu() {
    SECTION("the 8085 core -- reflection, and what the monitor sees");
    {
        Rig g;
        CHECK(std::string(g.cpu.isa()) == "8085", "it speaks 8085");
        CHECK(g.reg("M55") == 1 && g.reg("M65") == 1 && g.reg("M75") == 1,
              "reset leaves all three RST masks SET (nothing fires until SIM unmasks)");
        CHECK(g.reg("I75") != 0xEEEEEEEE && g.reg("TRAP") != 0xEEEEEEEE &&
                  g.reg("SID") != 0xEEEEEEEE && g.reg("SOD") != 0xEEEEEEEE,
              "the 8085 interrupt latches and serial pins are reachable by name");
        g.setReg("HL", 0x1234);
        CHECK(g.reg("H") == 0x12 && g.reg("L") == 0x34, "HL is the H,L pair, high byte first");
    }

    SECTION("RIM -- reads SID, the pending lines, INTE and the masks into A");
    {
        // INTE OFF while we read the pending lines back -- otherwise an unmasked
        // pending line would fire the interrupt before RIM ever executes.
        Rig g;
        g.setReg("IE", 0);
        g.setReg("M55", 1); g.setReg("M65", 0); g.setReg("M75", 1);
        g.setReg("I55", 0); g.setReg("I65", 1); g.setReg("I75", 1);
        g.setReg("SID", 1);
        g.load({0x20});  // RIM
        g.run(1);
        // b7 SID=1 b6 I7.5=1 b5 I6.5=1 b4 I5.5=0 | b3 IE=0 b2 M7.5=1 b1 M6.5=0 b0 M5.5=1
        CHECK((uint8_t)g.reg("A") == 0xE5, "RIM packs SID, the pending lines and the masks");

        // INTE and the masks read back too -- reset leaves all three masks set, and
        // with nothing pending, INTE=1 fires nothing.
        Rig h;
        h.setReg("IE", 1);
        h.load({0x20});  // RIM
        h.run(1);
        CHECK((uint8_t)h.reg("A") == 0x0F, "RIM shows INTE set and all masks set, nothing pending");
    }

    SECTION("SIM -- sets the masks (MSE), resets the 7.5 latch (R7.5), drives SOD (SOE)");
    {
        Rig g;
        g.setReg("A", 0x0D);  // MSE=1 | M7.5=1 M6.5=0 M5.5=1
        g.load({0x30});       // SIM
        g.run(1);
        CHECK(g.reg("M55") == 1 && g.reg("M65") == 0 && g.reg("M75") == 1,
              "SIM with MSE loaded the masks from bits 2-0");

        Rig h;
        h.setReg("M65", 1);
        h.setReg("A", 0x00);  // MSE=0 -- masks must be left alone
        h.load({0x30});
        h.run(1);
        CHECK(h.reg("M65") == 1, "SIM without MSE leaves the masks untouched");

        Rig r;
        r.setReg("I75", 1);
        r.setReg("A", 0x10);  // R7.5=1
        r.load({0x30});
        r.run(1);
        CHECK(r.reg("I75") == 0, "SIM bit 4 (R7.5) cleared the 7.5 edge latch");

        Rig s;
        s.setReg("A", 0xC0);  // SOE=1, SOD=1
        s.load({0x30});
        s.run(1);
        CHECK(s.reg("SOD") == 1, "SIM with SOE latched SOD high");
        s.setReg("A", 0x00);  // SOE=0 -- SOD must not change
        s.load({0x30}, 1);
        s.run(1);
        CHECK(s.reg("SOD") == 1, "SIM without SOE left SOD alone");
    }

    SECTION("TRAP -- non-maskable, vectors to 0x24, pushes PC, disables interrupts");
    {
        Rig g;
        g.setReg("SP", 0x8000);
        g.setReg("PC", 0x1234);
        g.setReg("IE", 0);     // even with INTE off...
        g.setReg("TRAP", 1);   // ...TRAP still fires
        g.step();
        CHECK(g.reg("PC") == 0x0024, "TRAP vectors to 0x24");
        CHECK(g.reg("SP") == 0x7FFE, "TRAP pushed the return address");
        CHECK(g.peek(0x7FFE) == 0x34 && g.peek(0x7FFF) == 0x12,
              "the pushed word is the interrupted PC, low byte at the lower address");
        CHECK(g.reg("IE") == 0, "interrupts are disabled on entry");
    }

    SECTION("RST 5.5/6.5/7.5 -- vectors, and the 7.5 edge latch vs 6.5/5.5 level");
    {
        Rig g;
        g.setReg("SP", 0x8000); g.setReg("PC", 0x0500);
        g.setReg("IE", 1);
        g.setReg("M75", 0); g.setReg("I75", 1);
        g.step();
        CHECK(g.reg("PC") == 0x003C, "RST 7.5 vectors to 0x3C");
        CHECK(g.reg("I75") == 0, "the 7.5 EDGE latch cleared when it was serviced");
        CHECK(g.reg("IE") == 0, "and INTE cleared on entry");

        Rig h;
        h.setReg("IE", 1); h.setReg("M65", 0); h.setReg("I65", 1);
        h.step();
        CHECK(h.reg("PC") == 0x0034, "RST 6.5 vectors to 0x34");
        CHECK(h.reg("I65") == 1, "6.5 is LEVEL-sensitive -- service does not clear it");

        Rig k;
        k.setReg("IE", 1); k.setReg("M55", 0); k.setReg("I55", 1);
        k.step();
        CHECK(k.reg("PC") == 0x002C, "RST 5.5 vectors to 0x2C");
    }

    SECTION("masking and the INTE gate hold a pending RST off");
    {
        Rig g;
        g.setReg("PC", 0x0000);
        g.setReg("IE", 1);
        g.setReg("M55", 1); g.setReg("I55", 1);  // pending, but masked
        g.load({0x00});                          // NOP
        g.step();
        CHECK(g.reg("PC") == 0x0001, "a MASKED RST 5.5 does not fire -- the NOP ran instead");

        Rig h;
        h.setReg("PC", 0x0000);
        h.setReg("IE", 0);                       // INTE off
        h.setReg("M65", 0); h.setReg("I65", 1);  // unmasked and pending
        h.load({0x00});
        h.step();
        CHECK(h.reg("PC") == 0x0001, "with INTE off, RST 6.5 is held -- the NOP ran");
    }

    SECTION("interrupt priority -- TRAP > RST7.5 > RST6.5 > RST5.5");
    {
        Rig g;
        g.setReg("SP", 0x8000); g.setReg("PC", 0x0000);
        g.setReg("IE", 1);
        g.setReg("M55", 0); g.setReg("M65", 0); g.setReg("M75", 0);
        g.setReg("I55", 1); g.setReg("I65", 1); g.setReg("I75", 1);
        g.setReg("TRAP", 1);

        g.step();
        CHECK(g.reg("PC") == 0x0024, "TRAP wins over every pending RST n.5");

        g.setReg("TRAP", 0);   // TRAP line drops
        g.setReg("IE", 1);     // the handler re-enables interrupts
        g.step();
        CHECK(g.reg("PC") == 0x003C, "then RST 7.5 (still pending) is highest");

        g.setReg("IE", 1);
        g.step();
        CHECK(g.reg("PC") == 0x0034, "then RST 6.5");

        g.setReg("I65", 0);    // the 6.5 line drops
        g.setReg("IE", 1);
        g.step();
        CHECK(g.reg("PC") == 0x002C, "then RST 5.5");
    }

    SECTION("INTR -- the 8080-style line, unchanged: opcode injected from the bus");
    {
        Rig g;
        g.setReg("SP", 0x8000); g.setReg("PC", 0x0700);
        g.setReg("IE", 1);
        g.irq.raise(0xFF);   // the device drives RST 7 (0xFF) onto the acknowledge
        g.step();
        CHECK(g.reg("PC") == 0x0038, "INTR fetched the injected RST 7 and vectored to 0x38");
        CHECK(g.reg("SP") == 0x7FFE, "and pushed the return address");
        CHECK(g.reg("IE") == 0, "INTE cleared on acknowledge");

        Rig h;
        h.setReg("PC", 0x0000);
        h.setReg("IE", 0);
        h.irq.raise(0xFF);
        h.load({0x00});
        h.step();
        CHECK(h.reg("PC") == 0x0001, "with INTE off, INTR is ignored -- the NOP ran");
    }

    SECTION("HLT stands down and any interrupt wakes it");
    {
        Rig g;
        g.setReg("SP", 0x8000); g.setReg("PC", 0x0000);
        g.load({0x76});  // HLT
        g.step();
        StepResult r = g.step();
        CHECK(r.status == RunStatus::Halted, "the core is halted, marking time");
        g.setReg("TRAP", 1);
        g.step();
        CHECK(g.reg("PC") == 0x0024, "TRAP woke the core out of HLT and vectored");
    }

    SECTION("ANA/ANI -- the faithful 8085 rule (AC is always set)");
    {
        // The 8085's logical AND always SETS the auxiliary carry, where the 8080
        // sets it to the OR of bit 3 of the two operands. This is the one
        // documented-flag divergence between the cores, and it is what makes the
        // 8085 gate 8085EXM (real-8085 CRCs) rather than 8080EXM. Pin it with a
        // case the two rules DISAGREE on: bit 3 clear in both operands, where the
        // 8080 would leave AC=0 but the 8085 sets it. (issue #347)
        Rig g;
        g.setReg("A", 0x01);
        g.setReg("B", 0x01);
        g.load({0xA0});  // ANA B
        g.run(1);
        CHECK(g.reg("A") == 0x01 && !g.flag("Z"), "ANA B -> A=01");
        CHECK(g.flag("AC"), "AC is set with bit 3 clear in both operands -- the 8085 rule");

        // ANI shares the ALU path and follows the same rule.
        g.setReg("PC", 0);
        g.setReg("A", 0x01);
        g.load({0xE6, 0x01});  // ANI 01
        g.run(1);
        CHECK(g.flag("AC"), "ANI also always sets AC on the 8085");
    }
}
