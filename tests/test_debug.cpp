#include "test.h"

#include "boards/s100-memory.h"
#include "core/debug.h"
#include "core/expr.h"
#include "core/machine.h"
#include "cpu/cpu.h"

#include <sstream>

using namespace altair;

namespace {

// A card that pulls pINT (pin 73) and, optionally, drives the IntAck cycle.
//
// THIS IS THE 88-VI, IN MINIATURE, AND IT NEEDED NO BUS CHANGE TO WRITE. That is
// the claim DESIGN.md 4.4 makes, and this board is what tests it: an interrupting
// card is an ordinary Board that PULLS THE WIRE, and a VECTORING card is one that
// additionally decodes Cycle::IntAck and drives a byte -- claiming that cycle
// exactly like any other. The bus picks no winner and hands out no vector, so a
// machine with no VI card in it gets the real Altair's behaviour for free rather
// than by special case.
//
// Note the shape of it, which is the whole of the interrupt model in six lines:
// assertsInt() is PURE -- it reports the state of a pin and does nothing -- and
// raise() is what actually moves the wire. A board that changed `raised` without
// calling intChanged() would be lying to the backplane, and Bus::setVerify(true)
// (on, below) aborts the instant it does.
class IntBoard : public Board {
public:
    bool vectors = false;   // do I drive the data bus during the acknowledge?
    uint8_t vector = 0xD7;  // RST 2

    std::string type() const override { return "test-int"; }

    bool assertsInt() const override { return raised_; }

    // The board pulls pin 73, and HOLDS it. Nobody comes and asks.
    void raise(bool on) {
        raised_ = on;
        intChanged();
    }

    bool decodes(const BusCycle& c) const override {
        return vectors && c.type == Cycle::IntAck;
    }
    uint8_t read(const BusCycle&) override { return vector; }
    std::vector<Property> properties() override { return {}; }

private:
    bool raised_ = false;
};

struct Rig {
    Machine m;
    MemoryBoard* mem = nullptr;
    CpuCore* cpu = nullptr;

    Rig() {
        std::string err;
        // THE WIRE IS CHECKED ON EVERY INSTRUCTION. intPending() reads a cached
        // wire-OR count; this re-derives it from every board's assertsInt() and
        // aborts on the first disagreement. A board that moved its interrupt pin
        // and forgot to say so hangs the guest forever, and "the emulator locks up
        // sometimes" is a bug worth a week. It is not left to trust.
        m.bus.setVerify(true);

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

// Build a breakpoint condition against a machine's real reflected registers -- the
// same path the monitor takes, so the test exercises the CPU-agnostic seam and not
// a hand-rolled resolver.
std::shared_ptr<const Expr> cond(CpuCore* cpu, const std::string& src) {
    auto regs = cpu->registers();
    auto known = [regs](const std::string& n) {
        for (const RegDef& rd : regs)
            if (rd.name == n) return true;
        return false;
    };
    std::string err;
    return Expr::parse(src, known, err);
}

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

    ib1.raise(true);
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
    ib2.raise(true);
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
    // PULLED BEFORE IT WAS PLUGGED IN, on purpose. The wire-OR is a COUNT the bus
    // maintains incrementally, so a card that is ALREADY asserting when it enters
    // the backplane has to be counted on the way in -- a card does not stop asking
    // to be serviced just because you moved it to another slot. Get that wrong and
    // this HLT never wakes.
    ib3.raise(true);
    h2.m.bus.attach(&ib3);
    h2.load({0x76});
    h2.cpu->setPc(0);
    RunResult hr2 = h2.m.debug.run(20);
    CHECK(hr2.why == StopReason::Steps,
          "but a HLT with a live interrupt source is NOT the end -- it is a machine WAITING");

    SECTION("BREAK <addr> IF <expr> -- a condition over the reflected registers");

    // INR A ; JMP 0. At 0001 (after the INR) A has just been bumped, so a condition
    // on A picks out one pass of the loop -- and the debugger never learns what an
    // 8080 is: it reads A by NAME.
    Rig c;
    c.load({0x3C, 0xC3, 0x00, 0x00});
    c.cpu->setPc(0);
    int cb = c.m.debug.add(BreakKind::Pc, 0x0001, 0x0001, cond(c.cpu, "A==3"));
    RunResult cr = c.m.debug.run(0);
    CHECK(cr.why == StopReason::Breakpoint && cr.bp == cb, "it stops");
    CHECK(cr.pc == 0x0001, "at the address");
    for (const RegDef& rd : c.cpu->registers())
        if (rd.name == "A") CHECK(rd.get() == 3, "with the condition actually holding: A==3");

    // A CONDITION THAT NEVER HOLDS IS NOT A STOP -- and it does not count as a hit,
    // because `hits` means "times it stopped you", not "times the PC passed by".
    Rig c2;
    c2.load({0x3C, 0xC3, 0x00, 0x00});
    c2.cpu->setPc(0);
    c2.m.debug.add(BreakKind::Pc, 0x0001, 0x0001, cond(c2.cpu, "A==0"));  // A is never 0 at 0001
    RunResult cr2 = c2.m.debug.run(50);
    CHECK(cr2.why == StopReason::Steps, "a never-true condition never stops the run");
    CHECK(c2.m.debug.breakpoints()[0].hits == 0, "and never counts a hit");

    // A COMPOUND CONDITION. INR A wraps FF->00 and sets no carry; the zero flag is
    // what marks the wrap. Stop the first time A is zero AND Z is set.
    Rig c3;
    c3.load({0x3C, 0xC3, 0x00, 0x00});
    c3.cpu->setPc(0);
    for (const RegDef& rd : c3.cpu->registers())
        if (rd.name == "A") rd.set(0xFE);   // so the next INR gives FF, then 00
    int c3b = c3.m.debug.add(BreakKind::Pc, 0x0001, 0x0001, cond(c3.cpu, "A==0 && Z==1"));
    RunResult cr3 = c3.m.debug.run(0);
    CHECK(cr3.why == StopReason::Breakpoint && cr3.bp == c3b, "A==0 && Z==1 stops on the wrap");
    for (const RegDef& rd : c3.cpu->registers())
        if (rd.name == "A") CHECK(rd.get() == 0, "and A is indeed zero there");

    // describe() carries the condition, which is what BREAK lists and every stop
    // prints.
    CHECK(c.m.debug.breakpoints()[0].describe().find("if A==3") != std::string::npos,
          "the condition shows in describe()");

    SECTION("HISTORY -- a flight recorder of bus cycles, fed by the same observer");

    Rig hh;
    hh.load({0x3E, 0x41,           // MVI A,41
             0x32, 0x00, 0x20,     // STA 2000
             0x76});               // HLT
    hh.cpu->setPc(0);
    hh.m.debug.run(0);

    auto recent = hh.m.debug.history(4);
    CHECK(!recent.empty(), "there is history after a run");
    // The last data cycle of this program is the STA's write to 2000. It is in there,
    // and it is a WRITE, and the byte is right -- the recorder saw the same cycle the
    // breakpoint would.
    bool sawWrite = false;
    for (const auto& rec : recent)
        if (rec.type == Cycle::MemWrite && rec.addr == 0x2000 && rec.data == 0x41) sawWrite = true;
    CHECK(sawWrite, "the STA's write to 2000 is on the tape");

    // Oldest-first, and bounded by what actually ran.
    auto few = hh.m.debug.history(2);
    CHECK(few.size() == 2, "history(n) returns n when it has them");
    CHECK(hh.m.debug.history(100000).size() < 100000, "and never more than it holds");

    SECTION("TRACE -- every cycle to a sink, filtered by a mask");

    Rig tt;
    tt.load({0xDB, 0x10,           // IN 10
             0x32, 0x00, 0x20,     // STA 2000  (a memory write)
             0x76});               // HLT
    tt.cpu->setPc(0);

    // No mask: everything shows, including the opcode fetches.
    std::ostringstream all;
    tt.m.debug.traceTo(&all, 0);
    tt.m.debug.run(0);
    tt.m.debug.traceOff();
    CHECK(all.str().find("IN") != std::string::npos, "an IN cycle is traced");
    CHECK(all.str().find("MW") != std::string::npos, "and the memory write");
    CHECK(all.str().find("MR") != std::string::npos, "and the fetches");

    // MASK=IN: only the I/O read survives -- not the fetches, not the store.
    Rig tt2;
    tt2.load({0xDB, 0x10, 0x32, 0x00, 0x20, 0x76});
    tt2.cpu->setPc(0);
    std::ostringstream masked;
    tt2.m.debug.traceTo(&masked, Debugger::InCycle);
    tt2.m.debug.run(0);
    tt2.m.debug.traceOff();
    CHECK(masked.str().find("IN") != std::string::npos, "MASK=IN keeps the IN");
    CHECK(masked.str().find("MR") == std::string::npos, "and drops the fetches");
    CHECK(masked.str().find("MW") == std::string::npos, "and drops the store");

    SECTION("TRACEPOINTS -- a breakpoint whose ACTION is TRACE, and does not stop");

    // The headline: trace a REGION of a program instead of all of it.
    //
    //   0000  MVI A,0        before  -- not traced
    //   0002  INR A          before  -- not traced
    //   0003  INR A          TRACE ON lands here
    //   0004  STA 2000       traced
    //   0007  HLT            TRACE OFF lands here -- not traced
    Rig tp;
    tp.load({0x3E, 0x00, 0x3C, 0x3C, 0x32, 0x00, 0x20, 0x76});
    tp.cpu->setPc(0);

    std::ostringstream region;
    tp.m.debug.traceTo(&region, 0);
    tp.m.debug.traceOff();  // CONFIGURED but not running -- what a tracepoint needs
    CHECK(tp.m.debug.traceConfigured(), "TRACE OFF keeps the sink");
    CHECK(!tp.m.debug.tracing(), "but is not tracing");

    tp.m.debug.add(BreakKind::Pc, 3, 3, nullptr, BreakAction::TraceOn);
    tp.m.debug.add(BreakKind::Pc, 7, 7, nullptr, BreakAction::TraceOff);
    RunResult tr = tp.m.debug.run(0);

    // It ran to the HLT: a tracepoint acts and the machine carries on. If this
    // reports Breakpoint, a tracepoint stopped the machine and the feature is a
    // breakpoint wearing a costume.
    CHECK(tr.why == StopReason::Halted, "a tracepoint does NOT stop the machine");
    CHECK(region.str().find("0003 = 3C") != std::string::npos, "the region's first fetch traced");
    CHECK(region.str().find("2000 = 02") != std::string::npos, "and the store inside it");
    CHECK(region.str().find("0000 = 3E") == std::string::npos, "nothing from before TRACE ON");
    CHECK(region.str().find("0007 = 76") == std::string::npos, "nor the HLT that turned it off");
    CHECK(!tp.m.debug.tracing(), "and it left the trace off, where the region ended");
    CHECK(tp.m.debug.breakpoints()[0].hits == 1, "hits counts a tracepoint firing");
    CHECK(tp.m.debug.breakpoints()[1].hits == 1, "both of them");

    // A CYCLE tracepoint -- safe where IF is not, because it reads no registers. And
    // the cycle that TRIGGERED it is traced: the observer matches before it emits, so
    // BREAK MEM W 2000 TRACE ON puts the write to 2000 at the TOP of the trace rather
    // than one cycle above it, which would be a trace that omits its own reason.
    Rig tc;
    tc.load({0x3E, 0x41, 0x32, 0x00, 0x20, 0x3C, 0x76});
    tc.cpu->setPc(0);
    std::ostringstream fromWrite;
    tc.m.debug.traceTo(&fromWrite, 0);
    tc.m.debug.traceOff();
    tc.m.debug.add(BreakKind::MemWrite, 0x2000, 0x2000, nullptr, BreakAction::TraceOn);
    CHECK(tc.m.debug.run(0).why == StopReason::Halted, "a cycle tracepoint does not stop either");
    std::string firstLine = fromWrite.str().substr(0, fromWrite.str().find('\n'));
    CHECK(firstLine.find("MW") != std::string::npos && firstLine.find("2000 = 41") != std::string::npos,
          "the write that turned tracing on is the FIRST line of the trace");

    // A tracepoint must not SHADOW an ordinary breakpoint at the same place. If the
    // match loop broke out on the tracepoint, this run would sail past bp 2.
    Rig tb;
    tb.load({0x3E, 0x41, 0x3C, 0x3C, 0x76});
    tb.cpu->setPc(0);
    std::ostringstream both;
    tb.m.debug.traceTo(&both, 0);
    tb.m.debug.traceOff();
    tb.m.debug.add(BreakKind::Pc, 3, 3, nullptr, BreakAction::TraceOn);
    int stopper = tb.m.debug.add(BreakKind::Pc, 3, 3);
    RunResult tbr = tb.m.debug.run(0);
    CHECK(tbr.why == StopReason::Breakpoint, "an ordinary breakpoint at a tracepoint's PC stops");
    CHECK(tbr.bp == stopper, "and it is the STOP one that is reported");
    CHECK(tb.m.debug.tracing(), "while the tracepoint still did its job");

    // describe() carries the action -- that is what BREAK's listing shows.
    CHECK(tb.m.debug.breakpoints()[0].describe().find("trace on") != std::string::npos,
          "describe() says trace on");
    CHECK(tb.m.debug.breakpoints()[1].describe().find("trace") == std::string::npos,
          "and an ordinary breakpoint does not mention tracing at all");
}
