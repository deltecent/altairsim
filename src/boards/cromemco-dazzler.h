#pragma once
//
// Cromemco Dazzler -- the first color graphics card for the S-100 bus (1976). See
// reference/Cromemco Dazzler.md and docs/boards/cromemco-dazzler.md.
//
// A TWO-PORT CONTROL CARD OVER A FRAMEBUFFER IN MAIN RAM, plus a host display.
//
//   I/O     -- two ports. OUT BASE (default 0x0E) is control: D7 = on/off, D6-D0 =
//              the high 7 bits of the framebuffer start (A15-A9), so the picture
//              begins on a 512-byte boundary (base = (v & 0x7F) << 9). OUT BASE+1
//              (0x0F) is format: resolution (D6), size (D5), color (D4), and in X4
//              mode the whole-picture color/grey nibble (D3-D0). IN BASE returns the
//              status two bits: D7 ODD/EVEN scan line, D6 END-OF-FRAME (vblank).
//   MEMORY  -- NONE decoded. The framebuffer is ordinary system RAM owned by a memory
//              board; the real card DMA'd it, we read it. So the Dazzler decodes no
//              address and never collides with the RAM under it.
//   DISPLAY -- once per pump() the card reads the 512 B / 2 KB framebuffer out of main
//              RAM (Bus::peek -- no bus cycle, no strobe, no snoop) and paints it into
//              a Surface, exactly the render economy the VDM-1 uses. Never touches SDL:
//              the Display is injected at the composition root (setDisplay), and a
//              headless build gets a NullDisplay, so the card runs and is tested with no
//              window (DESIGN.md 7.4).
//
// WHY NOT A BUS MASTER? The real card cycle-steals the frame out of memory; modeling
// that would reproduce its ~15% CPU slowdown and put DMA reads on the cycle stream. We
// deliberately do NOT (Patrick, 2026-07-23): a correct picture needs only to READ the
// current framebuffer each frame, which Bus::peek does deterministically and which a
// guest cannot observe anyway (nothing on the S-100 side reads a pixel back). The two
// status bits a guest CAN time come off the Clock, like the VDM-1's.

#include "core/board.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class Display;  // host/display.h -- injected; the board never learns it is SDL

class DazzlerBoard : public Board {
public:
    DazzlerBoard();

    std::string type() const override { return "dazzler"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    void reset(Reset r) override;
    void power() override;
    void pump() override;

    // SNAPSHOT/RESTORE (DESIGN.md 13). The guest-set control/format latches -- on/off,
    // the framebuffer base, the format byte. The framebuffer itself is RAM and travels
    // on the memory board; the render shadow and dirty flags are derived, so restore
    // just marks the picture dirty and the next frame reads and repaints it whole.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;

    // The host video service, wired once in main.cpp / tests/main.cpp -- an SdlDisplay
    // in the shipping binary, a NullDisplay headless. Borrowed; the composition root
    // owns it (like VdmBoard::setDisplay).
    static void setDisplay(Display* d);

    // ---- For tests, without a window: what the card was told to show. ----
    bool     on() const { return on_; }
    uint32_t base() const { return base_; }
    uint8_t  format() const { return format_; }
    uint8_t  statusByte() const;

private:
    // The framebuffer is at most four 512-byte quadrants (2 KB).
    static constexpr uint16_t kQuadrant = 512;
    static constexpr uint16_t kMaxBytes = 2048;

    void render();          // read the framebuffer from RAM and paint the injected Display
    bool frameChanged();    // would the picture look different from the one on screen?

    // Geometry decoded from the format byte: elements per side, and whether X4.
    int  elementsPerSide() const;  // 32, 64 or 128
    bool x4() const { return format_ & 0x40; }     // D6 -- high resolution (on/off bits)
    bool twoK() const { return format_ & 0x20; }   // D5 -- 2 KB (four quadrants) vs 512 B
    bool color() const { return format_ & 0x10; }  // D4 -- color vs 16-grey

    // ---- Guest-set state (control + format ports) ----
    bool     on_     = false;   // OUT BASE   D7
    uint32_t base_   = 0;       // OUT BASE   D6-D0 -> (v & 0x7F) << 9
    uint8_t  format_ = 0;       // OUT BASE+1

    // ---- Straps ----
    uint8_t port_ = 0x0E;       // control/status port; format is port_|1

    // ---- Change detection (frameChanged) ----
    //
    // The framebuffer lives in MAIN RAM, so unlike the VDM-1 we get no write callback
    // when the guest changes it -- we poll. Each pump we read the live bytes into this
    // shadow and compare; a repaint is owed only when the bytes, the base, the format
    // or the on/off state moved. `dirty_` starts true so a fresh card paints once.
    bool     dirty_ = true;
    bool     shadowOn_ = false;
    uint32_t shadowBase_ = 0;
    uint8_t  shadowFormat_ = 0;
    uint8_t  shadow_[kMaxBytes] = {};
};

} // namespace altair
