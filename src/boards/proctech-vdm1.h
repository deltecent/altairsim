#pragma once
//
// VDM-1 -- Processor Technology Video Display Module. The S-100 card that gives the
// Altair a memory-mapped video terminal: 16x64 characters of composite video,
// scanned out of a 1 KB screen RAM the guest writes into. See
// reference/Processor Technology VDM-1.md and docs/boards/proctech-vdm1.md.
//
// TWO HALVES OF THE BUS, plus a host display.
//
//   MEMORY  -- 1 KB of screen RAM at `base` (default 0xCC00). The guest writes
//              ASCII here exactly as it writes RAM; the card scans it 16x64 and
//              paints it. Byte = row*64 + col; D0-D6 = glyph, D7 = cursor.
//   I/O     -- one port at `port` (default 0xCC). `OUT` latches the scroll (the
//              character row shown at the top; the 16 rows wrap mod 16) and arms a
//              ~0.375 s one-shot; `IN` reads status (D0 = that one-shot, D1 = SCAN
//              ADVANCE, the right-margin flag a flicker-free writer waits on).
//   DISPLAY -- the card renders into a Surface every pump() and present()s it. It
//              never touches SDL: the Display is injected at the composition root
//              (setDisplay), and a headless build gets a NullDisplay, so the card
//              runs and is tested with no window (DESIGN.md 7.4).
//
// THE KEYBOARD IS NOT HERE. The VDM-1's keyboard was a SEPARATE parallel board;
// keystrokes from a host window return through a ByteStream/endpoint, not through
// this card (reference file 3.1.8, DESIGN.md 7.4). So this board is output only.

#include "core/board.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class Display;  // host/display.h -- injected; the board never learns it is SDL

class VdmBoard : public Board {
public:
    VdmBoard();

    std::string type() const override { return "vdm1"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;
    bool    peek(uint16_t addr, uint8_t& out) const override;

    void power() override;
    void pump() override;

    std::vector<Property> properties() override;
    std::vector<MapEntry> memMap() const override;
    std::vector<MapEntry> ioMap() const override;

    // The host video service, wired once in main.cpp / tests/main.cpp -- an
    // SdlDisplay in the shipping binary, a NullDisplay headless. The board holds a
    // borrowed pointer; the composition root owns it (like C700Board::setResolver).
    static void setDisplay(Display* d);

    // ---- For tests, without a window: what the card would show. ----
    uint8_t vram(uint16_t off) const { return off < kBytes ? screen_[off] : 0xFF; }
    uint8_t statusByte() const;
    uint8_t scroll() const { return scroll_; }

private:
    static constexpr uint16_t kBytes = 1024;  // 16 rows x 64 cols
    static constexpr int kCols = 64, kRows = 16;

    void render();  // paint the current screen into the injected Display

    uint8_t  screen_[kBytes];
    uint32_t base_   = 0xCC00;  // 1 KB-aligned screen-RAM base
    uint8_t  port_   = 0xCC;    // I/O port (scroll out / status in); low 2 bits zero
    uint8_t  scroll_ = 0;       // top character row (low 4 bits of the last OUT)
    bool     reverse_ = false;  // SW1/SW2 -- whole-screen video polarity
    uint8_t  cursorMode_ = 1;   // 0 = off, 1 = blink, 2 = steady (SW3/SW4)

    // The status one-shot (D0): high until this Clock deadline passes. A T-state
    // count, so it is deterministic and replay-safe -- derived from emulated time,
    // never a host timer (reference file 3.3).
    uint64_t timerExpiry_ = 0;
};

} // namespace altair
