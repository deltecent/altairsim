// The Altair 680b CPU board (mits-680cpu.h) -- the 88-CPU's twin, one core down.
//
// This is the CARD, exercised end to end THROUGH THE MONITOR: the same REGS,
// DISASM, EDIT and STEP an operator types, following a real Cpu6800 that actually
// runs. The point of the test is the wiring, not the arithmetic -- cpu6800 covers
// the flags. If the registry entry, the active-core plumbing, or the ISA-generic
// dispatch (DISASM/EDIT/REGS pick the assembler by isa()) were wrong for the 6800,
// a hand-entered 6800 program would fail to assemble, disassemble, or run RIGHT
// HERE, with no monitor code having ever named "6800".

#include "boards/mits-680cpu.h"
#include "boards/registry.h"
#include "cli/monitor.h"
#include "core/machine.h"
#include "core/value.h"
#include "cpu/cpu.h"
#include "test.h"

#include <memory>
#include <sstream>
#include <string>

using namespace altair;

void test_680board() {
    SECTION("the registry makes a `6800` board: a CPU carrier whose one unit is a 6800");
    {
        auto b = makeBoard("6800");
        CHECK(b != nullptr, "makeBoard(\"6800\") returns a board");
        CHECK(b && b->type() == "6800", "and it names itself 6800");

        // The core is a UNIT, and it is a 6800 CPU. (It decodes nothing -- it is a
        // BusMaster, it originates cycles rather than answering them -- which is the
        // default and needs no override; the end-to-end run below is the real proof.)
        auto units = b->units();
        CHECK(units.size() == 1 && units[0].name == "6800" && units[0].kind == UnitKind::Cpu,
              "its one unit is a 6800 CPU");

        // FLAT OUT (0) IS THE DEFAULT, the same as the 8080 and Z80 cards: the three
        // CPU cards behave alike unless you ask otherwise (mits-680cpu.h).
        long long hz = -1;
        for (auto& p : b->properties())
            if (p.name == "clock_hz") hz = p.get().i();
        CHECK(hz == 0, "clock_hz defaults to 0 -- flat out, like the other CPU cards");
    }

    SECTION("added to a machine, it is the active core and its ISA is 6800");
    {
        Machine m;
        std::string err;
        CHECK(m.add("6800", "cpu0", err), "add a 6800 CPU board");
        CpuCore* c = m.cpu();
        CHECK(c != nullptr, "the machine has an active core");
        CHECK(c && std::string(c->isa()) == "6800", "and it is a 6800");
    }

    // ---------------------------------------------------------------------
    // The whole point of the board: hand-enter a 6800 program with EDIT (typing
    // mnemonics, which only assemble because the active core's ISA is 6800), read
    // it straight back with DISASM, then STEP through real execution and watch REGS
    // move. The reset vector at FFFE/FFFF is the faithful 6800 restart: the first
    // STEP loads PC from it and runs there (cpu6800.cpp), so we point it at 0100.
    // ---------------------------------------------------------------------
    SECTION("EDIT assembles 6800 mnemonics, DISASM reads them back, STEP runs them");
    {
        Machine m;
        Monitor mon(m);
        std::ostringstream setup;
        mon.exec("BOARDS ADD 6800 cpu0", setup);
        mon.exec("BOARDS ADD memory mem0", setup);
        mon.exec("SET mem0 fill=zero", setup);
        mon.exec("REGION ADD mem0 type=ram at=0 size=64K", setup);
        mon.exec("POWER ON", setup);

        // A whole session: EDIT types three 6800 instructions at 0100 (their lengths
        // differ -- 2, 1, 3 -- so a contiguous DUMP proves each dropped by its own
        // encoding), the reset vector is set to 0100, then S 3 runs the restart fetch
        // plus the three instructions. LDAA #41 -> A=41; INCA -> A=42; STAA 0200
        // writes 42 to memory.
        std::istringstream in(
            "EDIT 0100\n"
            "LDAA #41\n"       // 0100 <- 86 41, drop to 0102
            "INCA\n"           // 0102 <- 4C,    drop to 0103
            "STAA 0200\n"      // 0103 <- B7 02 00, drop to 0106
            ".\n"
            "DISASM 0100 3\n"
            "DUMP 0100-0105\n"
            "DEPOSIT FFFE 01 00\n"   // reset vector -> 0x0100 (big-endian: hi at FFFE)
            "S 3\n"
            "DUMP 0200-0200\n"
            "QUIT\n");
        std::ostringstream out;
        mon.repl(in, out, /*interactive=*/false);
        std::string s = out.str();

        // The three mnemonics assembled to contiguous bytes -- proof the prompt
        // dropped by each instruction's length, and that the 6800 assembler ran.
        CHECK(s.find("86 41 4C B7 02 00") != std::string::npos,
              "EDIT assembled LDAA #41 / INCA / STAA 0200 to 86 41 4C B7 02 00");

        // DISASM read the same bytes straight back through the 6800 disassembler.
        CHECK(s.find("LDAA #41") != std::string::npos, "DISASM reads back LDAA #41");
        CHECK(s.find("INCA") != std::string::npos, "and INCA");
        CHECK(s.find("STAA 0200") != std::string::npos, "and STAA 0200");

        // EDIT never leaked a mnemonic to the command loop.
        CHECK(s.find("unknown command") == std::string::npos,
              "EDIT consumed its own mnemonic lines");

        // STEP actually executed on a real Cpu6800: A went 41 then 42, the PC walked
        // off the end of the third instruction (0106), and STAA landed 42 at 0200.
        CHECK(s.find("A=42") != std::string::npos, "LDAA #41 then INCA left A=42");
        CHECK(s.find("P=0106") != std::string::npos,
              "and the PC advanced through all three instructions to 0106");
        CHECK(m.cpu() && m.cpu()->pc() == 0x0106,
              "the active core's PC agrees -- STEP drove the real 6800");

        // The store reached memory: 0200 reads back 42.
        size_t d = s.rfind("0200");
        CHECK(d != std::string::npos && s.find(" 42", d) != std::string::npos,
              "STAA 0200 wrote 42 to memory");
    }
}
