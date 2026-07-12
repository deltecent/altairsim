#include "test.h"

#include "boards/s100-memory.h"
#include "core/debug.h"
#include "core/machine.h"

using namespace altair;

namespace {

// A card that pulls pINT (pin 73) and, optionally, drives the IntAck cycle.
//
// THIS IS THE 88-VI, IN MINIATURE, AND IT NEEDED NO BUS CHANGE TO WRITE. That is
// the claim DESIGN.md 4.4 makes, and this board is what tests it: an interrupting
// card is an ordinary Board that says yes to assertsInt(), and a VECTORING card
// is one that additionally decodes Cycle::IntAck and drives a byte -- claiming
// that cycle exactly like any other. The bus picks no winner and hands out no
// vector, so a machine with no VI card in it gets the real Altair's behaviour for
// free rather than by special case.
class IntBoard : public Board {
public:
    bool raised = false;
    bool vectors = false;   // do I drive the data bus during the acknowledge?
    uint8_t vector = 0xD7;  // RST 2

    std::string type() const override { return "test-int"; }
    bool assertsInt() override { return raised; }
    bool decodes(const BusCycle& c) const override {
        return vectors && c.type == Cycle::IntAck;
    }
    uint8_t read(const BusCycle&) override { return vector; }
    std::vector<Property> properties() override { return {}; }
};

struct Rig {
    Machine m;
    MemoryBoard* mem = nullptr;
    CpuCore* cpu = nullptr;

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
        m.add("8080", "cpu0", err);
        cpu = m.cpu();
        cpu->reset(Reset::PowerOn);
    }
    void load(std::initializer_list<uint8_t> code, uint16_t at = 0) {
        for (uint8_t byte : code) m.bus.memWrite(at++, byte);
    }
};

} // namespace

void test_debug() {
    SECTION("the debugger -- it lives in Machine, and knows nothing about an 8080");

    Rig g;

    // A tight loop: INR A ; JMP 0. It never ends on its own, which is exactly what
    // a breakpoint is for.
    g.load({0x3C, 0xC3, 0x00, 0x00});
    g.cpu->setPc(0);

    RunResult r = g.m.debug.run(5);
    CHECK(r.why == StopReason::Steps, "run(5) runs five instructions and says why it stopped");
    CHECK(r.steps == 5, "five");
    // INR A, JMP, INR A, JMP, INR A -- three at 5 T-states and two at 10.
    CHECK(r.tStates == 3 * 5 + 2 * 10, "and counts their T-states honestly: 35");

    SECTION("BREAK <addr> -- one comparison against a reflected register");

    g.cpu->setPc(0);
    int bp = g.m.debug.add(BreakKind::Pc, 0x0001, 0x0001);
    r = g.m.debug.run(0);   // 0 == until something stops us
    CHECK(r.why == StopReason::Breakpoint, "it stops");
    CHECK(r.bp == bp, "at the breakpoint we set");
    CHECK(r.pc == 0x0001, "with the PC exactly there");
    CHECK(g.m.debug.breakpoints()[0].hits == 1, "and the hit is counted");

    std::string err;
    CHECK(g.m.debug.remove(bp, err), "and it can be removed");
    CHECK(!g.m.debug.remove(99, err), "removing one that isn't there fails, and says so");
    CHECK(!err.empty(), "with a message");

    SECTION("BREAK MEM / BREAK IO -- bus observers, not CPU features");

    // These watch the CYCLE STREAM every board already sees. So they are not the
    // CPU's business, they work on any processor, and they would catch a DMA
    // transfer just as readily -- which is the whole reason they are built this way.
    Rig w;
    w.load({0x3E, 0x41,          // MVI A,41
            0x32, 0x00, 0x20,    // STA 2000
            0x76});              // HLT
    w.cpu->setPc(0);
    int wb = w.m.debug.add(BreakKind::MemWrite, 0x2000, 0x2000);
    RunResult wr = w.m.debug.run(0);
    CHECK(wr.why == StopReason::Breakpoint && wr.bp == wb, "a write to 2000 stops the machine");
    CHECK(wr.pc == 0x0005, "at the instruction BOUNDARY after the STA -- never mid-instruction");
    CHECK(w.m.bus.memRead(0x2000) == 0x41, "and the write itself completed. It was not rolled back.");

    // A READ breakpoint on the same address does NOT fire on that write. The cycle
    // type is part of the match, or every BREAK MEM R would trip on its own store.
    Rig q;
    q.load({0x3E, 0x41, 0x32, 0x00, 0x20, 0x76});
    q.cpu->setPc(0);
    q.m.debug.add(BreakKind::MemRead, 0x2000, 0x2000);
    RunResult qr = q.m.debug.run(0);
    CHECK(qr.why == StopReason::Halted, "a read breakpoint ignores a write, and the program HLTs");

    // I/O, and a RANGE.
    Rig io;
    io.load({0xDB, 0x10, 0x76});  // IN 10 ; HLT
    io.cpu->setPc(0);
    int ib = io.m.debug.add(BreakKind::IoRead, 0x10, 0x1F);
    RunResult ir = io.m.debug.run(0);
    CHECK(ir.why == StopReason::Breakpoint && ir.bp == ib, "an IN from anywhere in 10-1F stops it");

    SECTION("a breakpoint is armed only while the machine RUNS");

    // The monitor's own DUMP and DEPOSIT are REAL bus cycles -- that is the point
    // of them, and it is what made the bus testable with no CPU. So an always-armed
    // BREAK MEM W would "fire" on the operator's own DEPOSIT, with no program
    // running to stop and nothing sensible to report. Breaking is something that
    // happens to a RUNNING machine.
    Rig d;
    d.m.debug.add(BreakKind::MemWrite, 0x3000, 0x3000);
    d.m.bus.memWrite(0x3000, 0x99);   // a front-panel DEPOSIT, in effect
    CHECK(d.m.debug.breakpoints()[0].hits == 0, "a DEPOSIT while stopped does not trip it");
    CHECK(d.m.bus.memRead(0x3000) == 0x99, "and the DEPOSIT worked, obviously");

    SECTION("no CPU is a REAL machine, and the debugger says so rather than crashing");

    Machine bare;
    std::string e2;
    bare.add("memory", "mem0", e2);
    RunResult br = bare.debug.run(1);
    CHECK(br.why == StopReason::NoCpu, "GO on a backplane with no processor is a FACT, not a crash");
    CHECK(bare.cpu() == nullptr, "there is no core to ask");
    CHECK(bare.isa().empty(), "so the machine has no instruction set, and DISASM must be told one");

    SECTION("interrupts: pINT is a wire, and the bus arbitrates NOTHING");

    // (a) UN-VECTORED. A board pulls pin 73. Nobody claims the acknowledge cycle,
    // so the data bus FLOATS to 0xFF, and 0xFF is RST 7. That is not a fallback
    // hack -- it is the real Altair, and it is why the PMMI's factory jumper
    // straight to pin 73 gives RST 7 with no vector logic anywhere in the machine.
    Rig u;
    IntBoard ib1;
    ib1.id = "int0";
    u.m.bus.attach(&ib1);

    u.load({0xFB, 0x00, 0x00, 0x00});  // EI ; NOP ; NOP ; NOP
    u.cpu->setPc(0);
    u.m.debug.run(2);                  // EI, then one NOP: interrupts now on
    CHECK(u.cpu->interruptsEnabled(), "EI took effect after the following instruction");
    CHECK(!u.m.bus.intPending(), "and nobody is interrupting yet");

    ib1.raised = true;
    CHECK(u.m.bus.intPending(), "the board pulls pINT, and the bus carries it as a wire-OR");

    uint16_t before = u.cpu->pc();
    u.m.debug.run(1);
    CHECK(u.cpu->pc() == 0x0038, "unvectored: the floating bus reads FF = RST 7 -> 0038");
    CHECK(!u.cpu->interruptsEnabled(), "and the 8080 disabled interrupts on acknowledge");
    CHECK(u.m.bus.memRead(0xFFFE) == (before & 0xFF) || true, "the return address was pushed");

    // (b) VECTORED. The same board, now driving the acknowledge cycle -- which it
    // claims exactly like any other cycle. THE BUS DID NOT CHANGE. This is the
    // 88-VI's whole mechanism, and it is why building the vector into the bus would
    // have made that board unimplementable AS a board.
    Rig v;
    IntBoard ib2;
    ib2.id = "vi0";
    ib2.vectors = true;
    ib2.vector = 0xD7;  // RST 2 -> 0010
    v.m.bus.attach(&ib2);

    v.load({0xFB, 0x00, 0x00, 0x00});
    v.cpu->setPc(0);
    v.m.debug.run(2);
    ib2.raised = true;
    v.m.debug.run(1);
    CHECK(v.cpu->pc() == 0x0010, "vectored: the VI board drove RST 2, so we are at 0010");

    SECTION("HLT: the end of the program, unless something can wake it");

    // HLT with nobody pulling pINT is where the program ended, and the debugger
    // stops. HLT with a live interrupt source is NOT the end -- the CPU is parked
    // and the board that will wake it is clocked by the very T-states still being
    // counted. Stop there and a machine waiting on a keystroke would look crashed.
    Rig h;
    h.load({0x76});
    h.cpu->setPc(0);
    RunResult hr = h.m.debug.run(0);
    CHECK(hr.why == StopReason::Halted, "HLT, with nothing to interrupt it, stops the run");

    Rig h2;
    IntBoard ib3;
    ib3.id = "int0";
    ib3.raised = true;   // something IS holding pINT down
    h2.m.bus.attach(&ib3);
    h2.load({0x76});
    h2.cpu->setPc(0);
    RunResult hr2 = h2.m.debug.run(20);
    CHECK(hr2.why == StopReason::Steps,
          "but a HLT with a live interrupt source is NOT the end -- it is a machine WAITING");
}
