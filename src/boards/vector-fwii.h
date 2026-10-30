#pragma once
//
// Vector Graphic Flashwriter II -- the screen and the keyboard port of a Vector
// Graphic machine, on one S-100 board. 24 rows of 80 characters, scanned out of 2K of
// memory-mapped RAM the guest writes into. See reference/Flashwriter II Manual.md.
//
//   MEMORY   -- 2K of RAM at `base` (default F000), read and write, no mirror. It does
//               not look at PHANTOM*. 1920 bytes show. The last 128 do not.
//   I/O      -- two input ports at `port` (default 00). The decode needs sINP, so an
//               OUT is not decoded at all.
//                 even  status: bit 6 = key waiting, bit 0 = the inverse of bit 6,
//                       bit 5 = 0 during vertical blanking. Nothing drives the other
//                       five bits, so they read 1 on the bus that floats to FF.
//                 odd   data: the 8212 latch, all eight bits, and the read clears the
//                       key flag. An IN from the status port does not.
//   INTERRUPT -- the key flag is a level on pin 73, fitted from February 1980 (PIB
//               0055), until the IN from the data port. The board has no decode for
//               sINTA, so nothing claims the acknowledge and the floating bus reads
//               as RST 7. It has no VI connector, so `interrupt` offers none and int.
//   KEYBOARD -- the `keyboard` unit, a ByteStream latched in pump().
//   DISPLAY  -- the board paints a Surface in pump() and present()s it, only when a byte
//               that shows has changed. 640x480: each dot line is drawn twice for the
//               shape of a 4:3 tube. There is no hardware cursor. The guest makes one
//               with bit 7, which reverses a cell.
//
// THE VERTICAL COUNTER RUNS OFF THE BOARD'S OWN CRYSTAL, never the Clock (#825). See
// lineNow() in the .cpp.

#include "core/board.h"
#include "host/stream.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class Display;  // host/display.h -- injected; the board never learns it is SDL

// Turning an endpoint string into a stream is the monitor's job, not the board's
// (DESIGN.md 7.7). Declared identically to the chip headers'.
using EndpointResolver =
    std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;

class FwiiBoard : public Board {
public:
    FwiiBoard();

    std::string type() const override { return "fwii"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;
    bool    peek(uint16_t addr, uint8_t& out) const override;

    bool assertsInt() const override { return irq_ == IrqJumper::Int && kbHave_; }

    void reset(Reset r) override;
    void power() override;
    void pump() override;

    // SNAPSHOT/RESTORE (DESIGN.md 13). The video RAM and the 8212 travel. The raster
    // does not: its phase is host time, and host time does not travel in a snapshot.
    // Restore marks the screen dirty, so the restored picture reaches the host.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    std::vector<Property> properties() override;
    std::vector<Property> unitProperties(const std::string& unit) override;
    std::vector<UnitDef>  units() const override;
    std::vector<MapEntry> memMap() const override;
    std::vector<MapEntry> ioMap() const override;

    // A keystroke arriving is live traffic, so the idle policy stands the machine back
    // up for it (Board::rxBytes), the signal the VDB-8024 gives.
    uint64_t rxBytes() const override { return kbRx_; }

    bool connect(const std::string& unit, const std::string& endpoint,
                 std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;
    ByteStream* unitStream(const std::string& unit) override {
        return unit == "keyboard" ? kb_.get() : nullptr;
    }

    // The host video service, wired once in main.cpp / tests/main.cpp -- an SdlDisplay
    // in the shipping binary, a NullDisplay headless. Borrowed; the root owns it.
    static void setDisplay(Display* d);

    // The monitor resolves an endpoint string to a stream. Installed in main.cpp and
    // tests/main.cpp.
    static void setResolver(EndpointResolver r);

    // ---- For tests, without a window. ----
    uint8_t vram(uint16_t off) const { return off < kBytes ? vram_[off] : 0xFF; }
    uint8_t statusByte() const;

private:
    static constexpr uint16_t kBytes = 2048;  // four 2114s
    static constexpr int      kCols = 80, kRows = 24;
    static constexpr uint16_t kShown = kCols * kRows;  // the last 128 bytes never show

    void fillRam();        // new video RAM, per the `fill` policy
    void latchKeyboard();  // take one byte off the line, from pump() only
    int  lineNow() const;  // the vertical counter, 0-261
    void render();         // paint the screen into the injected Display

    // Would the picture differ from the one on the host? Deterministic, so a headless
    // build skips the same frames a windowed one does.
    bool frameChanged() const { return dirty_; }

    uint8_t  vram_[kBytes];
    uint16_t base_ = 0xF000;  // area F, pad 9. Rev 4-A's factory setting, and Monitor 4.3's
    uint8_t  port_ = 0x00;    // area J, even. Pad 2 is 00/01

    // Area L. The default is the wire every board has from February 1980.
    IrqJumper irq_ = IrqJumper::Int;

    // Video RAM at power-on. Real RAM is not zeroed, and on this board it shows.
    enum class Fill : uint8_t { Zero, Random };
    Fill     fill_ = Fill::Random;
    uint64_t seed_ = 1;

    int  videoWidth_ = 0;  // host window width in px, 0 = auto (~half the screen)
    bool dirty_ = true;    // true at the start: a fresh board owes the host one frame

    // ---- U46, the 8212 ----
    std::unique_ptr<ByteStream> kb_;  // never null -- a NullStream stands in
    uint8_t  kbData_ = 0;
    bool     kbHave_ = false;  // the service-request flip-flop, shown as "a key waits"
    uint64_t kbRx_   = 0;      // keystrokes handed to the guest -- the idle-traffic signal
};

} // namespace altair
