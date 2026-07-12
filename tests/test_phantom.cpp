// The Tarbell boot sequence -- the card that settles PHANTOM* (docs/boards/tarbell.md).
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

#include "boards/memory.h"
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

    // Combinational. POC* armed us, A5 releases us, the flip-flop keeps us released.
    bool assertsPhantom(const BusCycle& c) const override {
        if (!armed_) return false;
        if (c.type != Cycle::MemRead) return false;  // gated with the read strobe
        return (c.addr & kA5) == 0;
    }

    // The same condition gates the PROM's own output drivers, so the moment
    // PHANTOM* is released the PROM stops answering as well -- otherwise it would
    // contend with the RAM it was shadowing a cycle ago.
    bool decodes(const BusCycle& c) const override { return assertsPhantom(c); }

    uint8_t read(const BusCycle& c) override { return prom_[c.addr & 0x1F]; }

    // Clocked. The one flip-flop.
    void snoop(const BusCycle& c) override {
        if (c.type == Cycle::MemRead && (c.addr & kA5)) armed_ = false;
    }

    void reset(Reset r) override {
        if (r == Reset::PowerOn) armed_ = true;  // POC* enables PHANTOM*
    }

    bool armed() const { return armed_; }
    uint8_t prom_[32] = {};

private:
    bool armed_ = true;
};

MemoryBoard* ram64k(const char* id) {
    auto* m = new MemoryBoard();
    m->id = id;
    std::string err;
    CHECK(setProperty(*m, "fill", "zero", err), "fill=zero");
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
    CHECK(mem->rawRead(0x0000) == 0x76, "...but the byte DID land in the RAM underneath");
    CHECK(mem->rawRead(0x001F) == 0x77, "...but the byte DID land in the RAM underneath");

    // 3. A5 high: released on THIS CYCLE, combinationally. If the release waited a
    //    cycle, this read would float to 0xFF and no Tarbell would ever have booted.
    bus.memWrite(0x0020, 0x42);
    CHECK(bus.memRead(0x0020) == 0x42, "the A5 read itself is already un-shadowed");
    CHECK(!tar->armed(), "and the flip-flop has now latched PHANTOM* off");

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
