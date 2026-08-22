#pragma once
//
// CompuPro (Godbout) System Support 1 -- a multifunction S-100 board.
// docs/boards/compupro-ss1.md. Source: reference/CompuPro System Support 1.md
// (CompuPro/Godbout, Document #11620, 1981).
//
// One 16-port I/O block packs a serial channel (2651 UART), an interval timer
// (Intel 8253), two cascaded interrupt controllers (Intel 8259A), a battery-backed
// real-time clock/calendar (OKI MSM5832), and a socket for an AMD 9511A/9512 math
// coprocessor. This board is being brought up in phases (issue #392):
//
//   * PHASE 1 (this file today): the MSM5832 real-time clock. Ports base+10/+11.
//   * later: the 2651 UART (+12..+15), the 8253 timer (+4..+7), the dual 8259A
//     (+0..+3). The math socket (+8/+9) is deferred to its own issue -- an empty
//     socket is the real board's default, so those ports simply float.
//
// Only the ports a landed phase actually implements are decoded; the rest of the
// block floats 0xFF, which is exactly right for an unpopulated math socket.
//
// THE STANDARD BASE IS 50H. The manual claims no factory default beyond the CompuPro
// software convention that the block lives at 50H, so that is our default -- but the
// base is a strap like any other, and the `base` property moves the whole block.

#include "chips/msm5832.h"
#include "core/board.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class Ss1Board : public Board {
public:
    std::string type() const override { return "ss1"; }

    bool decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void write(const BusCycle& c) override;

    void reset(Reset) override;
    void power() override;

    // SNAPSHOT/RESTORE (DESIGN.md 13). The board itself is stateless straps; the RTC
    // carries the runtime state (its host-time offset and edit latches).
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;

private:
    // The two RTC ports, relative to the 16-port block base.
    uint8_t clockCmdPort() const { return (uint8_t)(base_ + 10); }
    uint8_t clockDataPort() const { return (uint8_t)(base_ + 11); }

    uint8_t base_ = 0x50;  // the 16-port block base -- CompuPro standard is 50H
    Msm5832 rtc_;
};

}  // namespace altair
