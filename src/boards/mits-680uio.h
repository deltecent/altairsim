#pragma once
//
// The Altair 680b Universal I/O board -- reference/Altair 680b Universal IO Board.md.
//
// THE 680b's GENERAL-PURPOSE EXPANSION BOARD, and a peer of the onboard I/O
// (mits-680io.h), not a replacement: it adds a SECOND 6850 serial port and a
// 6820 PIA parallel port to the machine whose console 6850 already lives at
// F000/F001. Like everything on the 6800 it is MEMORY-MAPPED (no IN/OUT space),
// and unlike the KCACR it is ACTIVE-HIGH -- the raw 6820/6850 register bits, not
// the KCACR's inverted "True = Logic 0" convention (reference §warning).
//
// ADDRESSING (reference §1). The 680b reserves F000-F0FF for I/O. This board
// takes a 16-address window whose base is set by switch S9 (16 positions,
// 0x10 apart). Within the window:
//
//     base+6 / base+7   6850 ACIA serial  ('serial')     -- A3=0 selects serial
//     base+8 .. base+B  6820 PIA-C parallel (port 1)      -- A3=1, A2=0
//     base+C .. base+F  6820 PIA-B parallel (port 2)      -- A3=1, A2=1 (if populated)
//
// At S9's lowest position (base = F000) that is the manual's default map:
// serial F006/F007, PIA-C F008-F00B, PIA-B F00C-F00F. window offsets 0..5 are
// NOT decoded here, so the onboard console at F000/F001 and the straps at F002
// (mits-680io.h) never clash even at base F000.
//
// TWO FIXED FACILITIES do not move with S9 (reference §2):
//
//     F003              hardware switch inputs ('sense') -- READ-ONLY tri-state
//     F010/F011         8-bit non-latched output, "Drive 1" (control/data)
//     F012/F013         8-bit non-latched output, "Drive 2"
//
// F010/F011 COLLIDE WITH THE KCACR (its status/control + data are at exactly
// F010/F011). The period fix is "remove UI/O IC A1", which disables this board's
// non-latched-output decode so the cassette board can own those addresses. The
// `nlout` property models that jumper: default on; set off when a 680kcacr shares
// the machine. See reference §2 and [[altairsim-88uio-board]].
//
// REUSE. The serial section is a Sio2Port (chips/sio2port.h), the same 6850 glue
// the 680io/Turnkey/2SIO carry -- one channel `serial`, base 0, the card
// translating base+6/base+7 to section ports 0/1. The parallel section is a
// Pia6820 (chips/mc6820.h), the register model the 88-4PIO uses inline, lifted
// into a chip; the card owns the section streams and feeds/drains the chip.

#include "chips/mc6820.h"
#include "chips/sio2port.h"
#include "core/board.h"
#include "host/stream.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class Uio680Board : public Board {
public:
    Uio680Board();

    std::string type() const override { return "680uio"; }

    // ---- bus (memory-mapped) ----
    bool    decodes(const BusCycle&) const override;
    uint8_t read(const BusCycle&) override;
    void    write(const BusCycle&) override;

    // Scattered addresses in page F0 (a relocatable window plus fixed F003 and
    // F010-F013), never the whole page -- the bus must ask per address.
    bool decodeIsPageUniform() const override { return false; }

    // ---- interrupts: the ACIA and both PIAs drive the 6800 IRQ (reference §6) ----
    bool    assertsInt() const override;
    uint8_t assertsVi() const override { return sio_.assertsVi(); }

    // ---- lifecycle ----
    void reset(Reset) override;
    void power() override { reset(Reset::PowerOn); }
    void pump() override;
    void clockAttached() override { sio_.attachClock(clock_); }
    void configChanged() override { sio_.refresh(); }

    // ---- reflection ----
    std::vector<Property> properties() override;
    std::vector<Property> unitProperties(const std::string& unit) override;
    std::vector<UnitDef>  units() const override;
    std::vector<MapEntry> memMap() const override;

    // ---- units: the serial `serial` (via the section) + the PIA sections ----
    bool connect(const std::string& unit, const std::string& endpoint, std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;
    ByteStream* unitStream(const std::string& unit) override;
    uint64_t    rxBytes() const override { return sio_.rxBytes(); }
    std::vector<std::string> drainLog() override;

    // ---- SNAPSHOT / RESTORE (DESIGN.md 13) ----
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // The parallel sections' endpoint resolver. The serial section shares the
    // one Sio2Port::setResolver the program installs globally, so this only wires
    // the PIA/output streams (like the 88-4PIO's own resolver).
    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;
    static void setResolver(EndpointResolver r);

private:
    // ---- addressing ----
    uint16_t base_  = 0xF000;   // S9 window base (F000 + position*0x10)
    int      pias_  = 1;        // 6820 PIAs populated: 1 (PIA-C) or 2 (+ PIA-B)
    bool     nlout_ = true;     // decode the F010-F013 non-latched output (IC A1)

    static constexpr uint16_t kSense    = 0xF003;   // fixed switch inputs, read-only
    static constexpr uint16_t kNlOutLo  = 0xF010;   // Drive 1 control/data
    static constexpr uint16_t kNlOutHi  = 0xF013;   // Drive 2 control/data

    uint8_t sense_  = 0x00;     // the F003 switch settings (a board strap)
    uint8_t drive1_ = 0x00;     // last byte driven on Drive 1 (F011)
    uint8_t drive2_ = 0x00;     // last byte driven on Drive 2 (F013)

    // ---- the chips ----
    Sio2Port sio_;              // the 6850 serial section, channel `serial`
    Pia6820  piaC_;             // parallel port 1 (sections p1a/p1b)
    Pia6820  piaB_;             // parallel port 2 (sections p2a/p2b), if pias_ >= 2

    // One PIA section's connection to the outside world. The chip holds the
    // registers; the board holds the stream (never null -- NullStream when idle).
    struct Line {
        std::unique_ptr<ByteStream> stream;
        std::string                 spec = "null";
    };
    std::array<Line, 4> line_;  // p1a, p1b, p2a, p2b

    // unit name -> index 0..3 (p1a/p1b/p2a/p2b) within the populated PIAs, or -1.
    int      lineIndex(const std::string& unit) const;
    Pia6820& piaForLine(int idx) { return idx < 2 ? piaC_ : piaB_; }
    int      sectionForLine(int idx) const { return idx & 1; }
    bool     applyEndpoint(const std::string& unit, const std::string& endpoint, std::string& err);

    std::vector<std::string> log_;
};

} // namespace altair
