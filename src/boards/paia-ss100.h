#pragma once
//
// PAiA SS-100 -- an S-100 speech synthesizer board (1982), built around a Votrax SC-01-A.
// See reference/PAiA SS-100 Speech Synthesizer.md and docs/boards/paia-ss100.md.
//
// TWO WRITE-ONLY PORTS in a block of four, at a base set by S1-S4 (A7-A4; default 30):
//
//   BASE+0  SPORT, the data port. Bits 0-5 are the phoneme code, on the SC-01's P0-P5;
//           bits 6-7 are the inflection, latched by IC8 into I1 (bit 6) and I2 (bit 7).
//           The write's strobe is the SC-01's STB: A/R falls, and the phoneme starts.
//   BASE+1  PPORT, the control word, latched by IC10 (Fig. 5):
//             b7     ENABLE  the interrupt request (IC11A: A/R AND ENABLE)
//             b6-5   VOLUME  00 full, 01 medium, 10 low, 11 the amplifier off (Q4)
//             b4     MODE    0 speech (the 3.4 kHz low-pass R25/C7 in), 1 sound effects
//             b3-0   XF      the master clock, 0000 highest ... 1111 lowest
//
// Nothing drives the bus on an IN: the four input strobes are decoded and not used.
// POC clears IC8 and IC10, so the board comes up with its interrupt disabled.
//
// THE INTERRUPT is a level: A/R high (the chip wants the next phoneme) AND ENABLE. It goes
// out on the jumper, VI0-VI7 or PINT (`interrupt`, default vi0, as the vocabulary software
// expects). It stays up until the next phoneme is strobed or ENABLE is cleared.
//
// THE MASTER CLOCK is a current source (Q1/Q2, R14-R24) charging C5. XF switches in
// R16-R19 and MODE switches in R20 + the R21 trim pot. masterHz() works the current out
// from the schematic and scales it so that XF8 in speech mode is 720 kHz, which is how the
// board is calibrated with R21 at mid-travel. The `r21` property moves the pot: a board
// trimmed elsewhere runs at other frequencies. Speech mode stays within about +/-10% of
// 720 kHz at mid-travel; effects mode is much lower, and R21 does not reach it.
//
// THE SOUND is made in pump(), from the chip's render(), through the board's own filter
// and volume. It plays only with a crystal (host/speaker.h, planSlice()).

#include "chips/votrax-sc01.h"
#include "core/board.h"
#include "core/clock.h"
#include "host/volume.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class PaiaSs100 : public Board {
public:
    PaiaSs100();
    ~PaiaSs100() override;

    std::string type() const override { return "ss100"; }

    bool decodes(const BusCycle& c) const override;
    void write(const BusCycle& c) override;

    bool    assertsInt() const override;
    uint8_t assertsVi() const override;

    void reset(Reset r) override;
    void power() override;
    void pump() override;

    // SNAPSHOT/RESTORE (DESIGN.md 13): the control word, the inflection latch and the
    // SC-01's state. The port and the jumper are config; the sound in progress is the
    // host's and does not travel.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    std::vector<Property> properties() override;
    std::vector<std::string> statusLines() const override;
    std::vector<MapEntry> ioMap() const override;

    // R21 at mid-travel, the default: XF8 in speech mode is then 720 kHz.
    static constexpr uint32_t kR21Mid = 500;

    // The SC-01's master clock for a control word, in Hz (bits 0-4 count), with R21 at
    // `r21` ohms (0-1000).
    static uint32_t masterHz(uint8_t control, uint32_t r21 = kR21Mid);

    // The VOLUME field as a gain: 1, about 0.55, about 0.15, and 0 with the amplifier off.
    static double volumeGain(uint8_t control);

    // ---- for tests ----
    uint8_t control() const { return control_; }
    const VotraxSc01& chip() const { return chip_; }

private:
    static constexpr uint8_t kEnable = 0x80, kMode = 0x10;

    bool requesting() const { return arHigh_ && (control_ & kEnable); }

    // Tell the chip the CPU and master clock rates, from T-state `t` on.
    void syncClocks(uint64_t t);
    // Schedule the rise of A/R, or nothing when it is high.
    void armAr();
    void arRose();

    // The sound: render the chip up to `t` with the control word `ctl`, into buf_.
    void renderTo(uint64_t t, uint8_t ctl);
    // Leave the time up to `now` unplayed.
    void skipSound(uint64_t now);
    // The control word changed at `now`: queue it for the sound.
    void noteControl(uint64_t now);
    bool driven(uint64_t now) const;

    // ---- straps ----
    uint8_t   base_ = 0x30;
    IrqJumper irq_  = IrqJumper::Vi0;
    uint32_t  r21_  = kR21Mid;   // the trim pot, ohms

    // ---- the latches ----
    uint8_t control_ = 0;   // IC10
    uint8_t pitch_   = 0;   // IC8: I1 (bit 0), I2 (bit 1)

    VotraxSc01    chip_;
    bool          arHigh_ = true;
    Clock::Handle arWake_ = Clock::kNone;

    // ---- the sound ----
    struct CtlChange {
        uint64_t t;
        uint8_t  control;
    };
    std::vector<CtlChange> ctlChanges_;   // control writes not yet heard
    uint8_t  heardCtl_    = 0;            // the control word the sound has reached
    uint64_t renderedTo_  = 0;
    uint64_t quietAt_     = 0;            // T-state at which the last phoneme ended
    bool     spoke_       = false;        // a phoneme since power-on or restore
    double   lowpass_     = 0;            // the R25/C7 filter's output
    std::vector<int16_t> buf_;
    Volume   vol_;                            // how loud on the host (a host setting)
};

} // namespace altair
