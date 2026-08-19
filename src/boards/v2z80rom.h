#pragma once
//
// S100Computers V2 Z80 CPU board -- ONLY its onboard paged monitor EEPROM
// (reference/v2-z80-cpu-board.md).
//
// The board id is `v2z80rom`, not `v2z80`, deliberately: this class is NOT a full V2 Z80 CPU
// board, only the paged monitor-EEPROM feature of that card. The real card has no onboard RAM
// (it is a CPU + monitor-EEPROM board); the Z80 processor of that card is a SEPARATE board here
// (mits-z80cpu), and RAM comes from a RAM board. This class models only the card's onboard
// monitor EEPROM. Pair `v2z80rom` with a Z80 CPU board and a RAM board in
// a machine file, and cold-start the monitor with the operator's own keystroke --
// `startup = ["RUN F000"]` (DESIGN.md 10.0; the `amon` machine does the same for its F000
// monitor). This board does NOT model the card's Power-On-Jump: modeling that reset-vectoring
// buys nothing over RUN F000 and is not this board's identity -- the paged EEPROM is.
//
// THE PAGED EEPROM. A single 8K 28C64 sits at CPU addresses F000-FFFF -- a 4K window. The chip
// holds two 4K "pages" and only one is visible at a time, selected by the A12 line the board
// drives from a port bit:
//
//   OUT D3H bit 1   0 -> LOW  page (builtin:master0, chip offset 0000-0FFF)
//                   1 -> HIGH page (builtin:master1, chip offset 1000-1FFF)
//   OUT D3H bit 0   1 -> INACTIVATE the EEPROM (RAM shows through F000-FFFF); 0 -> enabled
//
// D3H is a write-only latch (the board does not answer IN D3H). Its bit 2 is the companion
// memory-manager's overlap bit and is ignored here -- altairsim does not model that MMU (the
// Dual SD boot target is non-banked CP/M 3 in a flat 64K). At reset the latch powers up 0x00:
// EEPROM enabled, low page.
//
// RAM UNDER ROM. While enabled the EEPROM shadows RAM in its window: it asserts PHANTOM* for
// READS in F000-FFFF (so a 64K RAM board steps aside and the ROM byte wins) but never decodes a
// write (writes fall through to the RAM underneath), exactly like the Turnkey boot PROM. When
// the guest inactivates the EEPROM (OUT D3H bit 0 = 1) the board leaves F000-FFFF entirely and
// the RAM shows through -- which is how CP/M reclaims the top page after it has booted.

#include "core/board.h"
#include "core/roms.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class V2Z80RomBoard : public Board {
public:
    V2Z80RomBoard();

    std::string type() const override { return "v2z80rom"; }

    // ---- bus ----
    bool    decodes(const BusCycle&) const override;
    uint8_t read(const BusCycle&) override;
    void    write(const BusCycle&) override;
    bool    assertsPhantom(const BusCycle&) const override;
    bool    peek(uint16_t addr, uint8_t& out) const override;

    // ---- lifecycle ----
    void reset(Reset) override;
    void power() override;
    void configChanged() override;

    // ---- reflection ----
    std::vector<Property> properties() override;
    std::vector<MapEntry> memMap() const override;
    std::vector<MapEntry> ioMap() const override;
    std::vector<std::string> drainLog() override;

    // ---- SNAPSHOT / RESTORE (DESIGN.md 13) ----
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

private:
    static constexpr uint16_t kWindowBase = 0xF000;   // the EEPROM's 4K CPU window
    static constexpr uint32_t kPageSize   = 0x1000;   // 4K per page
    static constexpr uint32_t kEepromSize = 0x2000;   // 8K == two pages

    // ---- config / straps (from TOML; not serialized) ----
    uint8_t port_ = 0xD3;   // page/ROM-control latch (write-only)

    // ---- the EEPROM store: two 4K pages, re-read from the built-ins on power() ----
    uint8_t rom_[kEepromSize];

    // ---- runtime latches (travel in a snapshot) ----
    bool highPage_   = false;   // D3H bit 1: false = low page, true = high page
    bool romEnabled_ = true;    // D3H bit 0: false once the guest inactivated the EEPROM

    // The window is the whole top 4K (F000-FFFF); a uint16_t address cannot exceed FFFF, so the
    // upper bound is implicit (spelling it out warns tautological, and reds -Werror CI).
    bool inWindow(uint16_t a) const { return a >= kWindowBase; }
    uint8_t romByte(uint16_t a) const {
        return rom_[(highPage_ ? kPageSize : 0u) + (uint32_t)(a - kWindowBase)];
    }
    void loadRoms();

    void say(std::string s) { log_.push_back(std::move(s)); }
    std::vector<std::string> log_;
};

} // namespace altair
