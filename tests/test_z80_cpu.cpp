#include "test.h"

#include "boards/s100-memory.h"
#include "core/machine.h"
#include "cpu/cpuZ80.h"

using namespace altair;

// PHASE 2 IS NOT THE GATE -- ZEXALL/ZEXDOC are (Phase 4). These are targeted
// CHECKs on the things a suite is least fun to bisect from: the alternate bank,
// the block moves, IM 2 vectoring, and a handful of flag corners including the
// undocumented F5/F3 that are the whole reason for the ZEXALL bar. The core is
// driven directly against a Bus -- there is no z80 CARD yet (that is Phase 3).

namespace {

// A source of a maskable interrupt that also drives the vector byte onto the
// IntAck cycle, exactly as a real vectored-interrupt card does (DESIGN.md 4.4).
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
    CpuZ80 cpu;
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

void test_z80_cpu() {
    SECTION("the Z80 core -- reflection, and what the monitor sees");

    {
        Rig g;
        CHECK(std::string(g.cpu.isa()) == "z80", "it speaks z80");
        CHECK(g.reg("IX") == 0 && g.reg("IY") == 0, "IX and IY exist and start clear");
        CHECK(g.reg("WZ") != 0xEEEEEEEE, "MEMPTR/WZ is reachable by name");
        g.setReg("HL", 0x1234);
        CHECK(g.reg("H") == 0x12 && g.reg("L") == 0x34, "HL is the H,L pair, high byte first");
    }

    SECTION("the alternate bank -- EXX swaps BC/DE/HL, EX AF,AF' swaps AF");
    {
        Rig g;
        g.setReg("BC", 0x1111); g.setReg("DE", 0x2222); g.setReg("HL", 0x3333);
        g.setReg("BC'", 0xAAAA); g.setReg("DE'", 0xBBBB); g.setReg("HL'", 0xCCCC);
        g.load({0xD9});  // EXX
        g.run(1);
        CHECK(g.reg("BC") == 0xAAAA && g.reg("HL") == 0xCCCC, "EXX brought the shadow set forward");
        CHECK(g.reg("BC'") == 0x1111, "and put the main set away");

        Rig h;
        h.setReg("A", 0x55); h.setReg("F", 0x00);
        h.setReg("AF'", 0x99FF);
        h.load({0x08});  // EX AF,AF'
        h.run(1);
        CHECK(h.reg("A") == 0x99 && h.reg("F") == 0xFF, "EX AF,AF' swapped the accumulator and flags");
    }

    SECTION("block move -- LDIR copies BC bytes and leaves BC zero, P/V clear");
    {
        Rig g;
        g.load({0x11, 0x00, 0x90,   // LD DE,9000
                0x21, 0x00, 0x80,   // LD HL,8000
                0x01, 0x04, 0x00,   // LD BC,0004
                0xED, 0xB0});       // LDIR
        for (int i = 0; i < 4; ++i) g.poke((uint16_t)(0x8000 + i), (uint8_t)(0xA0 + i));
        g.run(3);            // the three loads
        g.run(4);            // LDIR re-executes until BC == 0
        CHECK(g.peek(0x9000) == 0xA0 && g.peek(0x9003) == 0xA3, "all four bytes moved");
        CHECK(g.reg("BC") == 0 && g.reg("HL") == 0x8004 && g.reg("DE") == 0x9004,
              "pointers advanced and BC counted down to zero");
        CHECK(!g.flag("PV"), "P/V is clear when LDIR finishes -- it means BC != 0");
    }

    SECTION("16-bit arithmetic -- SBC HL,DE borrows and sets N; ADD HL,BC clears it");
    {
        Rig g;
        g.setReg("HL", 0x0000); g.setReg("DE", 0x0001);
        g.setReg("F", 0x00);
        g.load({0xED, 0x52});  // SBC HL,DE  (carry clear)
        g.run(1);
        CHECK(g.reg("HL") == 0xFFFF, "0000 - 0001 = FFFF");
        CHECK(g.flag("CY") && g.flag("N") && g.flag("S"), "borrow sets C, SBC sets N, result is negative");

        Rig h;
        h.setReg("HL", 0x4000); h.setReg("BC", 0x1234);
        h.load({0x09});  // ADD HL,BC
        h.run(1);
        CHECK(h.reg("HL") == 0x5234 && !h.flag("N") && !h.flag("CY"), "ADD HL,BC: sum, N and C clear");
    }

    SECTION("flag corners -- overflow, DAA with N, INC at 7F");
    {
        Rig g;  // 7F + 01 = 80: signed overflow, half carry, no carry
        g.setReg("A", 0x7F);
        g.load({0xC6, 0x01});  // ADD A,01
        g.run(1);
        CHECK(g.reg("A") == 0x80, "7F + 1 = 80");
        CHECK(g.flag("PV") && g.flag("HF") && g.flag("S") && !g.flag("CY"),
              "positive+positive->negative is overflow, and bit-3 carry sets H");

        Rig h;  // DAA after a subtract must subtract, because N is set
        h.setReg("A", 0x00);
        h.load({0xD6, 0x01,   // SUB 01  -> A=FF, N set, H set, C set
                0x27});       // DAA
        h.run(2);
        CHECK(h.reg("A") == 0x99, "FF adjusted downward is 99 -- DAA read the N flag");
        CHECK(h.flag("N") && h.flag("CY"), "N stays set and the borrow is preserved");

        Rig k;
        k.setReg("B", 0x7F);
        k.load({0x04});  // INC B
        k.run(1);
        CHECK(k.reg("B") == 0x80 && k.flag("PV") && k.flag("HF") && !k.flag("N"),
              "INC 7F overflows to 80 (P/V), half-carries, and clears N");
    }

    SECTION("undocumented -- SLL shifts a 1 in, and F5/F3 copy the result bits");
    {
        Rig g;
        g.setReg("B", 0x80);
        g.load({0xCB, 0x30});  // SLL B
        g.run(1);
        CHECK(g.reg("B") == 0x01, "SLL 80 = 01 -- a 1 shifted into bit 0, 80 out to carry");
        CHECK(g.flag("CY"), "the top bit went to carry");

        Rig h;  // A becomes 28: F5 (bit5) set, F3 (bit3) set
        h.setReg("A", 0x00);
        h.load({0xC6, 0x28});  // ADD A,28
        h.run(1);
        uint8_t f = (uint8_t)h.reg("F");
        CHECK((f & 0x28) == 0x28, "F5 and F3 are copies of result bits 5 and 3 (both set in 0x28)");
    }

    SECTION("BIT n,(HL) takes F5/F3 from MEMPTR, not from the byte read");
    {
        Rig g;
        // Put MEMPTR in a known state via LD A,(nn): WZ becomes nn+1 = 6628.
        g.load({0x3A, 0x27, 0x66,   // LD A,(6627) -> WZ = 6628
                0x21, 0x00, 0x70,   // LD HL,7000
                0xCB, 0x46});       // BIT 0,(HL)
        g.poke(0x7000, 0x00);
        g.run(3);
        uint8_t f = (uint8_t)g.reg("F");
        // WZ high byte is 0x66 -> bit5 set (F5), bit3 clear (F3).
        CHECK((f & 0x20) && !(f & 0x08), "F5/F3 mirror MEMPTR's high byte (0x66), not the (HL) value");
        CHECK(g.flag("Z") && g.flag("HF"), "bit 0 of 00 is clear, so Z set; BIT always sets H");
    }

    SECTION("IX+d -- the displacement is signed and reaches the right byte");
    {
        Rig g;
        g.load({0xDD, 0x21, 0x00, 0x40,   // LD IX,4000
                0xDD, 0x7E, 0x05,         // LD A,(IX+5)
                0xDD, 0x77, 0xFB});       // LD (IX-5),A
        g.poke(0x4005, 0x5A);
        g.run(2);
        CHECK(g.reg("A") == 0x5A, "LD A,(IX+5) read 4005");
        g.run(1);
        CHECK(g.peek(0x3FFB) == 0x5A, "LD (IX-5),A wrote 3FFB -- a negative displacement");
    }

    SECTION("DD CB -- rotate through (IX+d), and the undocumented reg-copy form");
    {
        Rig g;
        g.setReg("IX", 0x5000);
        g.poke(0x5002, 0x01);
        g.load({0xDD, 0xCB, 0x02, 0x00});  // RLC (IX+2),B  -- rotate memory AND copy into B
        g.run(1);
        CHECK(g.peek(0x5002) == 0x02, "RLC rotated the byte at IX+2");
        CHECK(g.reg("B") == 0x02, "and the undocumented low-3-bits form copied the result into B");
    }

    SECTION("IM 2 -- the vector is (I<<8)|bus byte, and the handler address is read from it");
    {
        Rig g;
        // Handler at 8000; vector table entry at I:vec -> points to 8000.
        g.setReg("I", 0x30);
        g.setReg("IM", 2);
        g.poke(0x3040, 0x00);     // low byte of handler address
        g.poke(0x3041, 0x80);     // high byte -> 8000
        g.setReg("SP", 0xFF00);
        g.load({0xFB, 0x00}, 0x0100);  // EI ; NOP   (EI has a one-instruction shadow)
        g.cpu.setPc(0x0100);
        g.irq.raise(0x40);        // device drives vector byte 0x40 on the ack
        g.step();                 // EI -- interrupts not yet live
        StepResult r = g.step();  // NOP runs; interrupt accepted at the FOLLOWING boundary? no -- see below
        // After EI, IFF1 turns on at the end of the NOP; the interrupt is taken at the
        // next boundary. Step once more to reach it.
        g.irq.raise(0x40);
        r = g.step();
        CHECK(g.cpu.pc() == 0x8000, "PC vectored through 3040 to the handler at 8000");
        CHECK(!g.flag("IFF1"), "accepting the interrupt cleared IFF1");
        CHECK(g.peek(0xFEFF) == 0x01, "the return address (0102) was pushed, high byte at SP-1");
        (void)r;
    }

    SECTION("IM 1 -- a maskable interrupt is an RST 38");
    {
        Rig g;
        g.setReg("IM", 1);
        g.setReg("SP", 0xFF00);
        g.load({0xFB, 0x00, 0x00}, 0x0200);  // EI ; NOP ; NOP
        g.cpu.setPc(0x0200);
        g.step();                 // EI
        g.irq.raise(0x00);        // vector byte irrelevant in IM 1
        g.step();                 // NOP -- IFF1 becomes live at its end
        StepResult r = g.step();  // interrupt taken here
        CHECK(g.cpu.pc() == 0x0038, "IM 1 jams RST 38");
        (void)r;
    }
}
