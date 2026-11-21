#pragma once
//
// Votrax SC-01-A -- a phoneme speech synthesizer CHIP, NOT A BOARD (DESIGN.md 7.8).
//
// The PAiA SS-100 carries one. The next board with an SC-01 gets it for free. It knows
// nothing about S-100: it has six phoneme pins (P0-P5), STB, two inflection pins (I1, I2),
// A/R, a master clock, and an audio output.
//
// SOURCES (docs/sources.md). The datasheet gives the pins, the timing and the phoneme
// lengths (reference/PAiA SS-100 Speech Synthesizer.md). Patent US 4,433,210 is the chip's
// own design: the ROM fields, the transitions, the pitch counter, the noise gating and the
// switched-capacitor filters (reference/Votrax SC-01 patent US4433210.md). What only the
// die shows -- the ROM contents and bit order, the capacitor sizes, the glottal levels, the
// clock dividers and the noise register -- was read from MAME's votrax.cpp, as a
// reference only. The code here is our own. Each die value says so where it is used.
//
// TIME. The guest's side of the chip runs in T-states: strobe(), setInflection() and ar()
// take the T-state of the bus cycle. The chip's master clock is a separate oscillator (an
// RC, or the board's current source), so setClocks() says how fast both run. Within the
// chip everything counts master-clock cycles, the way the silicon does.
//
// A/R IS ARITHMETIC, NOT SOUND. A strobe fixes when A/R will rise: 72 master clocks to
// latch the phoneme, then 16 ticks of (4 x duration + 1) x 36 master clocks, then 2. So
// ar() and arDeadline() are exact at any moment, and a board can schedule its interrupt
// from arDeadline() whether the sound is played or not.
//
// THE SOUND IS RENDERED LATER, in the board's host turn. strobe() and setInflection() only
// queue what the guest did, with its T-state. render() runs the chip's digital and analog
// halves up to a T-state and makes PCM at the host's rate; skipTo() moves past time that
// will not be heard (a machine running flat out). The output is the same however the
// time is cut into render() calls.

#include <cstdint>
#include <vector>

namespace altair {

class StateWriter;
class StateReader;

class VotraxSc01 {
public:
    // The datasheet's standard master clock: the phoneme lengths in Table 1 are at this rate.
    static constexpr uint32_t kStandardClockHz = 720000;

    // From the rising edge of STB to the phoneme being latched, in master clocks
    // (datasheet: STB is held low 72 master-clock periods).
    static constexpr uint32_t kLatchClocks = 72;

    // arDeadline() when A/R is already high: nothing is pending.
    static constexpr uint64_t kNever = ~uint64_t{0};

    VotraxSc01();

    // ---- the pins ----

    // From T-state `t` on, the CPU runs at `cpuHz` T-states per second and the master
    // clock at `masterHz`. Call it when either changes; calling it with the same rates
    // changes nothing.
    void setClocks(uint64_t t, long long cpuHz, uint32_t masterHz);

    // I1 (bit 0) and I2 (bit 1) at T-state `t`. They are not latched: the pitch follows
    // them at once ("instantaneously set the pitch level", datasheet).
    void setInflection(uint64_t t, uint8_t inflection);

    // The rising edge of STB at T-state `t`, with `code` (P0-P5, the low 6 bits) on the
    // pins. A/R falls at once.
    void strobe(uint64_t t, uint8_t code);

    // A/R at T-state `t`, in true sense: true = the chip requests the next phoneme.
    bool ar(uint64_t t) const;

    // The T-state at which A/R rises, or kNever when it is already high.
    uint64_t arDeadline() const;

    // ---- the audio output ----

    // Append every sample that completes by T-state `untilT`, at `rate` samples per
    // emulated second, full scale +/-32767. A sample still in progress waits for the next
    // call.
    void render(uint64_t untilT, int rate, std::vector<int16_t>& out);

    // Move to T-state `t` and make nothing: what the guest did up to `t` takes effect, and
    // the sound between is not computed.
    void skipTo(uint64_t t);

    // ---- lifecycle ----

    // Power-up: T-state 0, STOP latched, A/R high, silence. The chip has no reset pin.
    void power();

    // SNAPSHOT/RESTORE: the latched phoneme, the inflection, the clocks and the A/R
    // timing. The sound in progress is not saved; after a restore the voice starts from
    // the latched phoneme with quiet filters.
    void serialize(StateWriter& w) const;
    void deserialize(StateReader& r);

    // ---- for SHOW and tests ----
    uint8_t  phoneme() const { return code_; }
    uint8_t  inflection() const { return infl_; }
    uint32_t masterHz() const { return masterHz_; }

    // The datasheet's symbol for `code` (Table 1), such as "AH" or "PA0".
    static const char* name(uint8_t code);

    // From STB to A/R rising for `code`, in master clocks.
    static uint64_t phonemeClocks(uint8_t code);

private:
    // ---- the guest's side: exact, in master clocks ----

    // The master-clock count at T-state `t`, and the first T-state at which it reaches `c`.
    uint64_t cycleAt(uint64_t t) const;
    uint64_t tAt(uint64_t c) const;
    // Move the anchor of that mapping to `t`, keeping it exact.
    void reanchor(uint64_t t);

    long long cpuHz_    = 2000000;
    uint32_t  masterHz_ = kStandardClockHz;
    uint64_t  anchorT_  = 0;   // a T-state ...
    uint64_t  anchorC_  = 0;   // ... the master clocks counted by then ...
    uint64_t  anchorR_  = 0;   // ... and the part of one, in units of 1/cpuHz_

    uint8_t  code_     = 0x3F;   // the phoneme last strobed
    uint8_t  infl_     = 0;
    uint64_t latchAt_  = 0;      // master clock at which code_ is latched
    uint64_t arRiseAt_ = 0;      // master clock at which A/R rises

    // ---- what the guest did, waiting to be heard ----
    enum class Ev : uint8_t { Latch, Inflection, Clocks };
    struct Event {
        uint64_t  t;
        Ev        kind;
        uint8_t   value;
        long long cpuHz;
        uint32_t  masterHz;
    };
    std::vector<Event> events_;
    void apply(const Event& e, double atT);
    void applyUpTo(double t);

    // ---- the sound: chip samples at master / 18, averaged into host samples ----
    double chipAt(double t) const { return rAnchorChip_ + (t - rAnchorT_) * rChipPerT_; }
    void   rebase(double t, long long cpuHz, uint32_t masterHz);

    double    rAnchorT_    = 0;   // the render side's own mapping from T-states ...
    double    rAnchorChip_ = 0;   // ... to chip samples
    double    rChipPerT_   = 0;
    long long rCpuHz_      = 2000000;
    uint32_t  rMasterHz_   = kStandardClockHz;
    double    outOrigin_   = 0;   // T-state where the host sample grid starts
    uint64_t  outN_        = 0;   // host samples made since outOrigin_
    int       outRate_     = 0;
    double    chipPos_     = 0;   // chip samples run, with the part of the current one
    double    chipOut_     = 0;   // the current chip sample's level
    uint64_t  chipN_       = 0;   // chip samples run: the digital half ticks on every other

    double chipSample();   // run one chip sample (and a digital tick on every other)
    void   tick();          // the digital half, at master / 36
    double analog();        // the analog half, at master / 18
    void   latch(uint8_t code);
    void   commitFilters(bool force);
    void   resetSound(uint64_t t);   // quiet, with code_ latched, the clocks as they are

    // The ROM fields of the latched phoneme.
    uint8_t romF1_ = 0, romF2_ = 0, romF2q_ = 0, romF3_ = 0;
    uint8_t romFc_ = 0, romVa_ = 0, romFa_ = 0;
    uint8_t romVd_ = 0, romCld_ = 0, romDur_ = 0;
    bool    romCl_ = false, romPause_ = false;

    // The parameters in transition (8 bits: 16 x the 4-bit value when settled).
    uint8_t curF1_ = 0, curF2_ = 0, curF2q_ = 0, curF3_ = 0;
    uint8_t curFc_ = 0, curVa_ = 0, curFa_ = 0;
    // The values the filters use, taken from the transition once per glottal period.
    uint8_t fltF1_ = 0, fltF2_ = 0, fltF2q_ = 0, fltF3_ = 0;
    uint8_t fltFc_ = 0, fltVa_ = 0, fltFa_ = 0;

    // The counters.
    uint16_t phoneTick_ = 0;   // master/36 cycles within the current tick
    uint8_t  ticks_     = 16;  // ticks of the phoneme so far (16 = done)
    uint8_t  update_    = 0;   // divides master/36 by 48 for the transition clocks
    uint8_t  pitch_     = 0;   // the glottal period counter
    uint16_t period_    = 256; // its period in counts (256: it wraps without a match)
    uint8_t  rInfl_     = 0;   // the inflection the sound has reached
    uint8_t  closure_   = 28;  // the closure ramp (0 = open, 28 = shut)
    bool     closed_    = true;
    uint16_t noise_     = 0;   // the noise shift register
    bool     noiseBit_  = false;

    // The filters, as direct-form biquads in normalized form.
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        double run(double x);
        void   quiet() { x1 = x2 = y1 = y2 = 0; }
    };
    Biquad f1_, f2v_, f2n_, f3_, f4_, out_;
};

} // namespace altair
