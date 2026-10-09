#pragma once
//
// Cromemco Dazzler II -- the Dazzler with its picture in 4 KB of on-board RAM instead of
// main memory. A modern board (s100computers.com, 2016), register-compatible with the
// original. See reference/Cromemco Dazzler.md section 6 and docs/boards/cromemco-dazzler2.md.
//
// A SECOND BOARD TYPE, NOT A SWITCH. Like tarbell / tarbelldd: the machine file names
// `dazzler2`, and DazzlerBoard keeps being the original. Everything the two share -- the
// ports, the control and format latches, the status clock, the pixel encoding, the video
// output -- is inherited. What differs is where the scan reads a byte (sample()) and what
// the status port drives on D5-D0.
//
// HOW THE REAL CARD GETS ITS PICTURE. It is a bus SLAVE. It watches every memory write
// (wantsSnoop); one whose address, less the base set by OUT 0E, falls in 0000-0FFF is also
// written into the card's RAM. Main memory takes the same write as always and answers every
// read, so the card never needs RAM at the picture address (a ROM or an empty slot works),
// and it never takes the bus (BJL_ver4.pld, `nce`/`nwr`). The CPU is not slowed.
//
// WHAT THE GUEST CAN SEE THAT THE ORIGINAL HIDES:
//   * the card RAM holds only what was written after the base was set; before that it is
//     undefined, so a picture drawn first shows random data (power() fills it from `seed`);
//   * the scan shows a 2 KB half of the 4 KB array -- the first, or (jumper P18 31-32
//     closed) the second, at +800H -- the `page` property;
//   * IN BASE reads D5-D0 as 0, not 1.
//
// NOT MODELED, AND SAID SO (docs/boards/cromemco-dazzler2.md, Limitations): the alternate
// color map, the page bit set from software through the extra port, a base change during
// display, and the joystick/DAC circuit and extra video outputs.

#include "boards/cromemco-dazzler.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class Dazzler2Board : public DazzlerBoard {
public:
    Dazzler2Board();

    std::string type() const override { return "dazzler2"; }

    bool wantsSnoop() const override { return true; }
    void snoop(const BusCycle& c) override;

    void power() override;

    // The base class's latches, then the card RAM. `page` and `seed` are straps, like `port`.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    std::vector<Property> properties() override;

    // ---- For tests ----
    uint8_t cardRam(uint16_t off) const { return ram_[off & (kRamBytes - 1)]; }

protected:
    uint8_t sample(uint16_t off) const override;

private:
    static constexpr uint16_t kRamBytes = 4096;   // the on-card dual-port RAM
    static constexpr uint16_t kPageBytes = 2048;  // the half the scan shows

    uint8_t  ram_[kRamBytes] = {};
    bool     secondPage_ = false;   // P18 31-32 closed: show the half at +800H
    uint64_t seed_ = 1;             // for the undefined power-on contents of ram_
};

} // namespace altair
