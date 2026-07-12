#pragma once
//
// The `memory` board -- docs/boards/memory.md.
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

    // ---- reflection ----
    std::vector<Property> properties() override;

    // ---- RAW: the PROM burner (DESIGN.md 10.2) ----
    size_t rawSize() const override { return store_.size(); }
    // Callers bounds-check with rawSize(); reading past the store is their bug.
    // It does NOT return 0xFF -- 0xFF is the BUS's floating value and has no
    // business in a board (DESIGN.md 4.6.1).
    uint8_t rawRead(size_t off) const override {
        return off < store_.size() ? store_[off] : 0x00;
    }
    bool rawWrite(size_t off, uint8_t v) override {
        if (off >= store_.size()) return false;
        store_[off] = v;
        return true;
    }

    std::vector<MapEntry> memMap() const override;
    std::vector<MapEntry> ioMap() const override;

    std::vector<std::string> subUnitTables() const override { return {"region"}; }
    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;

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
    std::vector<std::string> takeLog();

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

    bool honorsPhantom_ = true;
    PhantomAssert phantom_ = PhantomAssert::All;

    BankType bankType_ = BankType::None;
    int banks_ = 1;
    int bank_ = 0;

    Fill fill_ = Fill::Random;
    uint64_t seed_ = 1;

    std::vector<std::string> log_;
};

} // namespace altair
