#pragma once
//
// The `bankmem` board -- S-100 bank-switched RAM (docs/boards/bankmem.md).
//
// WHY THIS IS NOT THE `memory` BOARD. Bank switching used to be a `bank_type=`
// strap on `memory`, with one parameterized encoding ({port, banks, one-hot?,
// mask}) taken from SIMH `s100_bram.c`. `docs/devguide/banked-ram.md` audited that
// against the period manuals and found it wrong for four of five cards: the real
// cards are not one mechanism with a parameter -- they are genuinely different
// decoders (DESIGN.md 4.3, "the board owns its decode, or the model is a lie"). So
// banking left `memory` (which is now purely plain RAM/ROM, and thereby also the
// honest model of the ExpandoRAM I, which has no I/O port at all) and lives here,
// where each card owns its own decode.
//
// ONE MODEL, FOUR DECODERS. Every one of these cards reduces to the same shape: the
// board holds a set of SEGMENTS, each a slice of RAM with an address window and an
// ENABLED flag. A memory cycle is answered by whichever enabled segment's window
// covers the address; the write-only select port toggles those flags per the card's
// own rule. Only the rule differs, and `card=` chooses it:
//
//   vector       Vector Graphic 64K -- port 40, ONE-HOT select-one (1<<n), reset->0.
//                Tolerates 0x41/0x42 (bit 6 ignored) because OASIS writes them.
//   cromemco64kz Cromemco 64KZ / 64KZ-II -- port 40, 8-bit MASK: bit N enables bank
//                N, SEVERAL AT ONCE (OUT 40H,28H = banks 3 and 5). reset->no banks.
//   northstar    North Star HRAM -- port C0: bit 0 = on(0)/off(1), bits 1-7 = a
//                one-hot address of WHICH bank the command toggles. Banks are
//                switched individually (old off, then new on), not selected.
//   expandoram2  SD Systems ExpandoRAM II -- port FF, byte = page index (APPROX --
//                the real board decodes the page through an 82S130 PROM against
//                board-select + address bits, which is not transcribable from the
//                scan (reference/SD Systems ExpandoRAM II.md, DESIGN.md 0.1). We
//                model a binary page-select over 64K planes and SAY SO.)
//
// COMMON MEMORY (expandoram2 `partition=`). Banked CP/M (CP/M 3, SD Systems COSMOS)
// needs a region that is IDENTICAL in every bank -- the resident OS and the bank-switch
// routine itself live there, so it must survive the OUT that changes banks. On the real
// board the 82S130 PROM provides it: the EX-48 part maps the top 16K of every page to the
// same rows, the EX-32 part the top 32K. We model that directly: `partition=ex48` gives a
// 48K banked window (0000-BFFF) under a shared 16K common region (C000-FFFF); `ex32` gives
// 32K/32K; `none` (the default) is the plain whole-64K-plane behavior. The common region is
// one always-live segment every bank sees; only the region below it swaps. This is what lets
// SD Systems banked CP/M 3 (which writes the bank number straight to port FF, common at
// C000) run -- see reference/SD Systems COSMOS.md.
//
// RAM ONLY. None of these carried ROM, so there is no `rom` region here and no
// PHANTOM* role -- what a bank select does to a ROM plane is unknown, and we do not
// guess (the `memory` board carries ROM sockets; use it for that).

#include "core/board.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace altair {

class MemBankBoard : public Board {
public:
    enum class Card { Vector, Cromemco, Northstar, Expandoram2 };
    enum class Fill { Zero, Random };
    // ExpandoRAM II common-memory partition (see the header note). None = whole 64K
    // plane; Ex48 = 48K banked + 16K common @ C000; Ex32 = 32K banked + 32K common @ 8000.
    enum class Partition { None, Ex48, Ex32 };

    MemBankBoard() { rebuildSegments(); }

    std::string type() const override { return "bankmem"; }

    // ---- bus (DESIGN.md 4.2) ----
    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;
    bool    peek(uint16_t addr, uint8_t& out) const override;

    // ---- lifecycle (DESIGN.md 6) ----
    void reset(Reset r) override;
    void power() override;

    // ---- state: SNAPSHOT / RESTORE (DESIGN.md 13) ----
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // ---- reflection ----
    std::vector<Property>    properties() override;
    std::vector<MapEntry>    memMap() const override;
    std::vector<MapEntry>    ioMap() const override;
    std::vector<std::string> statusLines() const override;
    std::vector<std::string> drainLog() override;

    // ---- introspection (the tests and the `active` property read these) ----
    // The number of switchable banks -- the common region (if any) is not one of them.
    int     banks() const;
    // Bit i set if segment i currently drives the bus. For a select-one card this has
    // exactly one bit (or none); for Cromemco it is the live bank mask.
    uint32_t activeMask() const;

private:
    // A slice of RAM with a window and an enable. `key` is the card's own selector:
    //   vector       plane index         (one-hot select-one picks key == n)
    //   cromemco     bank-membership mask (enabled = (key & selectByte) != 0)
    //   northstar    one-hot bit position (1..7; the byte's set bit picks the segment)
    //   expandoram2  page index          (binary select-one picks key == page)
    struct Segment {
        uint16_t lo = 0x0000, hi = 0xFFFF;   // inclusive window
        std::vector<uint8_t> ram;            // hi - lo + 1 bytes
        bool enabled = false;                // driving the bus right now?
        bool resetEnabled = false;           // enable state after POC/RESET
        bool common = false;                 // the shared always-live region (partition!=none)?
        uint16_t key = 0;                    // card-specific selector (see above)
    };

    // Card defaults -- port, plane count, and the segment set. Called when `card` or
    // `banks` changes; ram is (re)filled from the current fill/seed.
    void rebuildSegments();
    void fillSegments();
    void applyReset();                   // set each segment's enabled to its reset default

    // The card's decode: recompute segment `enabled` flags from the select byte.
    void select(uint8_t data);

    // The enabled segment covering `a`, or nullptr. Reports intra-board contention
    // (two enabled segments over one address -- a Cromemco bus fight) once.
    Segment*       owner(uint16_t a);
    const Segment* owner(uint16_t a) const;

    int  cardMaxBanks() const;           // vector 8, cromemco 8, northstar 6, eram2 10
    uint8_t cardDefaultPort() const;     // 40 / 40 / C0 / FF

    // Partition geometry (expandoram2 common memory). base 0 == no common region.
    uint16_t partitionCommonBase() const;   // None 0, Ex48 C000, Ex32 8000
    uint32_t partitionBankedSize() const;   // bytes per switchable bank window
    uint32_t partitionCommonSize() const;   // bytes of shared common region (0 if None)

    Card card_ = Card::Vector;
    Partition partition_ = Partition::None;
    uint8_t port_ = 0x40;
    int wantBanks_ = 8;                  // requested plane/bank/page count (config)
    std::vector<Segment> segs_;
    uint8_t latch_ = 0;                  // last byte written to the select port

    Fill fill_ = Fill::Random;
    uint64_t seed_ = 1;

    std::vector<std::string> log_;
};

} // namespace altair
