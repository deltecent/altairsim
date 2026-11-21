// Votrax SC-01-A. See votrax-sc01.h for the model and its sources.

#include "chips/votrax-sc01.h"

#include "chips/votrax-sc01-rom.h"
#include "core/statefile.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace altair {

namespace {

// Table 1 of the datasheet, by code.
const char* const kNames[64] = {
    "EH3", "EH2", "EH1", "PA0", "DT",  "A2",  "A1",  "ZH",
    "AH2", "I3",  "I2",  "I1",  "M",   "N",   "B",   "V",
    "CH",  "SH",  "Z",   "AW1", "NG",  "AH1", "OO1", "OO",
    "L",   "K",   "J",   "H",   "G",   "F",   "D",   "S",
    "A",   "AY",  "Y1",  "UH3", "AH",  "P",   "O",   "I",
    "U",   "Y",   "T",   "R",   "E",   "W",   "AE",  "AE1",
    "AW2", "UH2", "UH1", "UH",  "O2",  "O1",  "IU",  "U1",
    "THV", "TH",  "ER",  "EH",  "E1",  "AW",  "PA1", "STOP",
};

// The glottal pulse, one level for each 8 counts of the pitch counter, then 0 for the
// rest of the period. Patent Fig. 10 draws the shape (a jump up, a falling staircase); its
// levels are trimpots there and are not given. These are the die's: a resistor ladder of
// one transistor to ground and seven of a quarter its size (MAME votrax.cpp). Scaled so
// that the middle is 0 and the top is 1.
constexpr double kGlottal[9] = {
    0.0, -4.0 / 7, 7.0 / 7, 6.0 / 7, 5.0 / 7, 4.0 / 7, 3.0 / 7, 2.0 / 7, 1.0 / 7,
};

// The noise level against the glottal pulse's peak, from MAME's comparison with line-in
// recordings of an SC-01-A.
constexpr double kNoise = 0.17;

// The output, from the filters' units to 16-bit PCM.
constexpr double kOutGain = 6000.0;

// The sum of the capacitors in `caps` whose bit is set in `bits`, bit 0 first.
double bank(unsigned bits, std::initializer_list<double> caps) {
    double sum = 0;
    for (double c : caps) {
        if (bits & 1) sum += c;
        bits >>= 1;
    }
    return sum;
}

// THE FILTERS. Each tunable filter is a switched-capacitor two-integrator loop (patent
// Figs. 4, 5 and 9). A switched capacitor S at the switching rate fc is a resistor of
// 1 / (fc * S), so only the ratios of the capacitors matter. The capacitor values are the
// die's (MAME votrax.cpp; they are areas in square micrometres, which is the same thing
// for a ratio). The patent's values for the same parts are in the reference file; the die
// is the chip that shipped, so it wins.
//
// The loop: the input goes through C1 in parallel with switched S1 into the first
// integrator (C2, damped by switched S2); its output goes through switched Sx into the
// second integrator (C3); that output, inverted, returns to the first. Solving it gives
//
//              1 + k0 s                 k0 = C1 / (fc S1)
//     H(s) = ------------------         k1 = C3 S2 / (fc S1 Sx)
//            1 + k1 s + k2 s^2          k2 = C3 C2 / (fc^2 S1 Sx)
//
// which is turned into a biquad by the bilinear transform, warped to be exact at the
// filter's natural frequency 1 / sqrt(k2). `fs` is the rate the filter runs at.
void resonator(double& b0, double& b1, double& b2, double& a1, double& a2,
               double C1, double S1, double S2, double C2, double Sx, double C3,
               double fc, double fs) {
    const double k0 = C1 / (fc * S1);
    const double k1 = C3 * S2 / (fc * S1 * Sx);
    const double k2 = C3 * C2 / (fc * fc * S1 * Sx);
    const double w0 = 1.0 / std::sqrt(k2);
    const double zc = w0 / std::tan(w0 / (2 * fs));
    const double m0 = zc * k0, m1 = zc * k1, m2 = zc * zc * k2;
    const double d = 1 + m1 + m2;
    b0 = (1 + m0) / d;
    b1 = 2 / d;
    b2 = (1 - m0) / d;
    a1 = (2 - 2 * m2) / d;
    a2 = (1 - m1 + m2) / d;
}

// The noise's way into F2: it enters at the second integrator, and from there the loop is
// a first-order shelf, low frequencies cut and high ones passed (the die's topology, MAME
// votrax.cpp):
//
//            k0 + k2 s           k0 = fc S2,  k1 = fc (S1 Sx / S2 + S2),  k2 = C2
//     H(s) = ---------
//            k1 + k2 s
void shelf(double& b0, double& b1, double& a1,
           double S1, double S2, double C2, double Sx, double fc, double fs) {
    const double k0 = fc * S2;
    const double k1 = fc * (S1 * Sx / S2 + S2);
    const double m  = 2 * fs * C2;
    const double d  = k1 + m;
    b0 = (k0 + m) / d;
    b1 = (k0 - m) / d;
    a1 = (k1 - m) / d;
}

// The fixed low-pass at the output. The die's capacitors put its corner near 150 Hz, which
// recordings of the chip show is wrong; MAME moves it to about 4 kHz (x 150/4000), and so
// do we. It still moves with the master clock, as every filter on the chip does.
void lowpass(double& b0, double& b1, double& a1, double fc, double fs) {
    const double k  = 23131.0 / (fc * 1122.0) * (150.0 / 4000.0);
    const double w  = 1.0 / k;
    const double zc = w / std::tan(w / (2 * fs));
    const double m  = zc * k;
    b0 = b1 = 1 / (1 + m);
    a1 = (1 - m) / (1 + m);
}

// One step of a transition: add one eighth of the way to the target (patent claims 1-3,
// "approximately equal to 12.5%"). `cur` holds 16 x the 4-bit value when it has settled.
void approach(uint8_t& cur, uint8_t target) {
    cur = static_cast<uint8_t>(cur - (cur >> 3) + (target << 1));
}

} // namespace

double VotraxSc01::Biquad::run(double x) {
    const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
}

VotraxSc01::VotraxSc01() { power(); }

const char* VotraxSc01::name(uint8_t code) { return kNames[code & 0x3F]; }

uint64_t VotraxSc01::phonemeClocks(uint8_t code) {
    const sc01rom::Phone p = sc01rom::lookup(code);
    // 16 ticks of (4 x duration + 1) cycles of master / 36, and 2 more for the path to
    // the A/R pin (die timing, MAME votrax.cpp).
    return kLatchClocks + 16ull * (4ull * p.duration + 1) * 36 + 2;
}

// ---- the guest's side ----

uint64_t VotraxSc01::cycleAt(uint64_t t) const {
    if (t <= anchorT_) return anchorC_;
    return anchorC_ + (anchorR_ + (t - anchorT_) * masterHz_) / static_cast<uint64_t>(cpuHz_);
}

uint64_t VotraxSc01::tAt(uint64_t c) const {
    if (c <= anchorC_) return anchorT_;
    const uint64_t need = (c - anchorC_) * static_cast<uint64_t>(cpuHz_) - anchorR_;
    return anchorT_ + (need + masterHz_ - 1) / masterHz_;
}

void VotraxSc01::reanchor(uint64_t t) {
    if (t <= anchorT_) return;
    const uint64_t n = anchorR_ + (t - anchorT_) * masterHz_;
    anchorC_ += n / static_cast<uint64_t>(cpuHz_);
    anchorR_ = n % static_cast<uint64_t>(cpuHz_);
    anchorT_ = t;
}

void VotraxSc01::setClocks(uint64_t t, long long cpuHz, uint32_t masterHz) {
    if (cpuHz < 1) cpuHz = 1;
    if (masterHz < 1) masterHz = 1;
    reanchor(t);
    if (cpuHz == cpuHz_ && masterHz == masterHz_) return;
    // Keep the part of a master clock already counted, in the new units.
    anchorR_ = anchorR_ * static_cast<uint64_t>(cpuHz) / static_cast<uint64_t>(cpuHz_);
    cpuHz_    = cpuHz;
    masterHz_ = masterHz;
    Event e{t, Ev::Clocks, 0, cpuHz, masterHz};
    events_.insert(std::upper_bound(events_.begin(), events_.end(), e,
                                    [](const Event& a, const Event& b) { return a.t < b.t; }),
                   e);
}

void VotraxSc01::setInflection(uint64_t t, uint8_t inflection) {
    inflection &= 3;
    reanchor(t);
    if (inflection == infl_) return;
    infl_ = inflection;
    Event e{t, Ev::Inflection, inflection, 0, 0};
    events_.insert(std::upper_bound(events_.begin(), events_.end(), e,
                                    [](const Event& a, const Event& b) { return a.t < b.t; }),
                   e);
}

void VotraxSc01::strobe(uint64_t t, uint8_t code) {
    code &= 0x3F;
    reanchor(t);
    const uint64_t now = cycleAt(t);
    code_ = code;
    if (latchAt_ > now) {
        // A second strobe before the first was latched: the pins are read when the latch
        // happens, so the newer code is the one that counts, on the same schedule.
        for (auto it = events_.rbegin(); it != events_.rend(); ++it)
            if (it->kind == Ev::Latch) {
                it->value = code;
                break;
            }
    } else {
        latchAt_ = now + kLatchClocks;
        Event e{tAt(latchAt_), Ev::Latch, code, 0, 0};
        events_.insert(std::upper_bound(events_.begin(), events_.end(), e,
                                        [](const Event& a, const Event& b) { return a.t < b.t; }),
                       e);
    }
    arRiseAt_ = latchAt_ - kLatchClocks + phonemeClocks(code);
}

bool VotraxSc01::ar(uint64_t t) const { return cycleAt(t) >= arRiseAt_; }

uint64_t VotraxSc01::arDeadline() const {
    if (arRiseAt_ <= anchorC_) return kNever;
    return tAt(arRiseAt_);
}

// ---- the sound ----

void VotraxSc01::rebase(double t, long long cpuHz, uint32_t masterHz) {
    rAnchorChip_ = chipAt(t);
    rAnchorT_    = t;
    rCpuHz_      = cpuHz;
    rMasterHz_   = masterHz;
    rChipPerT_   = masterHz / (18.0 * static_cast<double>(cpuHz));
    outOrigin_   = t;
    outN_        = 0;
}

void VotraxSc01::apply(const Event& e, double atT) {
    switch (e.kind) {
    case Ev::Latch:
        latch(e.value);
        break;
    case Ev::Inflection:
        rInfl_ = e.value;
        break;
    case Ev::Clocks:
        rebase(atT, e.cpuHz, e.masterHz);
        commitFilters(true);
        break;
    }
}

void VotraxSc01::applyUpTo(double t) {
    size_t n = 0;
    while (n < events_.size() && static_cast<double>(events_[n].t) <= t) {
        apply(events_[n], t);
        ++n;
    }
    if (n) events_.erase(events_.begin(), events_.begin() + static_cast<long>(n));
}

void VotraxSc01::render(uint64_t untilT, int rate, std::vector<int16_t>& out) {
    if (rate <= 0) return;
    reanchor(untilT);
    if (rate != outRate_) {
        outOrigin_ += static_cast<double>(outN_) * static_cast<double>(rCpuHz_) /
                      (outRate_ > 0 ? outRate_ : rate);
        outN_    = 0;
        outRate_ = rate;
    }
    const double until = static_cast<double>(untilT);
    for (;;) {
        double step  = static_cast<double>(rCpuHz_) / rate;
        double start = outOrigin_ + static_cast<double>(outN_) * step;
        if (start + step > until) break;
        applyUpTo(start);
        step  = static_cast<double>(rCpuHz_) / rate;
        start = outOrigin_ + static_cast<double>(outN_) * step;
        const double end = outOrigin_ + static_cast<double>(outN_ + 1) * step;
        if (end > until) break;

        // The average of the chip's output over the sample: each chip sample holds its
        // level until the next one.
        const double target = chipAt(end);
        const double from   = chipPos_;
        double acc = 0;
        while (chipPos_ < target) {
            const double edge = std::floor(chipPos_) + 1.0;
            if (edge <= target) {
                acc += (edge - chipPos_) * chipOut_;
                chipPos_ = edge;
                chipOut_ = chipSample();
            } else {
                acc += (target - chipPos_) * chipOut_;
                chipPos_ = target;
            }
        }
        const double level = target > from ? acc / (target - from) : chipOut_;
        const double s = std::clamp(level * kOutGain, -32767.0, 32767.0);
        out.push_back(static_cast<int16_t>(std::lround(s)));
        ++outN_;
    }
}

void VotraxSc01::skipTo(uint64_t t) {
    reanchor(t);
    const double at = static_cast<double>(t);
    applyUpTo(at);
    rAnchorChip_ = chipAt(at);
    rAnchorT_    = at;
    chipPos_     = rAnchorChip_;
    outOrigin_   = at;
    outN_        = 0;
}

double VotraxSc01::chipSample() {
    // The digital half runs at master / 36, every other sample.
    if ((chipN_++ & 1) == 0) tick();
    return analog();
}

void VotraxSc01::latch(uint8_t code) {
    const sc01rom::Phone p = sc01rom::lookup(code);
    romF1_  = p.f1;
    romF2_  = p.f2;
    romF2q_ = p.f2q;
    romF3_  = p.f3;
    romFc_  = p.fc;
    romVa_  = p.va;
    romFa_  = p.fa;
    romVd_  = p.vd;
    romCld_ = p.cld;
    romCl_  = p.cl;
    romDur_ = p.duration;
    // The pause phonemes freeze the formants (the patent's PAC bit). On the die it is
    // wired from the code, not stored in the ROM (MAME votrax.cpp).
    romPause_  = code == 0x03 || code == 0x3E;
    phoneTick_ = 0;
    ticks_     = 0;
    if (romCld_ == 0) closed_ = romCl_;
}

void VotraxSc01::tick() {
    // The phoneme counter: 16 ticks, each of (4 x duration + 1) cycles (patent Fig. 7).
    if (ticks_ != 16) {
        if (++phoneTick_ == ((romDur_ << 2) | 1)) {
            phoneTick_ = 0;
            ++ticks_;
            // CL takes effect at the closure delay (patent: CLD times FA and CL).
            if (ticks_ == romCld_) closed_ = romCl_;
        }
    }

    // The transition clocks: master / 1152 (625 Hz at 720 kHz) and master / 3456
    // (208 Hz), phased so the slow one falls between two fast ones (die, MAME votrax.cpp).
    if (++update_ == 48) update_ = 0;
    const bool fast = (update_ & 15) == 0;
    const bool slow = update_ == 40;

    // The formants move on the slow clock. A pause phoneme freezes them while there is
    // still sound (patent: FF = PAC . (VA + FA)).
    if (slow && (!romPause_ || !(fltFa_ || fltVa_))) {
        approach(curF1_, romF1_);
        approach(curF2_, romF2_);
        approach(curF2q_, romF2q_);
        approach(curF3_, romF3_);
    }
    // The amplitudes move on the fast clock, each after its delay (patent Fig. 7: CLD
    // holds FA, VD holds VA). FC goes with FA: it routes the noise.
    if (fast) {
        if (ticks_ >= romCld_) {
            approach(curFa_, romFa_);
            approach(curFc_, romFc_);
        }
        if (ticks_ >= romVd_) approach(curVa_, romVa_);
    }

    // The closure: when CL is set, or when there is nothing to hear, the output is ramped
    // shut over 28 ticks; otherwise it is open.
    if (!closed_ && (fltFa_ || fltVa_)) closure_ = 0;
    else if (closure_ != 28) ++closure_;

    // The pitch counter (patent Fig. 6): an 8-bit count at master / 36, preset from I1, I2
    // and the inverse of F1. Counting up from the preset to the carry is a period of
    // 256 - preset = 226 + 2 F1 - 32 I1 - 64 I2 counts.
    period_ = static_cast<uint16_t>((0xE0 ^ (rInfl_ << 5) ^ (fltF1_ << 1)) + 2);
    pitch_  = static_cast<uint8_t>(pitch_ + 1);
    if (pitch_ == period_) pitch_ = 0;

    // The filters take the transition's values at the start of a glottal pulse, so the
    // switches do not click while the tract rings (patent col. 14).
    if ((pitch_ & 0xF9) == 0x08) commitFilters(false);

    // The noise: a 15-bit shift register with an XNOR of its last two stages (die, MAME
    // votrax.cpp; the patent drew an 18-stage 4006).
    const bool in = noiseBit_ && noise_ != 0x7FFF;
    noise_    = static_cast<uint16_t>(((noise_ << 1) & 0x7FFE) | (in ? 1 : 0));
    noiseBit_ = (((noise_ >> 14) ^ (noise_ >> 13)) & 1) == 0;
}

void VotraxSc01::commitFilters(bool force) {
    fltFa_ = curFa_ >> 4;
    fltFc_ = curFc_ >> 4;
    fltVa_ = curVa_ >> 4;

    const double fc = rMasterHz_ / 36.0;   // the switching rate
    const double fs = rMasterHz_ / 18.0;   // the rate the filters run at

    if (force || fltF1_ != curF1_ >> 4) {
        fltF1_ = curF1_ >> 4;
        resonator(f1_.b0, f1_.b1, f1_.b2, f1_.a1, f1_.a2,
                  11247, 11797, 949, 52067,
                  2280 + bank(fltF1_, {2546, 4973, 9861, 19724}), 166272, fc, fs);
    }
    // F2 carries a fifth bit, the transition's half step (patent Fig. 3B).
    if (force || fltF2_ != curF2_ >> 3 || fltF2q_ != curF2q_ >> 4) {
        fltF2_  = curF2_ >> 3;
        fltF2q_ = curF2q_ >> 4;
        const double s2 = 829 + bank(fltF2q_, {1390, 2965, 5875, 11297});
        const double sx = 2352 + bank(fltF2_, {833, 1663, 3164, 6327, 12654});
        resonator(f2v_.b0, f2v_.b1, f2v_.b2, f2v_.a1, f2v_.a2,
                  24840, 29154, s2, 38180, sx, 34270, fc, fs);
        shelf(f2n_.b0, f2n_.b1, f2n_.a1, 29154, s2, 38180, sx, fc, fs);
        f2n_.b2 = f2n_.a2 = 0;
    }
    if (force || fltF3_ != curF3_ >> 4) {
        fltF3_ = curF3_ >> 4;
        resonator(f3_.b0, f3_.b1, f3_.b2, f3_.a1, f3_.a2,
                  0, 17594, 868, 18828,
                  8480 + bank(fltF3_, {2226, 4485, 9056, 18111}), 50019, fc, fs);
    }
    if (force) {
        // F4 is fixed.
        resonator(f4_.b0, f4_.b1, f4_.b2, f4_.a1, f4_.a2,
                  0, 28810, 1165, 21457, 8558, 7289, fc, fs);
        lowpass(out_.b0, out_.b1, out_.a1, fc, fs);
        out_.b2 = out_.a2 = 0;
    }
}

double VotraxSc01::analog() {
    // The voice: the glottal pulse, scaled by VA, through F1 and F2.
    double v = pitch_ < 72 ? kGlottal[pitch_ >> 3] : 0.0;
    v = f2v_.run(f1_.run(v * fltVa_ / 15.0));

    // The noise, scaled by FA. In a voiced phoneme it is let through only in the second
    // half of each glottal period (patent Fig. 8: FGATE or not VA).
    const bool gate = fltVa_ == 0 || pitch_ >= period_ / 2;
    const double n = gate ? (noiseBit_ ? kNoise : -kNoise) * fltFa_ / 15.0 : 0.0;

    // FC splits the noise: that much goes into F2, the rest joins after F3 (patent Fig. 4,
    // the FC and FC-bar banks).
    double vn = f3_.run(v + f2n_.run(n * fltFc_ / 15.0));
    vn += n * (15 - fltFc_) / 15.0;

    // F4, then the closure, then the output stage.
    vn = f4_.run(vn) * (7 - (closure_ >> 2)) / 7.0;
    return out_.run(vn);
}

// ---- lifecycle ----

void VotraxSc01::resetSound(uint64_t t) {
    events_.clear();
    rInfl_ = infl_;
    latch(code_);
    curF1_ = curF2_ = curF2q_ = curF3_ = 0;
    curFc_ = curVa_ = curFa_ = 0;
    update_   = 0;
    pitch_    = 0;
    period_   = 256;
    closure_  = 28;
    closed_   = true;
    noise_    = 0;
    noiseBit_ = false;
    for (Biquad* q : {&f1_, &f2v_, &f2n_, &f3_, &f4_, &out_}) q->quiet();

    rAnchorT_    = static_cast<double>(t);
    rAnchorChip_ = 0;
    rCpuHz_      = cpuHz_;
    rMasterHz_   = masterHz_;
    rChipPerT_   = masterHz_ / (18.0 * static_cast<double>(cpuHz_));
    outOrigin_   = static_cast<double>(t);
    outN_        = 0;
    outRate_     = 0;
    chipPos_     = 0;
    chipOut_     = 0;
    chipN_       = 0;
    commitFilters(true);
}

void VotraxSc01::power() {
    cpuHz_    = 2000000;
    masterHz_ = kStandardClockHz;
    anchorT_ = anchorC_ = anchorR_ = 0;
    code_     = 0x3F;
    infl_     = 0;
    latchAt_  = 0;
    arRiseAt_ = 0;
    resetSound(0);
}

void VotraxSc01::serialize(StateWriter& w) const {
    w.u8(code_);
    w.u8(infl_);
    w.u64(static_cast<uint64_t>(cpuHz_));
    w.u32(masterHz_);
    w.u64(anchorT_);
    w.u64(anchorC_);
    w.u64(anchorR_);
    w.u64(latchAt_);
    w.u64(arRiseAt_);
}

void VotraxSc01::deserialize(StateReader& r) {
    code_     = r.u8() & 0x3F;
    infl_     = r.u8() & 3;
    cpuHz_    = static_cast<long long>(r.u64());
    masterHz_ = r.u32();
    anchorT_  = r.u64();
    anchorC_  = r.u64();
    anchorR_  = r.u64();
    latchAt_  = r.u64();
    arRiseAt_ = r.u64();
    if (cpuHz_ < 1) cpuHz_ = 1;
    if (masterHz_ < 1) masterHz_ = 1;
    if (anchorR_ >= static_cast<uint64_t>(cpuHz_)) anchorR_ = 0;
    resetSound(anchorT_);
}

} // namespace altair
