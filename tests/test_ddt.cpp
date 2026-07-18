#include "test.h"

#include "boards/s100-memory.h"
#include "core/debug.h"
#include "core/machine.h"
#include "cpu/cpu.h"

#include <string>

// ---------------------------------------------------------------------------
// HOW DDT SETS A BREAKPOINT -- and it is not what people assume.
//
// The assumption (mine, until I tested it) is that a debugger's breakpoint needs
// interrupts: something has to reach in and stop the processor. DDT needs no such
// thing, and the machine it runs on need not have a single interrupt source in it.
// A DDT breakpoint is ONE BYTE OF SELF-MODIFYING CODE:
//
//     FF = RST 7, a ONE-BYTE `CALL 0038h`
//
// One byte, so it fits over the shortest instruction there is and never disturbs
// the byte after it. DDT saves the opcode it covers, writes FF, and plants a jump
// at 0038h. When the guest reaches the patched address it CALLs DDT -- the guest's
// own program does the calling, synchronously, with no wire moving anywhere.
//
// SID and ZSID are the same mechanism, and not by coincidence: FF is RST 38h on a
// Z80 and lands at the same 0038h. The debugger did not have to be ported.
//
// THE OFF-BY-ONE THAT IS THE WHOLE TRICK. RST pushes the address of the NEXT
// instruction, so the stack holds bp+1, not bp. Every software-breakpoint debugger
// ever written subtracts that one, and it is why the handler below reads POP H /
// DCX H before it does anything else. Get it wrong and DDT restores the byte one
// past the breakpoint -- corrupting the program it was asked to debug, in a way
// that shows up as a wrong answer somewhere else entirely.
//
// WHY A UNIT TEST WHEN THERE IS AN ACCEPTANCE TEST. tests/acceptance/ddt.exp drives
// the real DDT.COM off the tracked CP/M image, which is the better test -- but it
// needs `expect`, so it does not run on Windows. This one is C++ and runs on all
// three CI platforms, and it pins the mechanic on BOTH cores, which the acceptance
// test cannot do (there is no Z80 CP/M image in the tree).
// ---------------------------------------------------------------------------

using namespace altair;

namespace {

// A 64K machine with the named core in it -- "8080" for DDT, "z80" for SID.
struct Rig {
    Machine m;
    CpuCore* cpu = nullptr;

    explicit Rig(const char* core) {
        std::string err;
        Board* b = m.add("memory", "mem0", err);
        MemoryBoard* mem = dynamic_cast<MemoryBoard*>(b);
        Region r;
        r.kind = RegionKind::Ram;
        r.at = 0;
        r.size = 0x10000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);
        mem->power();
        m.add(core, "cpu0", err);
        cpu = m.cpu();
        cpu->reset(Reset::PowerOn);
    }

    void load(std::initializer_list<uint8_t> code, uint16_t at) {
        for (uint8_t byte : code) m.bus.memWrite(at++, byte);
    }
    uint8_t peek(uint16_t a) { return m.bus.peek(a); }
    uint16_t word(uint16_t a) { return (uint16_t)(m.bus.peek(a) | (m.bus.peek(a + 1) << 8)); }

    uint32_t reg(const char* name) {
        for (const RegDef& r : cpu->registers())
            if (r.name == name) return r.get();
        return 0xEEEEEEEEu;
    }
    void setReg(const char* name, uint32_t v) {
        for (const RegDef& r : cpu->registers())
            if (r.name == name) r.set(v);
    }
};

// THE PROGRAM UNDER TEST, at 0100h because that is where a .COM lands.
//
//     0100  3E 11     MVI A,11
//     0102  06 22     MVI B,22        <- the breakpoint goes HERE
//     0104  0E 33     MVI C,33
//     0106  76        HLT
//
// Two-byte instructions on purpose: a one-byte FF written over `06 22` leaves the
// 22 sitting there as a stray operand, so a handler that restores the wrong address
// does not merely fail to help -- it executes `MVI B` with rubbish, or falls into
// the 22. The damage is visible.
const uint16_t PROG = 0x0100;
const uint16_t BP = 0x0102;      // the patched instruction
const uint8_t ORIG = 0x06;       // the opcode DDT saved before writing FF

// DDT'S HANDLER, in the five instructions it really is. Same opcodes on both cores.
//
//     0038  E1        POP H       ; HL = bp+1 -- what RST pushed
//     0039  2B        DCX H       ; HL = bp   -- the off-by-one, undone
//     003A  36 06     MVI M,06    ; put the guest's own opcode back
//     003C  E5        PUSH H      ; and return TO the breakpoint, not past it
//     003D  C9        RET
//
// The real DDT does more here (it saves every register, unpatches EVERY breakpoint,
// and gives you a `-` prompt), but the resume path is exactly this shape, and this
// is the part that has to be right for `G` to work afterwards.
void plantHandler(Rig& rig) {
    rig.load({0xE1, 0x2B, 0x36, ORIG, 0xE5, 0xC9}, 0x0038);
}

// The full cycle on one core: patch, trap, restore, resume.
void breakpointCycle(const char* core) {
    Rig rig(core);
    rig.load({0x3E, 0x11, 0x06, 0x22, 0x0E, 0x33, 0x76}, PROG);
    plantHandler(rig);

    const uint16_t SP0 = 0xBFFF;
    rig.setReg("SP", SP0);
    rig.cpu->setPc(PROG);

    // DDT sets the breakpoint: save the opcode, write FF over it.
    const uint8_t saved = rig.peek(BP);
    CHECK(saved == ORIG, "the opcode DDT is about to cover is the one we assembled");
    rig.m.bus.memWrite(BP, 0xFF);

    // EVERY RUN HERE IS STEP-CAPPED, and that is not belt-and-braces. The failure
    // this file is FOR -- a handler that restores the wrong address, or none at all
    // -- leaves the FF in place, so the resumed program traps again, and again, and
    // `run(0)` (run until something stops us) never comes back. A mutation test
    // caught exactly that: I broke the restore, and the assertions below never got
    // the chance to fail -- the process just sat there. A hang is not a test result.
    // The cap turns that bug into StopReason::Steps, which fails loudly and at once.
    const int CAP = 100;   // the program is a dozen instructions

    // Run to the trap. `G100` in DDT -- and note that NOTHING interrupts: the
    // debugger's own breakpoint is what stops us, but only because we set it at the
    // handler's front door to freeze the machine mid-trap and look at the stack.
    // Take this breakpoint away and the guest sails through the handler unaided,
    // which is the point.
    rig.m.debug.add(BreakKind::Pc, 0x0038, 0x0038);
    RunResult r = rig.m.debug.run(CAP);

    CHECK(r.why == StopReason::Breakpoint, "the guest reached 0038 on its own -- no interrupt");
    CHECK(rig.cpu->pc() == 0x0038, "FF is a one-byte CALL 0038h, on both cores");
    CHECK(rig.reg("A") == 0x11, "the instruction BEFORE the breakpoint ran");
    CHECK((rig.reg("BC") & 0xFF00) == 0x0000, "and the patched one did NOT -- B is untouched");

    // THE OFF-BY-ONE, PINNED. This is the assertion the whole file exists for.
    CHECK(rig.reg("SP") == SP0 - 2, "RST pushed a return address, like the CALL it is");
    CHECK(rig.word((uint16_t)(SP0 - 2)) == BP + 1,
          "and it points PAST the FF -- a debugger that trusts it restores the wrong byte");

    // Let the handler run: restore, and return to the breakpoint address itself.
    std::string err;
    rig.m.debug.remove(rig.m.debug.breakpoints()[0].id, err);
    r = rig.m.debug.run(CAP);

    CHECK(r.why == StopReason::Halted,
          "the resumed program ran to its HLT -- Steps here means it is trapping in a loop");
    CHECK(rig.peek(BP) == ORIG, "the FF is GONE -- DDT put the guest's opcode back");
    CHECK((rig.reg("BC") & 0xFF00) == 0x2200, "the restored instruction re-executed");
    CHECK((rig.reg("BC") & 0x00FF) == 0x0033, "and the program carried on past it");
    CHECK(rig.reg("SP") == SP0, "with the stack balanced -- the trap left nothing behind");
}

} // namespace

void test_ddt() {
    SECTION("DDT's breakpoint is RST 7, and it needs no interrupt at all");

    breakpointCycle("8080");

    // SID/ZSID, and the reason they needed no new mechanism: FF is RST 38h on a
    // Z80, vectoring to the same 0038h, and every instruction in the handler above
    // is byte-identical across the two instruction sets.
    breakpointCycle("z80");

    SECTION("our breakpoints do not collide with the guest's -- ours patch nothing");

    // WHY THIS MATTERS. altairsim's BREAK is a PC comparison in the run loop, not a
    // byte written into the guest's memory. If it were a patch, debugging DDT would
    // be impossible: two debuggers would fight over the same byte at the same
    // address, each restoring what the other saved, and the corruption would look
    // like a CPU bug. A monitor breakpoint at an address the guest has ALREADY
    // planted FF on must stop us BEFORE the RST, and leave the FF alone.
    Rig rig("8080");
    rig.load({0x3E, 0x11, 0x06, 0x22, 0x0E, 0x33, 0x76}, PROG);
    plantHandler(rig);
    rig.setReg("SP", 0xBFFF);
    rig.cpu->setPc(PROG);
    rig.m.bus.memWrite(BP, 0xFF);      // the GUEST's breakpoint

    int ours = rig.m.debug.add(BreakKind::Pc, BP, BP);   // ...and ours, same address
    RunResult r = rig.m.debug.run(100);

    CHECK(r.why == StopReason::Breakpoint && r.bp == ours, "our breakpoint stops the run");
    CHECK(rig.cpu->pc() == BP, "at the address, with the RST not yet executed");
    CHECK(rig.reg("SP") == 0xBFFF, "nothing has been pushed");
    CHECK(rig.peek(BP) == 0xFF, "and the guest's own FF is still exactly where it put it");
}
