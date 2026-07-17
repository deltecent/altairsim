// The Tarbell boot sequence -- the card that settles PHANTOM* (docs/boards/tarbell-sd.md).
//
// This is not a Tarbell disk controller. It is the PHANTOM*/boot-PROM half of one,
// which is the half that constrains the BUS, and it lives here rather than in src/
// because we do not ship a disk controller that cannot read a disk.
//
// What it proves, and why each one matters:
//
//   1. The PROM shadows RAM for READS.
//   2. Writes go straight through to the RAM underneath, at the SAME addresses,
//      WHILE the PROM is shadowing them -- because the Tarbell gates PHANTOM* with
//      the read strobe and simply does not pull the pin during a write cycle. The
//      memory board needs NO strap for this: it never sees PHANTOM* asserted on a
//      write, so it answers the write like any other. This is the whole reason
//      `phantom = read` exists, and it is a real card, not a convenience.
//   3. The very cycle that reads an address with A5 high is ALREADY un-shadowed --
//      the release is combinational, off the address bus. If it were not, the
//      bootstrap's first fetch outside the PROM would float to 0xFF and the machine
//      would never boot.
//   4. Once released it STAYS released, even for a read back down at 0x0000. That
//      is what the flip-flop is for; without it, the loaded sector's own data reads
//      below 0x20 would fall back into the PROM.
//   5. POC* re-arms it. That is how you boot the machine twice.

#include "boards/s100-memory.h"
#include "core/bus.h"
#include "test.h"

using namespace altair;

namespace {

// A5 -- ONE ADDRESS LINE. Not "address >= 32". The Tarbell decodes the wire, so
// 0x0040 does NOT release it (bit 5 is clear) even though 0x40 > 0x20. Rewriting
// this as a range compare would be a plausible, tidy, wrong simulation of a wire,
// and the last case below is here to catch anyone who tries.
constexpr uint16_t kA5 = 0x0020;

class TarbellBoot : public Board {
public:
    std::string type() const override { return "tarbell"; }
    std::vector<Property> properties() override { return {}; }

    // PHANTOM* IS HELD, LIKE AN INTERRUPT. POC* asserts it and it stays asserted --
    // on reads AND on writes -- until A5 takes it away. It is NOT gated with the
    // read strobe (Patrick, from the schematic, 2026-07-12), and this card has no
    // opinion whatever about what a write should do. That is the MEMORY card's
    // jumper: honors_phantom = "read" means it stops answering reads and keeps
    // answering writes, which is how the bootstrap's sector lands in the RAM that
    // this PROM is shadowing.
    //
    // The release is COMBINATIONAL, and it has to be: the very cycle that first
    // sees A5 must ALREADY find PHANTOM* gone, or the memory board would not answer
    // that read and the fetch at 0x0020 would come back 0xFF. The flip-flop below
    // is what makes it STAY gone.
    bool assertsPhantom(const BusCycle& c) const override {
        if (!armed_) return false;
        if (c.type == Cycle::MemRead && (c.addr & kA5)) return false;  // released, now
        return true;
    }

    // The same gate feeds the PROM's own output drivers, so when PHANTOM* goes away
    // the PROM stops answering too -- otherwise it would contend with the RAM it was
    // shadowing a cycle ago. It only ever drives a READ; a PROM cannot latch a write.
    bool decodes(const BusCycle& c) const override {
        return c.type == Cycle::MemRead && assertsPhantom(c);
    }

    uint8_t read(const BusCycle& c) override { return prom_[c.addr & 0x1F]; }

    // A5 IS AN ADDRESS LINE, so this card answers 0000-001F and NOT 0020-003F --
    // two different answers inside page 0. It cannot be cached per page, and it
    // says so; the bus then serves page 0 from the exact two-pass path.
    bool decodeIsPageUniform() const override { return false; }

    // This card WATCHES cycles it does not answer. Almost none do.
    bool wantsSnoop() const override { return true; }

    // Clocked. The one flip-flop -- and it changes what this card decodes FOREVER,
    // so the backplane is told the wiring moved. This is the latched half; the
    // combinational half is up in assertsPhantom(), and only the latch is news.
    void snoop(const BusCycle& c) override {
        if (c.type == Cycle::MemRead && (c.addr & kA5) && armed_) {
            armed_ = false;
            decodeChanged();
        }
    }

    void reset(Reset r) override {
        if (r == Reset::PowerOn && !armed_) {
            armed_ = true;  // POC* enables PHANTOM*
            decodeChanged();
        }
    }

    bool armed() const { return armed_; }
    uint8_t prom_[32] = {};

private:
    bool armed_ = true;
};

// The memory card the Tarbell boots against. THE JUMPER IS THE POINT: honors_phantom
// = "read" means it takes itself off the bus for READS while the PROM shadows it,
// and keeps answering WRITES -- so the bootstrap's sector lands in the RAM beneath
// the PROM. The Tarbell holds PHANTOM* on writes too; it is this strap, and not the
// asserting card, that decides a write still gets through.
MemoryBoard* ram64k(const char* id) {
    auto* m = new MemoryBoard();
    m->id = id;
    std::string err;
    CHECK(setProperty(*m, "fill", "zero", err), "fill=zero");
    CHECK(setProperty(*m, "honors_phantom", "read", err),
          "the RAM under a shadow is strapped: off for reads, answering writes");
    Region r;
    r.kind = RegionKind::Ram;
    r.at = 0x0000;
    r.size = 0x10000;
    CHECK(m->addRegion(r, err), "64K of RAM under the PROM");
    m->power();
    return m;
}

} // namespace

void test_phantom() {
    SECTION("PHANTOM* -- the Tarbell boot sequence");

    Bus bus;
    // THE HARD CASE, SO CHECK THE CACHE AGAINST THE TRUTH ON EVERY CYCLE. The
    // Tarbell decodes on A5 (so its pages are not page-uniform) and it changes its
    // decode mid-run (the flip-flop). If the bus's cached tables can be wrong
    // anywhere, they can be wrong here. This re-derives every decode the slow way
    // and aborts on the first disagreement.
    bus.setVerify(true);

    auto* tar = new TarbellBoot();
    auto* mem = ram64k("mem0");
    tar->id = "tar";
    for (int i = 0; i < 32; i++) tar->prom_[i] = (uint8_t)(0xC0 + i);
    bus.attach(tar);
    bus.attach(mem);
    tar->reset(Reset::PowerOn);

    // 1. The PROM shadows RAM for reads.
    CHECK(bus.memRead(0x0000) == 0xC0, "0000 reads the boot PROM, not RAM");
    CHECK(bus.memRead(0x001F) == 0xDF, "001F reads the boot PROM, not RAM");

    // 2. Writes fall through to the RAM UNDERNEATH -- at the very addresses the
    //    PROM is shadowing. This is the bootstrap depositing its sector.
    bus.memWrite(0x0000, 0x76);
    bus.memWrite(0x001F, 0x77);
    CHECK(bus.memRead(0x0000) == 0xC0, "reads still come back from the PROM");
    CHECK(bus.memRead(0x001F) == 0xDF, "reads still come back from the PROM");
    CHECK(tar->armed(), "no A5 read yet, so nothing has released PHANTOM*");
    CHECK(mem->storeAt(0x0000) == 0x76, "...but the byte DID land in the RAM underneath");
    CHECK(mem->storeAt(0x001F) == 0x77, "...but the byte DID land in the RAM underneath");

    // 2b. A WRITE WITH A5 SET DOES NOT RELEASE ANYTHING, AND THE REAL PROM PROVES IT.
    //
    // TARPROM.ASM (roms/TARBELL-SD) loads the sector with HL starting at 0000:
    //
    //     RLOOP:  IN   DDATA      ; read a byte
    //             MOV  M,A        ; put into memory   <-- WRITES 0000, 0001, ... 007F
    //             INX  H
    //             JMP  RLOOP
    //
    // So the loop writes straight through 0x0020 and beyond while still FETCHING
    // ITSELF from the PROM at 000C-0018. If a write with A5 high dropped PHANTOM*,
    // the shadow would fall away in the middle of the load, the next fetch at RLOOP
    // would come from RAM -- which by then holds sector bytes, not the loader -- and
    // the bootstrap would eat itself. The trigger is a memory READ. Only a read.
    bus.memWrite(0x0040, 0x99);
    CHECK(tar->armed(), "a WRITE with A5 set does NOT release it -- the sector load "
                        "walks through 0020+ while still fetching from the PROM");
    CHECK(bus.memRead(0x0000) == 0xC0, "...so the PROM is still shadowing, mid-load");

    // 3. A5 high on a READ: released on THIS CYCLE, combinationally.
    //
    // AND THE PROM PROVES THIS TOO. Its last instruction is `JZ 07DH`, and 0x7D is
    // 0111_1101 -- A5 IS SET. The jump into the sector it just loaded is ITSELF the
    // first read with A5 high. If the flip-flop only took effect on the NEXT cycle,
    // that fetch would still be shadowed, would come back from the PROM, and no
    // Tarbell would ever have booted. The release is armed by the address of the
    // code it is jumping to.
    bus.memWrite(0x0020, 0x42);
    CHECK(bus.memRead(0x0020) == 0x42, "the A5 read itself is already un-shadowed");
    CHECK(!tar->armed(), "and the flip-flop has now latched PHANTOM* off");

    // The real jump target, for the same reason and with the real number.
    tar->reset(Reset::PowerOn);
    bus.memWrite(0x007D, 0xC3);
    CHECK(tar->armed(), "re-armed, and the write did not disturb it");
    CHECK(bus.memRead(0x007D) == 0xC3, "JZ 07DH lands in RAM: 0x7D has A5 set");
    CHECK(!tar->armed(), "...and that very fetch is what released the PROM");

    // 4. It stays released, back down inside what used to be the PROM's range.
    CHECK(bus.memRead(0x0000) == 0x76, "0000 now reads the RAM we wrote in step 2");
    CHECK(bus.memRead(0x001F) == 0x77, "the PROM does not come back for low reads");

    // The PROM stopped decoding when it stopped shadowing, so it is not contending
    // with the RAM it used to cover. A shadow that outlives its welcome is a fault.
    CHECK(bus.drain().empty(), "no contention: the released PROM stops driving too");

    // 5. POC* re-arms. This is how you boot the machine a second time.
    tar->reset(Reset::PowerOn);
    CHECK(bus.memRead(0x0000) == 0xC0, "POC* re-enables PHANTOM* and the PROM is back");

    // --- A5 is a WIRE, not a threshold. ---
    Bus b2;
    b2.setVerify(true);
    auto* t2 = new TarbellBoot();
    auto* m2 = ram64k("mem1");
    t2->id = "tar";
    b2.attach(t2);
    b2.attach(m2);
    t2->reset(Reset::PowerOn);
    b2.memRead(0x0040);
    CHECK(t2->armed(), "0040 is above the PROM but A5 is CLEAR -- still shadowing");
    b2.memRead(0x0060);
    CHECK(!t2->armed(), "0060 has A5 set -- released");

    delete tar;
    delete mem;
    delete t2;
    delete m2;
}
