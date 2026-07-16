#include "test.h"

#include "boards/s100-memory.h"
#include "core/debug.h"
#include "core/machine.h"

using namespace altair;

namespace {

// ---------------------------------------------------------------------------
// A DMA controller, in miniature -- the bus-mastering analogue of test_debug's
// IntBoard, and the proof that DESIGN.md 4.5 is real.
//
// It DECODES NOTHING: like a CPU card, a bus master ORIGINATES cycles, it does not
// answer them (test_cpu asserts the CPU card decodes no address and no port; the
// same is true here). It pulls pHOLD when it has a block to move, and once the run
// loop grants pHLDA it drives real RAM->RAM cycles through the very
// BusMaster::step(Bus&) the CPU uses -- so a DMA transfer is just cycles on the
// backplane, snooped and observed like any other.
//
// HAS-A, NOT IS-A (board.h): the board CONTAINS a BusMaster (mover_) and returns it
// from busMaster(); it does NOT inherit BusMaster the way the CPU card does. If it
// did, Machine::master()/masters() -- a dynamic_cast to BusMaster, also how the
// monitor lists the machine's CPUs -- would mistake this controller for a processor
// and try to step it as one.
//
// The one knob that matters is the GRAIN, and it lives entirely in requestsBus():
//   burst       -- hold pHOLD true for the whole block; serviceDma's while-loop
//                  drains it in a single grant.
//   cycle-steal -- drop pHOLD after one byte and re-arm a Clock deadline, so the CPU
//                  runs instructions between DMA cycles.
// The run loop knows about neither mode. That is the whole point.
// ---------------------------------------------------------------------------
class DmaBoard : public Board {
public:
    DmaBoard() { mover_.d = this; }

    // Program a transfer and assert pHOLD. gap is the cycle-steal re-arm interval in
    // T-states (ignored for a burst).
    void start(uint16_t src, uint16_t dst, uint16_t len, bool cycleSteal, uint64_t gap = 40) {
        src_ = src;
        dst_ = dst;
        remaining_ = len;
        cycleSteal_ = cycleSteal;
        gap_ = gap;
        hold_ = len > 0;
        holdChanged();  // announce pHOLD, or the run loop's cached wire never sees it
    }

    bool     done()  const { return remaining_ == 0; }
    uint64_t moved() const { return moved_; }

    // Per-byte cost. NOT a real chip's timing -- this is a harness board, not a
    // modeled card -- just a concrete nonzero number, so the stolen cycles show up in
    // clock.now() and the run loop's zero-T-state wedge guard is never provoked.
    static constexpr uint32_t kPerByte = 8;  // ~ a memory read cycle + a write cycle

    std::string type() const override { return "test-dma"; }
    std::vector<Property> properties() override { return {}; }

    bool       requestsBus() const override { return hold_; }
    BusMaster* busMaster()         override { return &mover_; }

private:
    // One granted byte: two REAL bus cycles, so the memory board actually decodes
    // them and any observer/breakpoint watching the stream sees them.
    StepResult transferOne(Bus& bus) {
        uint8_t v = bus.memRead(src_++);
        bus.memWrite(dst_++, v);
        ++moved_;
        if (--remaining_ == 0) {
            hold_ = false;  // block done: release pHOLD for good
            holdChanged();
        } else if (cycleSteal_) {
            hold_ = false;  // yield the bus now...
            holdChanged();
            clock_->after(gap_, [this] {  // ...and ask for it again later
                hold_ = true;
                holdChanged();
            });
        }
        // burst: hold_ stays true, so serviceDma takes the next byte in the same grant
        return {kPerByte, RunStatus::Ok};
    }

    struct Mover : public BusMaster {
        DmaBoard* d = nullptr;
        StepResult step(Bus& bus) override { return d->transferOne(bus); }
    } mover_;

    uint16_t src_ = 0, dst_ = 0;
    uint32_t remaining_ = 0, moved_ = 0;
    bool     hold_ = false, cycleSteal_ = false;
    uint64_t gap_ = 40;
};

// 64K of RAM and an 8080, built by hand -- same shape as test_debug's Rig, with a
// DMA controller attached and a recognizable pattern laid down for it to move.
struct Rig {
    Machine  m;
    DmaBoard dma;

    static constexpr uint16_t kSrc = 0x2000, kDst = 0x3000, kLen = 16;

    Rig() {
        std::string err;
        m.bus.setVerify(true);  // re-derive decode + the interrupt wire every cycle

        Board* b = m.add("memory", "mem0", err);
        auto* mem = dynamic_cast<MemoryBoard*>(b);
        Region r;
        r.kind = RegionKind::Ram;
        r.at = 0;
        r.size = 0x10000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);
        mem->power();

        m.add("8080", "cpu0", err);
        m.cpu()->reset(Reset::PowerOn);

        // The controller goes into a slot like any card. It needs the clock for its
        // cycle-steal re-arm; a burst never looks at it.
        dma.id = "dma0";
        dma.attachClock(&m.clock);
        m.bus.attach(&dma);

        // A distinctive source block, and a destination that is still zero.
        for (uint16_t i = 0; i < kLen; ++i) m.bus.memWrite(kSrc + i, (uint8_t)(0x40 + i));
    }

    void load(std::initializer_list<uint8_t> code, uint16_t at = 0) {
        for (uint8_t byte : code) m.bus.memWrite(at++, byte);
    }
    bool destMatchesSource(uint16_t len = kLen) {
        for (uint16_t i = 0; i < len; ++i)
            if (m.bus.memRead(kDst + i) != (uint8_t)(0x40 + i)) return false;
        return true;
    }
};

} // namespace

void test_dma() {
    SECTION("DMA -- a board becomes a bus master, and the clock notices (DESIGN.md 4.5)");

    // A BURST transfer, triggered before the run. The CPU program is NOP ; HLT, so we
    // can read the arithmetic exactly: the NOP retires, the run loop reaches an
    // instruction boundary, and the whole 16-byte block is stolen in a single grant.
    {
        Rig g;
        g.load({0x00, 0x76});  // NOP ; HLT
        g.m.cpu()->setPc(0);
        g.dma.start(Rig::kSrc, Rig::kDst, Rig::kLen, /*cycleSteal=*/false);

        RunResult r = g.m.debug.run(0);

        CHECK(r.why == StopReason::Halted, "the CPU still reaches its HLT -- DMA did not derail it");
        CHECK(r.steps == 2, "and retired exactly its two instructions, NOP and HLT");
        CHECK(g.dma.done() && g.dma.moved() == Rig::kLen, "the whole block moved");
        CHECK(g.destMatchesSource(), "byte for byte, RAM->RAM, through real bus cycles");

        // NOP is 4 T-states, HLT is 7. The 16-byte burst steals 16 * 8 = 128 more. The
        // CPU did the SAME two instructions either way -- but 128 extra T-states of
        // emulated time elapsed, which is the CPU genuinely losing the bus.
        CHECK(r.tStates == 4 + 7, "the CPU's own StepResult counts only its instructions: 11");
        CHECK(g.m.clock.now() == 4u + 128u + 7u,
              "but the clock elapsed 139 -- the 128 stolen T-states are charged to it");
    }

    SECTION("DMA -- the same run with no transfer proves the 128 was the theft");

    {
        Rig g;
        g.load({0x00, 0x76});
        g.m.cpu()->setPc(0);
        // dma NOT started: hold_ is false, serviceDma is inert.
        RunResult r = g.m.debug.run(0);
        CHECK(r.why == StopReason::Halted && r.steps == 2, "identical CPU work");
        CHECK(g.m.clock.now() == 4u + 7u, "and only 11 T-states elapse -- no DMA, no theft");
        CHECK(g.dma.moved() == 0, "the controller sat idle, holding nothing");
    }

    SECTION("cycle-steal -- the CPU runs BETWEEN the stolen cycles, not after them");

    // A tight INR A ; JMP 0 loop that never ends on its own, and an 8-byte transfer
    // that trickles across one byte per grant with a 40 T-state gap. Driving the
    // machine one instruction at a time, we count how many instructions the CPU
    // retires while the block is still in flight.
    {
        Rig g;
        g.load({0x3C, 0xC3, 0x00, 0x00});  // INR A ; JMP 0
        g.m.cpu()->setPc(0);
        g.dma.start(Rig::kSrc, Rig::kDst, /*len=*/8, /*cycleSteal=*/true, /*gap=*/40);

        int cpuStepsDuringTransfer = 0;
        for (int guard = 0; guard < 10000 && !g.dma.done(); ++guard) {
            g.m.debug.run(1);  // exactly one CPU instruction, plus its serviceDma
            ++cpuStepsDuringTransfer;
        }

        CHECK(g.dma.done() && g.dma.moved() == 8, "all eight bytes eventually crossed");
        CHECK(g.destMatchesSource(8), "and correctly");
        CHECK(cpuStepsDuringTransfer > 8,
              "the CPU retired MANY more instructions than there were DMA cycles -- it interleaved");
    }

    SECTION("burst vs cycle-steal is the BOARD's choice -- one grant moves the block");

    // The exact same slice loop over a BURST controller: a single CPU instruction is
    // enough for the whole block to move, because the burst holds pHOLD across every
    // byte in one grant. The contrast with the cycle-steal count above is the whole
    // distinction, and the run loop has no mode flag for it.
    {
        Rig g;
        g.load({0x3C, 0xC3, 0x00, 0x00});
        g.m.cpu()->setPc(0);
        g.dma.start(Rig::kSrc, Rig::kDst, /*len=*/8, /*cycleSteal=*/false);

        int cpuSteps = 0;
        for (int guard = 0; guard < 10000 && !g.dma.done(); ++guard) {
            g.m.debug.run(1);
            ++cpuSteps;
        }
        CHECK(g.dma.done() && g.dma.moved() == 8, "the block moved");
        CHECK(cpuSteps == 1, "in a single grant -- one CPU instruction sufficed for all eight bytes");
    }

    SECTION("BREAK MEM W catches a DMA write, because a DMA cycle is just a cycle");

    // The debugger's breakpoints watch the bus cycle stream from outside the
    // backplane (DESIGN.md 3.0.3), and a DMA write is on that stream like any other.
    // So a BREAK MEM W on the destination fires on the controller's OWN store, with no
    // CPU instruction touching that address at all -- which is exactly the promise
    // BreakKind::MemWrite's comment makes ("ANY master ... CPU, DMA, or the front
    // panel"), now with a DMA master to make good on it.
    {
        Rig g;
        g.load({0x00, 0x00, 0x00, 0x76});  // NOP sled ; HLT -- the CPU never writes kDst
        g.m.cpu()->setPc(0);
        int bp = g.m.debug.add(BreakKind::MemWrite, Rig::kDst, Rig::kDst);
        g.dma.start(Rig::kSrc, Rig::kDst, Rig::kLen, /*cycleSteal=*/false);

        RunResult r = g.m.debug.run(0);
        CHECK(r.why == StopReason::Breakpoint && r.bp == bp,
              "the DMA's write to the destination tripped the breakpoint");
        CHECK(g.m.bus.memRead(Rig::kDst) == 0x40, "and the write itself landed -- not rolled back");
    }
}
