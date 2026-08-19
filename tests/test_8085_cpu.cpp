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

    // ---- The two undocumented condition bits, V and K. The stock exercisers
    // CANNOT reach these: 8085EXM masks the flag byte with 0D5h, so V (bit 1) and K
    // (bit 5) are invisible to it. Every expected value below is hand-derived from
    // Ken Shirriff's silicon rules (reference/Intel 8085 undocumented instructions
    // and flags.md): V = carry-in XOR carry-out of bit 7; K = V XOR sign, except
    // INX/DCX where K is the carry out of the 16-bit incrementer. The oracle is the
    // die analysis applied by hand, NOT this core grading itself (DESIGN.md 3.2). ----

    SECTION("V/K on add and subtract -- signed overflow, and the useful compare bit");
    {
        // 0x7F + 1 = 0x80: the textbook +127 -> -128 overflow. V set; result is
        // negative so K = V XOR sign = 1 XOR 1 = 0.
        Rig g;
        g.setReg("A", 0x7F);
        g.load({0xC6, 0x01});  // ADI 01
        g.run(1);
        CHECK(g.reg("A") == 0x80 && g.flag("V") && !g.flag("K"),
              "ADI 7F+1 overflows: V set, K clear (result is negative)");

        // 0x80 + 0x80 = 0x00: -128 + -128 overflows to 0. V set, result non-negative,
        // so K = 1 XOR 0 = 1.
        Rig h;
        h.setReg("A", 0x80); h.setReg("B", 0x80);
        h.load({0x80});  // ADD B
        h.run(1);
        CHECK(h.reg("A") == 0x00 && h.flag("V") && h.flag("K"),
              "ADD 80+80 overflows to 0: V and K both set");

        // 0x10 + 0x10 = 0x20: no overflow, positive result. V and K both clear.
        Rig n;
        n.setReg("A", 0x10);
        n.load({0xC6, 0x10});  // ADI 10
        n.run(1);
        CHECK(!n.flag("V") && !n.flag("K"), "ADI 10+10: no overflow, V and K clear");

        // CMP: K = 1 exactly when the second value is larger than the first (a signed
        // 'below'). 1 vs 2 -> K set; the accumulator is left untouched.
        Rig c;
        c.setReg("A", 0x01);
        c.load({0xFE, 0x02});  // CPI 02
        c.run(1);
        CHECK(c.reg("A") == 0x01 && !c.flag("V") && c.flag("K"),
              "CPI 1<2: second value larger sets K (V clear here)");

        Rig d;
        d.setReg("A", 0x02);
        d.load({0xFE, 0x01});  // CPI 01
        d.run(1);
        CHECK(!d.flag("K"), "CPI 2>1: second value not larger, K clear");

        // 0x80 - 1 = 0x7F: -128 - 1 overflows. V set, result positive, K = 1 XOR 0 = 1.
        Rig s;
        s.setReg("A", 0x80);
        s.load({0xD6, 0x01});  // SUI 01
        s.run(1);
        CHECK(s.reg("A") == 0x7F && s.flag("V") && s.flag("K"),
              "SUI 80-1 overflows: V and K set");
    }

    SECTION("V/K on INR/DCR -- only at the signed boundary");
    {
        Rig g;  // INR sets V only on 0x7F -> 0x80
        g.setReg("A", 0x7F);
        g.load({0x3C});  // INR A
        g.run(1);
        CHECK(g.reg("A") == 0x80 && g.flag("V") && !g.flag("K"), "INR 7F->80: V set, K clear");

        Rig h;
        h.setReg("A", 0x10);
        h.load({0x3C});  // INR A
        h.run(1);
        CHECK(!h.flag("V"), "INR 10->11: no overflow, V clear");

        Rig d;  // DCR sets V only on 0x80 -> 0x7F
        d.setReg("A", 0x80);
        d.load({0x3D});  // DCR A
        d.run(1);
        CHECK(d.reg("A") == 0x7F && d.flag("V") && d.flag("K"), "DCR 80->7F: V and K set");

        Rig z;  // 0x00 -> 0xFF: no overflow, but the result is negative, so K = sign
        z.setReg("A", 0x00);
        z.load({0x3D});  // DCR A
        z.run(1);
        CHECK(z.reg("A") == 0xFF && !z.flag("V") && z.flag("K"),
              "DCR 00->FF: V clear, K = sign of the result");
    }

    SECTION("V/K on the logical ops -- V forced 0, so K is just the sign");
    {
        Rig g;
        g.setReg("A", 0x80);
        g.load({0xB7});  // ORA A -- leaves 0x80, negative
        g.run(1);
        CHECK(!g.flag("V") && g.flag("K"), "ORA leaves V=0, so K follows the sign (set)");

        Rig h;
        h.setReg("A", 0x0F);
        h.load({0xE6, 0x0F});  // ANI 0F -- positive result
        h.run(1);
        CHECK(!h.flag("V") && !h.flag("K"), "ANI positive result: V and K clear");
    }

    SECTION("V/K on INX/DCX -- the special case: K is the 16-bit carry, V untouched");
    {
        // INX of 0xFFFF carries out of the incrementer -> K set. And V must be left
        // exactly as it was: seed V=1 first and prove INX does not disturb it.
        Rig g;
        g.setReg("V", 1);
        g.setReg("HL", 0xFFFF);
        g.load({0x23});  // INX H
        g.run(1);
        CHECK(g.reg("HL") == 0x0000 && g.flag("K"), "INX of 0xFFFF sets K (carry out)");
        CHECK(g.flag("V"), "INX left V untouched (the one non-V-XOR-sign case)");

        Rig n;
        n.setReg("V", 0);
        n.setReg("HL", 0x1234);
        n.load({0x23});  // INX H
        n.run(1);
        CHECK(!n.flag("K") && !n.flag("V"), "INX of a non-max pair: K clear, V still untouched");

        // DCX of 0x0000 borrows out of the decrementer -> K set.
        Rig d;
        d.setReg("HL", 0x0000);
        d.load({0x2B});  // DCX H
        d.run(1);
        CHECK(d.reg("HL") == 0xFFFF && d.flag("K"), "DCX of 0x0000 sets K (borrow out)");

        Rig e;
        e.setReg("HL", 0x0001);
        e.load({0x2B});  // DCX H
        e.run(1);
        CHECK(!e.flag("K"), "DCX of 0x0001: no borrow, K clear");
    }

    SECTION("V/K on the rotates -- documented carry-only, but the ALU still latches them");
    {
        // Left rotates are treated as A+A, so V = bit6 XOR bit7 of the old A.
        Rig g;
        g.setReg("A", 0x40);   // bit6=1, bit7=0 -> V=1; result 0x80 negative -> K=0
        g.load({0x07});  // RLC
        g.run(1);
        CHECK(g.reg("A") == 0x80 && g.flag("V") && !g.flag("K"), "RLC 0x40: V set, K clear");

        Rig h;
        h.setReg("A", 0x80);   // bit6=0, bit7=1 -> V=1; result 0x01 positive -> K=1
        h.load({0x07});  // RLC
        h.run(1);
        CHECK(h.reg("A") == 0x01 && h.flag("V") && h.flag("K"), "RLC 0x80: V and K set");

        // Right rotates have a constant internal carry, so V=0 and K falls to the sign.
        Rig r;
        r.setReg("A", 0x01);
        r.load({0x0F});  // RRC -> 0x80, negative
        r.run(1);
        CHECK(r.reg("A") == 0x80 && !r.flag("V") && r.flag("K"), "RRC 0x01: V=0, K = sign (set)");
    }

    SECTION("V and K ride the PSW where the 8080 keeps constants (bits 1 and 5)");
    {
        // PUSH PSW must place V in bit 1 and K in bit 5 -- the two positions the 8080
        // nails to 1 and 0. Produce V=1,K=1 (0x80+0x80) then push and read the byte.
        Rig g;
        g.setReg("SP", 0x8000);
        g.setReg("A", 0x80); g.setReg("B", 0x80);
        g.load({0x80, 0xF5});  // ADD B ; PUSH PSW
        g.run(2);
        uint8_t f = g.peek(0x7FFE);  // the pushed flags byte
        CHECK((f & 0x02) != 0, "V is PSW bit 1 (PUSH PSW)");
        CHECK((f & 0x20) != 0, "K is PSW bit 5 (PUSH PSW)");
        CHECK((f & 0x08) == 0, "bit 3 stays 0, as on both parts");

        // POP PSW round-trips them back into the flags.
        Rig h;
        h.setReg("SP", 0x7FFE);
        h.poke(0x7FFE, 0x22);  // bits 1 and 5 set: V and K
        h.poke(0x7FFF, 0x00);
        h.load({0xF1});  // POP PSW
        h.run(1);
        CHECK(h.flag("V") && h.flag("K"), "POP PSW restores V and K from bits 1 and 5");
    }

    // ---- The five SAFE undocumented opcodes. On the 8085 these bytes are NOT the
    // 8080's duplicate JMP/CALL/RET: RSTV (0xCB), SHLX (0xD9), LHLX (0xED), JNK
    // (0xDD) and JK (0xFD). None touches a flag; the two branches only READ the V/K
    // bits proven above. Expected effects are from the octal table + Shirriff
    // (reference/Intel 8085 undocumented instructions and flags.md 2). ----

    SECTION("SHLX / LHLX -- the DE-addressed twins of SHLD / LHLD");
    {
        // SHLX (0xD9) stores HL at (DE), low byte first. It must NOT act as the
        // 8080's undocumented RET.
        Rig g;
        g.setReg("HL", 0xBEEF);
        g.setReg("DE", 0x4000);
        g.load({0xD9});  // SHLX
        g.run(1);
        CHECK(g.peek(0x4000) == 0xEF && g.peek(0x4001) == 0xBE,
              "SHLX wrote L then H at (DE)");
        CHECK(g.reg("PC") == 0x0001, "SHLX is one byte -- it did not RET off the stack");

        // LHLX (0xED) loads HL from (DE). It must NOT act as the 8080's undoc CALL.
        Rig h;
        h.setReg("DE", 0x4000);
        h.poke(0x4000, 0x34);
        h.poke(0x4001, 0x12);
        h.load({0xED});  // LHLX
        h.run(1);
        CHECK(h.reg("HL") == 0x1234, "LHLX read HL from (DE), low byte first");
        CHECK(h.reg("PC") == 0x0001, "LHLX is one byte -- it did not CALL");

        // The round trip: LHLX after SHLX of a different pointer.
        Rig r;
        r.setReg("HL", 0xABCD);
        r.setReg("DE", 0x5000);
        r.load({0xD9,          // SHLX  -> (5000)=CD (5001)=AB
                0x21, 0x00, 0x00,  // LXI H,0000 -- clobber HL
                0xED});         // LHLX  -> HL back to ABCD
        r.run(3);
        CHECK(r.reg("HL") == 0xABCD, "SHLX then LHLX round-trips HL through (DE)");
    }

    SECTION("RSTV -- RST to 0x0040 only when V is set, and it leaves INTE alone");
    {
        // V set: push PC, jump to 0x0040. Unlike the hardware vectors, an RSTV
        // instruction does NOT disable interrupts (cf. the RST n instruction).
        Rig g;
        g.setReg("SP", 0x8000);
        g.setReg("PC", 0x0300);
        g.setReg("IE", 1);
        g.setReg("V", 1);
        g.load({0xCB}, 0x0300);  // RSTV, where PC points
        g.step();
        CHECK(g.reg("PC") == 0x0040, "RSTV with V set vectors to 0x0040");
        CHECK(g.reg("SP") == 0x7FFE && g.peek(0x7FFE) == 0x01 && g.peek(0x7FFF) == 0x03,
              "RSTV pushed the return address (PC past the opcode)");
        CHECK(g.reg("IE") == 1, "RSTV is an instruction -- it does not clear INTE");

        // V clear: it is a no-op that just falls through to the next instruction.
        Rig h;
        h.setReg("SP", 0x8000);
        h.setReg("PC", 0x0000);
        h.setReg("V", 0);
        h.load({0xCB, 0x00}, 0);  // RSTV ; NOP
        h.step();
        CHECK(h.reg("PC") == 0x0001 && h.reg("SP") == 0x8000,
              "RSTV with V clear is a one-byte no-op -- nothing pushed");
    }

    SECTION("JK / JNK -- the two jumps on the K (X5) bit");
    {
        // JK (0xFD) jumps when K is set. Seed K via a compare (1 < 2 sets K).
        Rig g;
        g.setReg("A", 0x01);
        g.load({0xFE, 0x02,          // CPI 02   -> K set (second value larger)
                0xFD, 0x00, 0x20});  // JK 2000
        g.run(2);
        CHECK(g.reg("PC") == 0x2000, "JK taken when K is set");

        // JK not taken when K is clear (2 > 1): PC falls past the 3-byte instruction.
        Rig h;
        h.setReg("A", 0x02);
        h.load({0xFE, 0x01,          // CPI 01   -> K clear
                0xFD, 0x00, 0x20});  // JK 2000  (not taken)
        h.run(2);
        CHECK(h.reg("PC") == 0x0005, "JK not taken skips the operand -- PC past the 3 bytes");

        // JNK (0xDD) is the mirror: taken when K is clear.
        Rig n;
        n.setReg("A", 0x02);
        n.load({0xFE, 0x01,          // CPI 01   -> K clear
                0xDD, 0x00, 0x20});  // JNK 2000
        n.run(2);
        CHECK(n.reg("PC") == 0x2000, "JNK taken when K is clear");

        Rig m;
        m.setReg("A", 0x01);
        m.load({0xFE, 0x02,          // CPI 02   -> K set
                0xDD, 0x00, 0x20});  // JNK 2000 (not taken)
        m.run(2);
        CHECK(m.reg("PC") == 0x0005, "JNK not taken skips the operand");
    }
}

// The five ALU-affecting undocumented opcodes (reference file 2). Operation and flag
// masks come from the Tundra data sheet + the 1979 Electronics article (they agree);
// flag VALUES follow section 1 / Shirriff. Vectors are hand-derived.
//
// These live in their own function rather than in test_8085_cpu(): that function had
// grown large enough (the V/K work added many sections) that appending these tipped
// MSVC over a per-function stack-frame/unwind threshold and SEGFAULTed the Windows
// unit run -- clang's smaller frame model hid it on macOS/Linux. Splitting keeps each
// function's frame modest. See the PR discussion for #347.
void test_8085_undoc_alu() {
    SECTION("DSUB -- HL = HL - BC, all seven flags (two chained 8-bit subtracts)");
    {
        // 0x0501 - 0x0102 = 0x03FF: no borrow, positive, non-zero.
        Rig g;
        g.setReg("HL", 0x0501); g.setReg("BC", 0x0102);
        g.load({0x08});  // DSUB
        g.run(1);
        CHECK(g.reg("HL") == 0x03FF && !g.flag("CY") && !g.flag("S") && !g.flag("Z"),
              "DSUB 0501-0102 = 03FF, no borrow");

        // 0x0000 - 0x0001 = 0xFFFF: borrow out of bit 15 (CY set), negative (S set),
        // V clear so K = V XOR sign = 1.
        Rig h;
        h.setReg("HL", 0x0000); h.setReg("BC", 0x0001);
        h.load({0x08});
        h.run(1);
        CHECK(h.reg("HL") == 0xFFFF && h.flag("CY") && h.flag("S") && !h.flag("Z")
                  && h.flag("K"),
              "DSUB 0000-0001 = FFFF: borrow, negative, K set");

        // Equal operands -> 0x0000: Z is the full 16-bit zero, no borrow.
        Rig n;
        n.setReg("HL", 0x1234); n.setReg("BC", 0x1234);
        n.load({0x08});
        n.run(1);
        CHECK(n.reg("HL") == 0x0000 && n.flag("Z") && !n.flag("CY") && !n.flag("S"),
              "DSUB 1234-1234 = 0000: Z set (full 16-bit zero)");
    }

    SECTION("ARHL -- arithmetic shift-right HL, CY only");
    {
        // 0x8004 >> 1, sign preserved: 0xC002; L bit 0 was 0 so CY clear.
        Rig g;
        g.setReg("HL", 0x8004);
        g.load({0x10});  // ARHL
        g.run(1);
        CHECK(g.reg("HL") == 0xC002 && !g.flag("CY"),
              "ARHL 8004 -> C002 (sign held), CY clear");

        // 0x0003 >> 1 = 0x0001; the dropped low bit (1) lands in CY.
        Rig h;
        h.setReg("HL", 0x0003);
        h.load({0x10});
        h.run(1);
        CHECK(h.reg("HL") == 0x0001 && h.flag("CY"), "ARHL 0003 -> 0001, CY = old L0");

        // CY ONLY: every other flag is untouched. Seed them and confirm they survive.
        Rig n;
        n.setReg("HL", 0x0002);  // L bit 0 = 0 -> CY will clear
        n.setReg("Z", 1); n.setReg("S", 1); n.setReg("V", 1); n.setReg("K", 1);
        n.setReg("AC", 1); n.setReg("P", 1);
        n.load({0x10});
        n.run(1);
        CHECK(!n.flag("CY") && n.flag("Z") && n.flag("S") && n.flag("V")
                  && n.flag("K") && n.flag("AC") && n.flag("P"),
              "ARHL touches CY only -- S/Z/P/AC/V/K survive");
    }

    SECTION("RDEL -- rotate DE left through carry, CY and V only");
    {
        // 0x8000, CY in 0: bit 15 -> CY (set), result 0x0000; V = bit14 XOR bit15 = 1.
        Rig g;
        g.setReg("DE", 0x8000); g.setReg("CY", 0);
        g.load({0x18});  // RDEL
        g.run(1);
        CHECK(g.reg("DE") == 0x0000 && g.flag("CY") && g.flag("V"),
              "RDEL 8000 (CY=0) -> 0000, CY and V set");

        // 0x4000, CY in 0: bit 15 = 0 (CY clear), V = 1 XOR 0 = 1, result 0x8000.
        Rig h;
        h.setReg("DE", 0x4000); h.setReg("CY", 0);
        h.load({0x18});
        h.run(1);
        CHECK(h.reg("DE") == 0x8000 && !h.flag("CY") && h.flag("V"),
              "RDEL 4000 (CY=0) -> 8000, CY clear, V set");

        // 0x0001, CY in 1: old CY into bit 0 -> 0x0003; bit 15 = 0 (CY clear); V clear.
        // Also confirm CY and V are the ONLY flags that move (seed Z/S/K and check).
        Rig n;
        n.setReg("DE", 0x0001); n.setReg("CY", 1);
        n.setReg("Z", 1); n.setReg("S", 1); n.setReg("K", 1);
        n.load({0x18});
        n.run(1);
        CHECK(n.reg("DE") == 0x0003 && !n.flag("CY") && !n.flag("V")
                  && n.flag("Z") && n.flag("S") && n.flag("K"),
              "RDEL 0001 (CY=1) -> 0003; CY/V only -- Z/S/K survive");
    }

    SECTION("LDHI d8 -- DE = HL + imm8, no flags");
    {
        Rig g;
        g.setReg("HL", 0x1234);
        g.load({0x28, 0x11});  // LDHI 11
        g.run(1);
        CHECK(g.reg("DE") == 0x1245, "LDHI: DE = HL + imm8");

        // Carry across the byte boundary is a plain 16-bit add; still no flags.
        Rig h;
        h.setReg("HL", 0x00F0);
        h.setReg("CY", 1); h.setReg("Z", 1); h.setReg("V", 1); h.setReg("K", 1);
        h.load({0x28, 0x20});  // LDHI 20
        h.run(1);
        CHECK(h.reg("DE") == 0x0110 && h.flag("CY") && h.flag("Z") && h.flag("V")
                  && h.flag("K"),
              "LDHI 00F0+20 = 0110, and every flag survives (no flags)");
    }

    SECTION("LDSI d8 -- DE = SP + imm8, no flags");
    {
        Rig g;
        g.setReg("SP", 0x2000);
        g.load({0x38, 0x10});  // LDSI 10
        g.run(1);
        CHECK(g.reg("DE") == 0x2010, "LDSI: DE = SP + imm8");

        // 16-bit wrap, and no flags disturbed.
        Rig h;
        h.setReg("SP", 0xFFFF);
        h.setReg("S", 1); h.setReg("AC", 1); h.setReg("P", 1);
        h.load({0x38, 0x02});  // LDSI 02
        h.run(1);
        CHECK(h.reg("DE") == 0x0001 && h.flag("S") && h.flag("AC") && h.flag("P"),
              "LDSI FFFF+02 = 0001 (wraps), flags survive");
    }
}
