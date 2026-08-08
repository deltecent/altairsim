#include "test.h"

#include "boards/s100-memory.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "cpu/cpu6800.h"

using namespace altair;

namespace {

// A bare 6800 over 64K of RAM. The CPU board does not exist yet (that is the next
// step), so the core is constructed BY HAND and driven directly -- these tests are
// about the CHIP, and nothing about a card or a .toml should be able to redden
// them. The machine is here only for its bus and its memory board.
struct Rig {
    Machine m;
    MemoryBoard* mem = nullptr;
    Cpu6800 cpu;

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
    }

    void load(std::initializer_list<uint8_t> code, uint16_t at) {
        uint16_t a = at;
        for (uint8_t byte : code) m.bus.memWrite(a++, byte);
    }
    void poke(uint16_t a, uint8_t v) { m.bus.memWrite(a, v); }
    uint8_t peek(uint16_t a) { return m.bus.peek(a); }

    void step(int n = 1) {
        for (int i = 0; i < n; ++i) cpu.step(m.bus);
    }

    uint32_t reg(const char* name) {
        for (const RegDef& r : cpu.registers())
            if (r.name == name) return r.get();
        return 0xEEEEEEEE;
    }
    void setReg(const char* name, uint32_t v) {
        for (const RegDef& r : cpu.registers())
            if (r.name == name) r.set(v);
    }
    bool fl(const char* name) { return reg(name) != 0; }

    // Assemble a tiny program at `at`, point PC there, run it. Reset is deliberately
    // NOT called so the reset-vector latch stays disarmed and PC is exactly `at`.
    void execAt(uint16_t at, std::initializer_list<uint8_t> code, int n) {
        load(code, at);
        cpu.setPc(at);
        step(n);
    }
};

} // namespace

void test_cpu6800() {
    SECTION("the 6800 -- reflection, reset and the deferred restart vector");
    {
        Rig g;
        CHECK(std::string(g.cpu.isa()) == "6800", "the core says which instruction set it speaks");

        // The CCR reflects as six lamps in hardware order H I N Z V C, then A B X SP PC.
        bool sawH = false, sawC = false, sawA = false, sawPC = false;
        for (const RegDef& r : g.cpu.registers()) {
            if (r.name == "H") sawH = (r.show == RegShow::Flag && r.bits == 1);
            if (r.name == "C") sawC = (r.show == RegShow::Flag && r.bits == 1);
            if (r.name == "A") sawA = (r.bits == 8);
            if (r.name == "PC") sawPC = (r.bits == 16);
        }
        CHECK(sawH && sawC, "H and C are one-bit lamps");
        CHECK(sawA && sawPC, "A is 8-bit, PC is 16-bit");

        // Reset arms the I mask and the vector fetch, but touches no memory and no
        // registers -- the FFFE/FFFF read happens on the first step().
        g.setReg("A", 0x5A);
        g.poke(0xFFFE, 0x12);
        g.poke(0xFFFF, 0x34);
        g.cpu.reset(Reset::PowerOn);
        CHECK(g.fl("I"), "reset sets the interrupt mask");
        CHECK(g.reg("A") == 0x5A, "and leaves the registers alone");
        CHECK(g.cpu.pc() == 0x0000, "PC is not yet the vector -- the fetch is deferred");

        g.load({0x01}, 0x1234);  // NOP at the vector target
        g.step();
        CHECK(g.cpu.pc() == 0x1235, "first step reads FFFE/FFFF -> 1234, then runs the NOP there");
    }

    SECTION("loads, stores and the big-endian 16-bit word");
    {
        Rig g;
        // LDX #FC00 stores CE FC 00 (MS byte first); STX to a direct address writes
        // the high byte at the LOWER address. Read it back low-first and you get the
        // classic transposition bug.
        g.execAt(0x0100, {0xCE, 0xFC, 0x00, 0xDF, 0x50}, 2);  // LDX #FC00 ; STX 50
        CHECK(g.reg("X") == 0xFC00, "LDX # took the word high byte first");
        CHECK(g.peek(0x0050) == 0xFC && g.peek(0x0051) == 0x00, "STX wrote hi at the lower address");

        g.execAt(0x0100, {0xCE, 0x00, 0x00, 0xDE, 0x50}, 2);  // LDX #0 ; LDX 50 (direct)
        CHECK(g.reg("X") == 0xFC00, "LDX direct read the word back big-endian");

        // Indexed load: A6 05 with X=0200 reads 0205.
        g.poke(0x0205, 0x77);
        g.execAt(0x0100, {0xCE, 0x02, 0x00, 0xA6, 0x05}, 2);  // LDX #0200 ; LDAA 05,X
        CHECK(g.reg("A") == 0x77, "LDAA offset,X added the offset to X");
        CHECK(!g.fl("V"), "a load clears V");

        // STAA extended and LDAA extended round-trip.
        g.execAt(0x0100, {0x86, 0xAB, 0xB7, 0x08, 0x00}, 2);  // LDAA #AB ; STAA 0800
        CHECK(g.peek(0x0800) == 0xAB, "STAA extended reached the 16-bit address");
    }

    SECTION("ADD -- half-carry and overflow, the two flags 8080 habit gets wrong");
    {
        Rig g;
        g.execAt(0x0100, {0x86, 0x0F, 0x8B, 0x01}, 2);  // LDAA #0F ; ADDA #01
        CHECK(g.reg("A") == 0x10, "0F + 01 = 10");
        CHECK(g.fl("H"), "a carry out of bit 3 sets H");

        g.execAt(0x0100, {0x86, 0x7F, 0x8B, 0x01}, 2);  // LDAA #7F ; ADDA #01
        CHECK(g.reg("A") == 0x80, "7F + 01 = 80");
        CHECK(g.fl("V") && g.fl("N"), "two positives making a negative sets V, and N");
        CHECK(!g.fl("C"), "no unsigned carry, so C is clear");

        g.execAt(0x0100, {0x86, 0xFF, 0x8B, 0x01}, 2);  // LDAA #FF ; ADDA #01
        CHECK(g.reg("A") == 0x00 && g.fl("Z"), "FF + 01 wraps to 00");
        CHECK(g.fl("C") && g.fl("H"), "and carries out of both bit 7 and bit 3");

        // ABA folds B into A with the same rules.
        g.setReg("A", 0x14);
        g.setReg("B", 0x28);
        g.execAt(0x0100, {0x1B}, 1);  // ABA
        CHECK(g.reg("A") == 0x3C, "ABA: 14 + 28 = 3C");
    }

    SECTION("SUB/CMP -- C is a borrow, V is signed, and H is NOT touched");
    {
        Rig g;
        // Set H high first, then subtract, and prove SUB left H alone.
        g.execAt(0x0100, {0x86, 0x20, 0x06, 0x86, 0x50, 0x80, 0x01}, 4);  // LDAA #20;TAP;LDAA #50;SUBA #01
        CHECK(g.reg("A") == 0x4F, "50 - 01 = 4F");
        CHECK(g.fl("H"), "TAP set H, and SUB does not affect it");

        g.execAt(0x0100, {0x86, 0x00, 0x80, 0x01}, 2);  // LDAA #00 ; SUBA #01
        CHECK(g.reg("A") == 0xFF && g.fl("C"), "00 - 01 borrows: FF, C set");
        CHECK(g.fl("N") && !g.fl("V"), "N set, no signed overflow");

        g.execAt(0x0100, {0x86, 0x80, 0x80, 0x01}, 2);  // LDAA #80 ; SUBA #01
        CHECK(g.reg("A") == 0x7F && g.fl("V"), "80 - 01 = 7F overflows the sign");
        CHECK(!g.fl("C"), "and does not borrow");

        // CMP sets flags but writes nothing back.
        g.setReg("B", 0x05);
        g.execAt(0x0100, {0xC1, 0x05}, 1);  // CMPB #05
        CHECK(g.fl("Z") && g.reg("B") == 0x05, "CMPB #05 with B=05: Z set, B unchanged");
    }

    SECTION("NEG / COM / INC / DEC / CLR / TST");
    {
        Rig g;
        g.execAt(0x0100, {0x86, 0x01, 0x40}, 2);  // LDAA #01 ; NEGA
        CHECK(g.reg("A") == 0xFF && g.fl("C") && !g.fl("V"), "NEG 01 = FF, C set (borrow), no V");

        g.execAt(0x0100, {0x4F, 0x40}, 2);  // CLRA ; NEGA
        CHECK(g.reg("A") == 0x00 && !g.fl("C") && g.fl("Z"), "NEG 00 = 00, C clear");

        g.execAt(0x0100, {0x86, 0x80, 0x40}, 2);  // LDAA #80 ; NEGA
        CHECK(g.reg("A") == 0x80 && g.fl("V") && g.fl("C"), "NEG 80 = 80, its own negative: V set");

        g.execAt(0x0100, {0x86, 0x00, 0x43}, 2);  // LDAA #00 ; COMA
        CHECK(g.reg("A") == 0xFF && g.fl("C") && !g.fl("V"), "COM ones-complements and always sets C");

        g.execAt(0x0100, {0x86, 0x7F, 0x4C}, 2);  // LDAA #7F ; INCA
        CHECK(g.reg("A") == 0x80 && g.fl("V"), "INC 7F -> 80 sets the overflow");

        g.execAt(0x0100, {0x86, 0x80, 0x4A}, 2);  // LDAA #80 ; DECA
        CHECK(g.reg("A") == 0x7F && g.fl("V"), "DEC 80 -> 7F sets the overflow");

        g.execAt(0x0100, {0x86, 0xFF, 0x4F}, 2);  // LDAA #FF ; CLRA
        CHECK(g.reg("A") == 0x00 && g.fl("Z") && !g.fl("N") && !g.fl("V") && !g.fl("C"), "CLR zeroes A and its flags");

        g.execAt(0x0100, {0x86, 0x80, 0x4D}, 2);  // LDAA #80 ; TSTA
        CHECK(g.reg("A") == 0x80 && g.fl("N") && !g.fl("C"), "TST sets N/Z from the value, clears C, writes nothing");
    }

    SECTION("shifts and rotates -- C takes the shifted-out bit, V = N ^ C");
    {
        Rig g;
        g.execAt(0x0100, {0x86, 0x40, 0x48}, 2);  // LDAA #40 ; ASLA
        CHECK(g.reg("A") == 0x80 && !g.fl("C") && g.fl("N") && g.fl("V"), "ASL 40 -> 80: C=0, N=1, V=N^C=1");

        g.execAt(0x0100, {0x86, 0x80, 0x48}, 2);  // LDAA #80 ; ASLA
        CHECK(g.reg("A") == 0x00 && g.fl("C") && g.fl("Z") && g.fl("V"), "ASL 80 -> 00: C=1, Z=1, V=N^C=1");

        g.execAt(0x0100, {0x86, 0x01, 0x44}, 2);  // LDAA #01 ; LSRA
        CHECK(g.reg("A") == 0x00 && g.fl("C") && !g.fl("N") && g.fl("V"), "LSR 01 -> 00: C=1, N always 0, V=1");

        g.execAt(0x0100, {0x86, 0x81, 0x47}, 2);  // LDAA #81 ; ASRA
        CHECK(g.reg("A") == 0xC0 && g.fl("C") && g.fl("N") && !g.fl("V"), "ASR 81 -> C0: sign kept, C=1, V=N^C=0");

        g.execAt(0x0100, {0x0D, 0x86, 0x00, 0x46}, 3);  // SEC ; LDAA #00 ; RORA
        CHECK(g.reg("A") == 0x80 && !g.fl("C") && g.fl("N"), "ROR rotates carry into bit 7");

        g.execAt(0x0100, {0x0D, 0x86, 0x00, 0x49}, 3);  // SEC ; LDAA #00 ; ROLA
        CHECK(g.reg("A") == 0x01 && !g.fl("C"), "ROL rotates carry into bit 0");
    }

    SECTION("branches -- every condition, plus the forward/back target math");
    {
        Rig g;
        // BEQ forward: taken lands at (PC+2)+offset; not-taken falls through.
        g.setReg("Z", 1);
        g.execAt(0x0100, {0x27, 0x7E}, 1);  // BEQ +7E
        CHECK(g.cpu.pc() == 0x0180, "BEQ taken: 0100 + 2 + 7E = 0180");
        g.setReg("Z", 0);
        g.execAt(0x0100, {0x27, 0x7E}, 1);
        CHECK(g.cpu.pc() == 0x0102, "BEQ not taken: falls through to 0102");

        // BRA backward with FE (-2) is the classic self-loop.
        g.execAt(0x0100, {0x20, 0xFE}, 1);  // BRA -2
        CHECK(g.cpu.pc() == 0x0100, "BRA FE branches to itself");

        // Signed vs unsigned condition pairs.
        g.setReg("C", 0); g.setReg("Z", 0);
        g.execAt(0x0100, {0x22, 0x10}, 1);  // BHI
        CHECK(g.cpu.pc() == 0x0112, "BHI taken when C=0 and Z=0");
        g.setReg("C", 1);
        g.execAt(0x0100, {0x22, 0x10}, 1);
        CHECK(g.cpu.pc() == 0x0102, "BHI not taken when C=1");

        g.setReg("N", 1); g.setReg("V", 0);  // N^V=1 -> less-than
        g.execAt(0x0100, {0x2C, 0x10}, 1);  // BGE
        CHECK(g.cpu.pc() == 0x0102, "BGE not taken when N^V=1");
        g.execAt(0x0100, {0x2D, 0x10}, 1);  // BLT
        CHECK(g.cpu.pc() == 0x0112, "BLT taken when N^V=1");
    }

    SECTION("index and stack -- INX/DEX touch only Z; TSX/TXS; PSH/PUL");
    {
        Rig g;
        // Load X, THEN raise every flag, then INX and prove only Z moved.
        g.execAt(0x0100, {0xCE, 0x00, 0x00, 0x86, 0xFF, 0x06, 0x08}, 4);
        // LDX #0 ; LDAA #FF ; TAP (all flags 1) ; INX
        CHECK(g.reg("X") == 0x0001, "INX advanced X");
        CHECK(!g.fl("Z"), "Z cleared because X != 0");
        CHECK(g.fl("N") && g.fl("V") && g.fl("C"), "but N, V and C are untouched by INX");

        g.execAt(0x0100, {0xCE, 0x00, 0x01, 0x09}, 2);  // LDX #0001 ; DEX
        CHECK(g.reg("X") == 0x0000 && g.fl("Z"), "DEX to zero sets Z");

        // TSX is SP+1 -> X ; TXS is X-1 -> SP.
        g.setReg("SP", 0x00FE);
        g.execAt(0x0100, {0x30}, 1);  // TSX
        CHECK(g.reg("X") == 0x00FF, "TSX: X = SP + 1");
        g.setReg("X", 0x0200);
        g.execAt(0x0100, {0x35}, 1);  // TXS
        CHECK(g.reg("SP") == 0x01FF, "TXS: SP = X - 1");

        // PSHA then PULA round-trips A through the stack and restores SP.
        g.setReg("SP", 0x00FF);
        g.setReg("A", 0x42);
        g.execAt(0x0100, {0x36, 0x86, 0x00, 0x32}, 3);  // PSHA ; LDAA #00 ; PULA
        CHECK(g.reg("A") == 0x42 && g.reg("SP") == 0x00FF, "PSHA/PULA restore A and the stack pointer");
        CHECK(g.peek(0x00FF) == 0x42, "and the byte really went to the stack");
    }

    SECTION("subroutines -- BSR/JSR push the return address, RTS pops it");
    {
        Rig g;
        g.setReg("SP", 0x00FF);
        g.load({0x39}, 0x0200);                       // RTS at 0200
        g.execAt(0x0100, {0xBD, 0x02, 0x00}, 1);      // JSR 0200 (extended)
        CHECK(g.cpu.pc() == 0x0200, "JSR jumped to the routine");
        CHECK(g.reg("SP") == 0x00FD, "and pushed a two-byte return");
        CHECK(g.peek(0x00FE) == 0x01 && g.peek(0x00FF) == 0x03, "return 0103 stacked hi at the lower address");
        g.step();  // RTS
        CHECK(g.cpu.pc() == 0x0103 && g.reg("SP") == 0x00FF, "RTS returned past the JSR and unwound SP");

        // BSR uses a relative target.
        g.setReg("SP", 0x00FF);
        g.execAt(0x0100, {0x8D, 0x7E}, 1);  // BSR +7E
        CHECK(g.cpu.pc() == 0x0180 && g.reg("SP") == 0x00FD, "BSR: 0100 + 2 + 7E = 0180, return pushed");

        // JMP extended is a plain transfer.
        g.execAt(0x0100, {0x7E, 0x04, 0x00}, 1);  // JMP 0400
        CHECK(g.cpu.pc() == 0x0400, "JMP extended set PC");
    }

    SECTION("interrupts -- SWI/RTI frame, IRQ masking, NMI, and WAI");
    {
        Rig g;
        // SWI stacks the full frame, masks interrupts, vectors from FFFA/FFFB; RTI
        // restores every byte of it.
        g.setReg("SP", 0x00FF);
        g.setReg("A", 0x11);
        g.setReg("B", 0x22);
        g.setReg("X", 0x3344);
        g.setReg("I", 0);
        g.poke(0xFFFA, 0x03);
        g.poke(0xFFFB, 0x00);
        g.load({0x3B}, 0x0300);                 // RTI at the handler
        g.execAt(0x0100, {0x3F}, 1);            // SWI
        CHECK(g.cpu.pc() == 0x0300, "SWI vectored through FFFA/FFFB");
        CHECK(g.fl("I"), "SWI masked interrupts");
        CHECK(g.reg("SP") == 0x00F8, "and pushed the seven-byte frame");
        g.step();  // RTI
        CHECK(g.cpu.pc() == 0x0101, "RTI returned to just after the SWI");
        CHECK(g.reg("A") == 0x11 && g.reg("B") == 0x22 && g.reg("X") == 0x3344, "and restored A, B and X");
        CHECK(!g.fl("I") && g.reg("SP") == 0x00FF, "and the pre-SWI I mask and stack pointer");

        // IRQ is the bus wire, gated by I. Masked: it runs the normal instruction.
        g.setReg("SP", 0x00FF);
        g.setReg("I", 1);
        g.poke(0xFFF8, 0x04);
        g.poke(0xFFF9, 0x00);
        g.m.bus.intWireChanged(true);           // a board pulls pin 73
        g.execAt(0x0100, {0x01}, 1);            // NOP -- should just run, I is set
        CHECK(g.cpu.pc() == 0x0101, "IRQ masked: the NOP ran and no vector was taken");
        // Unmasked: the next boundary vectors through FFF8/FFF9.
        g.setReg("I", 0);
        g.cpu.setPc(0x0100);
        g.step();
        CHECK(g.cpu.pc() == 0x0400 && g.fl("I"), "IRQ unmasked: vectored through FFF8 and set the mask");
        CHECK(g.reg("SP") == 0x00F8, "stacking the frame");
        g.m.bus.intWireChanged(false);

        // NMI ignores the mask entirely.
        g.setReg("SP", 0x00FF);
        g.setReg("I", 1);
        g.poke(0xFFFC, 0x05);
        g.poke(0xFFFD, 0x00);
        g.cpu.setPc(0x0100);
        g.cpu.signalNmi();
        g.step();
        CHECK(g.cpu.pc() == 0x0500, "NMI vectored through FFFC/FFFD despite I being set");

        // WAI stacks in advance and parks until an interrupt, then takes it cheaply.
        Rig w;
        w.setReg("SP", 0x00FF);
        w.setReg("I", 0);
        w.poke(0xFFF8, 0x04);
        w.poke(0xFFF9, 0x00);
        w.load({0x3E}, 0x0100);  // WAI
        w.cpu.setPc(0x0100);
        w.step();
        CHECK(w.cpu.halted() && w.reg("SP") == 0x00F8, "WAI parked with the frame already stacked");
        CHECK(w.cpu.pc() == 0x0101, "PC sits just past the WAI");
        w.step();  // still nothing pending
        CHECK(w.cpu.halted() && w.reg("SP") == 0x00F8, "and stays parked, not re-stacking");
        w.m.bus.intWireChanged(true);
        w.step();
        CHECK(w.cpu.pc() == 0x0400 && !w.cpu.halted(), "the IRQ woke it and vectored");
        CHECK(w.reg("SP") == 0x00F8, "reusing the already-stacked frame -- SP did not drop again");
        w.m.bus.intWireChanged(false);
    }

    SECTION("DAA -- the decimal adjust table");
    {
        Rig g;
        g.execAt(0x0100, {0x86, 0x09, 0x8B, 0x01, 0x19}, 3);  // LDAA #09;ADDA #01;DAA
        CHECK(g.reg("A") == 0x10 && !g.fl("C"), "BCD 09 + 01 = 10");

        g.execAt(0x0100, {0x86, 0x15, 0x8B, 0x15, 0x19}, 3);  // 15 + 15
        CHECK(g.reg("A") == 0x30, "BCD 15 + 15 = 30");

        g.execAt(0x0100, {0x86, 0x18, 0x8B, 0x08, 0x19}, 3);  // 18 + 08, half-carry
        CHECK(g.reg("A") == 0x26, "BCD 18 + 08 = 26 (half-carry path)");

        g.execAt(0x0100, {0x86, 0x99, 0x8B, 0x01, 0x19}, 3);  // 99 + 01 -> carry
        CHECK(g.reg("A") == 0x00 && g.fl("C") && g.fl("Z"), "BCD 99 + 01 = 00 with carry out");
    }

    SECTION("CPX -- sets N/Z/V from the 16-bit compare but LEAVES CARRY ALONE");
    {
        Rig g;
        g.execAt(0x0100, {0x0D, 0xCE, 0x12, 0x34, 0x8C, 0x12, 0x34}, 3);  // SEC;LDX #1234;CPX #1234
        CHECK(g.fl("Z") && !g.fl("N") && !g.fl("V"), "equal: Z set");
        CHECK(g.fl("C"), "and C is untouched -- still set from SEC");

        g.execAt(0x0100, {0xCE, 0x12, 0x34, 0x8C, 0x12, 0x35}, 2);  // LDX #1234;CPX #1235
        CHECK(!g.fl("Z") && g.fl("N"), "1234 - 1235 is negative: N set, Z clear");
    }

    SECTION("TAP / TPA round-trip the CCR through A");
    {
        Rig g;
        g.execAt(0x0100, {0x86, 0x2A, 0x06, 0x07}, 3);  // LDAA #2A ; TAP ; TPA
        CHECK(g.fl("H") && !g.fl("I") && g.fl("N") && !g.fl("Z") && g.fl("V") && !g.fl("C"),
              "TAP set the flags from the bits of 2A");
        CHECK(g.reg("A") == 0xEA, "TPA read them back with the top two bits forced to 1");
    }

    SECTION("an undefined opcode is inert -- one byte, no state change");
    {
        Rig g;
        g.setReg("A", 0x55);
        g.execAt(0x0100, {0x00}, 1);  // 00 is undefined on the 6800
        CHECK(g.cpu.pc() == 0x0101, "PC advanced by exactly one byte");
        CHECK(g.reg("A") == 0x55, "and nothing else changed");
    }

    SECTION("serialize / deserialize is a byte-exact round-trip");
    {
        Rig g;
        g.setReg("A", 0x12);
        g.setReg("B", 0x34);
        g.setReg("X", 0x5678);
        g.setReg("SP", 0x9ABC);
        g.setReg("CC", 0xEF);  // all six flags on top of the two constant bits
        g.cpu.setPc(0xDEF0);

        StateWriter w;
        g.cpu.serialize(w);

        Cpu6800 clone;
        StateReader r(w.data());
        clone.deserialize(r);
        CHECK(r.ok(), "the reader consumed the frame without underrunning");

        auto get = [](Cpu6800& c, const char* name) -> uint32_t {
            for (const RegDef& rr : c.registers())
                if (rr.name == name) return rr.get();
            return 0xEEEEEEEE;
        };
        CHECK(get(clone, "A") == 0x12 && get(clone, "B") == 0x34, "A and B survived");
        CHECK(get(clone, "X") == 0x5678 && get(clone, "SP") == 0x9ABC, "X and SP survived");
        CHECK(clone.pc() == 0xDEF0, "PC survived");
        CHECK(get(clone, "CC") == 0xEF, "and every condition-code bit survived");
    }
}
