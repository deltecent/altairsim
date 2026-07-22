#pragma once
//
// The `memory` board -- docs/boards/s100-memory.md.
//
// ONE BOARD IS ONE CARD. A card carrying 48K of RAM and three PROM sockets is
// one board, so this class holds a LIST OF REGIONS, each `ram` or `rom`. That
// collapse is what makes an empty PROM socket and an unpopulated RAM page the
// same case: neither is covered by a region, so the board does not decode it,
// nothing drives the bus, and it reads 0xFF.
//
// THERE IS NO WRITE-PROTECT ON THIS BOARD, and there must never be one. A `rom`
// region simply DOES NOT DECODE A WRITE CYCLE -- it does not reject the write,
// or ignore it, or log it. It never answers. Everything else (the byte
// vanishing, or falling through to RAM on another card) is emergent from what
// else is on the bus, and needs no rule anywhere.

#include "core/board.h"
#include "core/hex.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace altair {

enum class RegionKind { Ram, Rom };
enum class PhantomAssert { None, Read, All };

// WHAT I DO WHEN SOMEONE ELSE PULLS PHANTOM* -- and it has the same three
// positions as the assert strap, because it is the same kind of jumper.
//
//   none  I ignore the pin. I keep driving. (If a ROM is shadowing me, that is
//         two boards driving one cycle, and the bus reports the contention.)
//   read  I switch off for READS and keep answering WRITES. THIS IS THE TARBELL
//         CASE: its PROM shadows my reads while the byte still lands in my RAM
//         underneath, which is how the bootstrap deposits a sector into the very
//         addresses the PROM is covering.
//   all   I switch off entirely for the cycle.
//
// The read/write distinction lives HERE, on the HONORING board, and an earlier
// version of this file said in bold that it must never do so -- that the
// asserting card should gate PHANTOM* with its read strobe instead. That was
// wrong, and it was wrong because it was reasoned rather than sourced. PATRICK
// READ THE TARBELL SCHEMATIC (2026-07-12): the card holds PHANTOM* asserted like
// an interrupt, continuously, from RESET until A5 releases it -- it does not gate
// the pin with MEMR at all. The memory card is what is strapped to ignore it on
// writes.
enum class PhantomHonor { None, Read, All };
enum class Fill { Zero, Random };

enum class BankType { None, Eram, Vram, Cram, Hram, B810 };

struct BankSpec {
    const char* name;
    const char* card;
    uint8_t port;
    int banks;
    bool oneHot;
    uint8_t mask;
};
const BankSpec& bankSpec(BankType t);
bool parseBankType(const std::string& s, BankType& out);

struct Region {
    RegionKind kind = RegionKind::Ram;
    uint16_t at = 0;
    uint32_t size = 0;      // bytes, rounded up to a 0x100 page
    std::string mount;      // rom only: a host path, or "builtin:<name>"

    // ...AND WHERE THAT PATH ACTUALLY LEADS. The two differ only when the ROM was
    // named by a machine file in another directory (core/paths.h): `mount` is what
    // the file said, `mountFile` is where we found it.
    //
    // THIS CARD IS THE ONLY ONE THAT HAS TO KEEP BOTH, and the reason is power():
    // a ROM region is re-read from the host on EVERY power cycle, long after the
    // loader has packed up and gone home. A disk or a tape is opened once and the
    // handle survives, so those cards resolve and forget. This one cannot.
    std::string mountFile;

    std::string describe() const;
};

class MemoryBoard : public Board {
public:
    MemoryBoard() {
        for (int& o : owner_) o = -1;  // every page unpopulated until a region says so
    }
    std::string type() const override { return "memory"; }

    // ---- config ----
    bool addRegion(Region r, std::string& err);
    const std::vector<Region>& regions() const { return regions_; }

    // ---- bus (DESIGN.md 4.2) ----
    bool assertsPhantom(const BusCycle& c) const override;
    bool decodes(const BusCycle& c) const override;

    // Do I switch off for this cycle when someone else pulls PHANTOM*?
    bool honors(const BusCycle& c) const;
    uint8_t read(const BusCycle& c) override;
    void write(const BusCycle& c) override;

    // RAM and ROM can always be looked at without being disturbed -- that is what
    // makes them memory. (An unpopulated page is not covered by a region, so the
    // bus never asks this board about it in the first place.)
    bool peek(uint16_t addr, uint8_t& out) const override {
        if (!owner(addr)) return false;
        out = store_[plane(addr)];
        return true;
    }

    // ---- lifecycle (DESIGN.md 6) ----
    void reset(Reset r) override;
    void power() override;

    // ---- state: SNAPSHOT / RESTORE (DESIGN.md 13) ----
    //
    // The store (RAM, and any ROM plane the guest or the burner mutated) and the
    // selected bank travel. The regions, straps, fill policy and mount paths are
    // config and are already correct in a matching machine. RESTORE writes store_
    // directly -- it must NOT go through power(), which re-fills RAM and re-reads
    // ROM images and would wipe exactly the state being restored.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // ---- reflection ----
    std::vector<Property> properties() override;

    // ---- The PROM burner (DESIGN.md 10.2) ----
    //
    // peek()'s mirror, and deliberately its exact shape: a BUS address in, the live
    // bank applied, and the board's own owner() deciding whether it is ours to answer
    // for. The ONLY difference is that this one writes, and that a ROM region does not
    // stop it -- which is the entire point. A bus write cannot program a PROM (§4.2),
    // because on real hardware it cannot; you pull the chip and put it in a programmer,
    // and that is not a bus operation. This is that programmer.
    //
    // It takes a bus address and not a store offset ON PURPOSE. Every address the
    // operator types means the same thing everywhere in this monitor -- 0000-FFFF, what
    // the CPU would see. The store offset (bank * 64K + addr) is this board's private
    // business and stays that way; to reach another bank, SELECT it (SET mem0 bank=3)
    // and use ordinary addresses, exactly as the guest must.
    //
    // False means "not mine" -- this board does not decode that address. The caller
    // says so; this does not guess and does not silently drop the byte.
    bool poke(uint16_t addr, uint8_t v) {
        if (!owner(addr)) return false;
        store_[plane(addr)] = v;
        return true;
    }

    // ---- The store, flat, by offset. NOT AN ADDRESS SPACE ANYONE TYPES. ----
    //
    // This is the card's own view of its own silicon: `banks_ * 64K` of it, with bank 3
    // simply BEING offset 0x30000. It exists so that this board's invariants can be
    // asserted from outside -- that a bank really is a plane, that random fill really is
    // random -- and for nothing else. Nothing the operator or an agent can reach comes
    // through here.
    //
    // It is deliberately NOT on Board. It was, once, as rawRead/rawWrite, and the price
    // was a whole second address space bolted to every card in the machine to serve one.
    // If you are reaching for this from anything but a test, you want peek() or poke(),
    // which speak bus addresses like the rest of the program.
    size_t storeSize() const { return store_.size(); }
    // Past the end is a CALLER bug -- bounds-check with storeSize(). It does NOT answer
    // 0xFF: that is the BUS's floating value, and a board has no business forging it
    // (DESIGN.md 4.6.1), least of all where the bus cannot see.
    uint8_t storeAt(size_t off) const { return off < store_.size() ? store_[off] : 0x00; }

    std::vector<MapEntry> memMap() const override;
    std::vector<MapEntry> ioMap() const override;

    std::vector<std::string> subUnitTables() const override { return {"region"}; }
    std::vector<Property>    subUnitProperties(const std::string& table) const override;
    std::vector<SubUnit>     subUnits() const override;

    // A ROM region is a SOCKET, and a socket is a unit: `MOUNT mem0:rom0 dbl.hex`.
    //
    // A RAM region is NOT a unit. There is no chip to pull and nothing to mount
    // into it, and offering `mem0:ram0` would be offering something that can only
    // ever fail. (To put bytes in RAM you LOAD them, exactly as the CPU would.)
    std::vector<UnitDef> units() const override;
    bool mount(const std::string& unit, const std::string& path, bool ro,
               std::string& err) override;
    bool unmount(const std::string& unit, std::string& err) override;

    // "rom1" -> its index in regions_. regions_.size() means this card has no such
    // unit -- the caller reports it; this does not guess.
    size_t romRegionIndex(const std::string& unit) const;

    // Load an image into the store, behind the bus. Used by power-up ROM
    // loading and by `LOAD ... RAW`.
    bool blit(const Image& img, std::string& err);

    // Messages the board wants said out loud (a bank select it could not
    // decode, a ROM that failed to load). Drained by the monitor.
    std::vector<std::string> drainLog() override;

protected:
    // Reached only through Board::loadSubUnit(), which has already vetted the keys.
    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;

private:
    static constexpr uint32_t kPage = 0x100;
    static size_t page(uint16_t a) { return a >> 8; }
    size_t plane(uint16_t a) const { return (size_t)bank_ * 0x10000 + a; }

    // The page table stores an INDEX, not a Region* -- pointers into regions_
    // dangle the moment the vector reallocates, which it does on the second
    // region of every combo card.
    const Region* owner(uint16_t a) const {
        int i = owner_[page(a)];
        return i < 0 ? nullptr : &regions_[(size_t)i];
    }

    void rebuildPageMap();
    bool loadRomRegion(size_t idx, std::string& err);
    void fillRegion(size_t idx);
    void fillRam();
    void growStore();

    std::vector<Region> regions_;
    int owner_[256];              // page table: which region covers each page, or -1
    std::vector<uint8_t> store_;  // banks_ * 64K. Bank 3 simply IS offset 0x30000.

    PhantomHonor honors_ = PhantomHonor::All;
    PhantomAssert phantom_ = PhantomAssert::All;

    BankType bankType_ = BankType::None;
    int banks_ = 1;
    int bank_ = 0;

    Fill fill_ = Fill::Random;
    uint64_t seed_ = 1;

    std::vector<std::string> log_;
};

} // namespace altair
