#pragma once
//
// CompuPro M-Drive/H -- a 512K RAM disk on two I/O ports (board 197B). See
// reference/CompuPro M-Drive Manual.md; the section numbers below are that file's.
//
// THE PORTS (§2), at an even base (default C6, S1-1..7 = A7-A1):
//       C6  IN   the byte at the address        OUT  the byte to the address
//       C7  IN   (not decoded: the bus floats)  OUT  one byte into the address counter
// Each data access, read or write, adds one to the address afterwards.
//
// THE ADDRESS (§3) is a 22-bit counter wired as a shift register on load. Each OUT to C7
// shifts it up one byte and puts the new byte at the bottom, so three OUTs load it HIGH BYTE
// FIRST, and only bits 0-5 of the first byte are kept. It wraps from 3FFFFF to 000000.
//
// ONE ENTRY IS THE WHOLE SET. Up to eight cards share the two ports; S1-8..10 give each its
// cascade number, which is MA21-MA19. Every card loads every address byte, and only the card
// whose number matches MA21-MA19 drives the data bus. Two entries on one port pair would be
// bus contention to this simulator (decodes() cannot look at the counter), so `boards = n`
// fits cards 0..n-1. An address on a card that is not there reads FF -- nothing drives the
// bus -- and a write to it goes nowhere. CompuPro's BIOS counts the cards by exactly that.
//
// RESET (§7): the counters' CLR is tied high and DRAM keeps its contents, so neither reset
// does anything. MFORM formats only a drive it finds unformatted, so the data must survive.
// POWER refills the RAM from `fill`/`seed` and sets the counter to 0.
//
// WHAT IS NOT MODELED, AND WHY EACH IS SAFE TO LEAVE OUT:
//   - AN IN FROM THE ADDRESS PORT. The PAL equations (197P-1) are not printed, and the
//     schematic has no read path from the counters, so the port is not decoded.
//   - WAIT STATES. The card holds RDY until the 8203 answers; the manual gives no count.
//   - PWRFAIL* through J1 into the PAL. The PAL equations are not printed.

#include "core/board.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class MDriveHBoard : public Board {
public:
    static constexpr uint32_t kCardBytes = 512u * 1024u;  // one card: 8 x 64K rows
    static constexpr uint32_t kAddrMask  = 0x3FFFFF;      // the 22-bit counter
    static constexpr int      kMaxBoards = 8;             // S1-8..10: three bits

    MDriveHBoard();

    std::string type() const override { return "mdriveh"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    void reset(Reset) override {}  // the counter and the RAM both survive (§7)
    void power() override;

    std::vector<Property>    properties() override;
    std::vector<MapEntry>    ioMap() const override;
    std::vector<std::string> statusLines() const override;

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // For the tests: the counter, and the byte at an address (FF where no card is fitted).
    uint32_t address() const { return addr_; }
    uint8_t  peekRam(uint32_t a) const { return a < ram_.size() ? ram_[a] : 0xFF; }

private:
    enum class Fill { Zero, Random };

    // Size the store to `boards_` cards. Cards already fitted keep their bytes; a new card
    // is filled per `fill`/`seed`.
    void resize(int boards);
    void fillCard(int card);

    uint8_t  port_   = 0xC6;
    int      boards_ = 1;
    uint32_t addr_   = 0;
    Fill     fill_   = Fill::Random;
    uint64_t seed_   = 1;
    std::vector<uint8_t> ram_;
};

}  // namespace altair
