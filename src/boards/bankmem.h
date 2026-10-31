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
// ONE MODEL, FIVE DECODERS. Every one of these cards reduces to the same shape: the
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
//   expandoram2  SD Systems ExpandoRAM II -- port FF, LOW NIBBLE = page. The real
//                board's 82S130 PROM spreads each page over 16K banks on several
//                boards (Fig 2-4, reference/SD Systems ExpandoRAM II.md 4). We keep
//                one segment per page for the whole subsystem: software sees the
//                same pages, and a page with no RAM leaves the switched window
//                empty. ex48's page A is one 16K cell at 8000-BFFF. With
//                partition=none (one board of 4164s) page = bank is an APPROXIMATION:
//                the manual gives no map for that board.
//   expandoram3  SD Systems ExpandoRAM III -- port FF, LOW NIBBLE = page. The PROM map
//                of Fig 2-2 is built cell by cell: each board is 4 banks x 4 RAM
//                quarters of 16K, and the PROM picks which cell answers each CPU
//                quarter for each page (reference/SD Systems ExpandoRAM III.md 4).
//                One or two boards (ram=256|512), config=cpm|oasis, and the S3 bank
//                enables. A page with no cell leaves the switched window empty.
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
    enum class Card { Vector, Cromemco, Northstar, Expandoram2, Expandoram3 };
    enum class Fill { Zero, Random };
    // ExpandoRAM II common-memory partition (see the header note). None = whole 64K
    // plane; Ex48 = 48K banked + 16K common @ C000; Ex32 = 32K banked + 32K common @ 8000.
    enum class Partition { None, Ex48, Ex32 };
    // PHANTOM* honoring, the `memory` board's jumper for a RAM card that sits UNDER a boot
    // PROM (see the header note and s100-memory.h). None = never step aside; Read = step
    // aside for reads but keep taking writes (so a cold-boot loader's writes fall through
    // to this RAM while the PROM shadows reads); All = step aside for reads and writes.
    // Class-scoped so it does not collide with the `memory` board's altair::PhantomHonor.
    enum class PhantomHonor { None, Read, All };
    // ExpandoRAM III S3 switch 5: which PROM map is in force. Cpm = pages in 0000-BFFF,
    // common C000-FFFF; Oasis = pages in 4000-FFFF, common 0000-3FFF.
    enum class Config { Cpm, Oasis };

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
    //   expandoram3  page index          (each segment is one 16K PROM cell)
    struct Segment {
        uint16_t lo = 0x0000, hi = 0xFFFF;   // inclusive window
        std::vector<uint8_t> ram;            // hi - lo + 1 bytes
        bool enabled = false;                // driving the bus right now?
        bool resetEnabled = false;           // enable state after POC/RESET
        bool common = false;                 // the shared always-live region (partition!=none)?
        uint16_t key = 0;                    // card-specific selector (see above)
        // expandoram3 only: where the cell is -- board x 4 + bank (the bit of
        // bank_enable that gates it) and the RAM quarter (which 16K of the 64K bank).
        uint8_t bank = 0, quarter = 0;
    };

    // Card defaults -- port, plane count, and the segment set. Called when `card` or
    // `banks` changes; ram is (re)filled from the current fill/seed.
    void rebuildSegments();
    void fillSegments();
    void applyReset();                   // set each segment's enabled to its reset default

    // The card's decode: recompute segment `enabled` flags from the select byte.
    void select(uint8_t data);
    // expandoram3: set every cell's enable from the page and the bank enables.
    // Returns false if the PROM has no cell for that page (the window is then empty).
    bool selectEram3Page(int page);
    void rebuildEram3();
    int  eram3Pages() const;             // pages carried: 5 (1 board) or 10 (2 boards)

    // Do I take myself off the bus for THIS cycle when another board pulls PHANTOM*?
    // (See PhantomHonor.) bankmem carries no ROM, so it never asserts phantom itself --
    // there is no self-shutoff hazard the `memory` board has to guard against.
    bool honors(const BusCycle& c) const;

    // The enabled segment covering `a`, or nullptr. Reports intra-board contention
    // (two enabled segments over one address -- a Cromemco bus fight) once.
    Segment*       owner(uint16_t a);
    const Segment* owner(uint16_t a) const;

    int  cardMaxBanks() const;           // vector 8, cromemco 8, northstar 6, eram2 10/11, eram3 10
    uint8_t cardDefaultPort() const;     // 40 / 40 / C0 / FF / FF

    // Partition geometry (expandoram2 common memory). base 0 == no common region.
    uint16_t partitionCommonBase() const;   // None 0, Ex48 C000, Ex32 8000
    uint32_t partitionBankedSize() const;   // bytes per switchable bank window
    uint32_t partitionCommonSize() const;   // bytes of shared common region (0 if None)
    // The RAM a subsystem of n banks holds, common included. ex48's page A (the 11th
    // page) is one 16K cell, not a 48K bank.
    uint32_t ramBytes(int n) const;

    Card card_ = Card::Vector;
    Partition partition_ = Partition::None;
    PhantomHonor honors_ = PhantomHonor::None;  // config jumper; default off (no PROM overlay)
    uint8_t port_ = 0x40;
    int wantBanks_ = 8;                  // requested plane/bank/page count (config)
    std::vector<Segment> segs_;
    uint8_t latch_ = 0;                  // last byte written to the select port

    // vector: the addresses each plane answers. A 64K board starts at 0000 and area D trims its
    // top. A window that starts higher stands for the 16K static boards, which the manual mixes
    // with it and which carry the same bank select.
    uint16_t windowLo_ = 0x0000;
    uint16_t windowHi_ = 0xFFFF;

    // expandoram3 config (S3). bankEnable_ bit = board x 4 + bank, set = switch ON.
    Config config_ = Config::Cpm;
    uint8_t bankEnable_ = 0xFF;
    int eram3Boards_ = 1;

    Fill fill_ = Fill::Random;
    uint64_t seed_ = 1;

    std::vector<std::string> log_;
};

} // namespace altair
