#include "boards/paia-ss100.h"

#include "core/statefile.h"
#include "core/value.h"
#include "host/audio.h"
#include "host/speaker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace altair {
namespace {

constexpr double kPi = 3.14159265358979323846;

// ---- the clock's current source (Fig. 4) ----
// Q1's base sits on the R15/R14 divider from +5 V. Each XF bit that is LOW pulls more
// current from Q1's emitter through its diode (D3-D6) and resistor, and so does MODE low
// through D7, R20 and the R21 trim pot. Q1's collector current crosses R22; Q2 mirrors it
// through R23 into C5 and the SC-01's clock pin, and the frequency follows the current.
constexpr double kR14 = 3300, kR15 = 1000, kR20 = 620;
constexpr double kR22 = 1000, kR23 = 2700, kR24 = 3300;
constexpr double kXfR[4] = {82000, 39000, 20000, 10000};               // R16-R19, b0-b3
constexpr double kVbe = 0.65, kVdiode = 0.65, kVol = 0.2;              // a 74LS273 low
constexpr uint8_t kCalibration = 0x08;   // "output xxx01000 ... adjust R21 for 720 kHz"

double sourceCurrent(uint8_t control, double r21) {
    const double ve    = 5.0 * kR14 / (kR14 + kR15) - kVbe;
    const double drive = ve - kVdiode - kVol;
    double ie = ve / kR24;
    for (int b = 0; b < 4; ++b)
        if (!(control & (1u << b))) ie += drive / kXfR[b];
    if (!(control & 0x10)) ie += drive / (kR20 + r21);
    return std::max(0.0, (ie * kR22 - kVbe) / kR23);
}

// ---- the audio path (Fig. 4) ----
// The SC-01's output goes through C6 and R25 to R26, then R30 and the LM386. C7 to ground
// through Q3 makes the speech-mode low-pass. IC9A (b6) pulls the R26/R30 node to ground
// through R28 and IC9B (b5) through R29: the volume divider.
constexpr double kR25 = 1000, kR26 = 47000, kR28 = 1500, kR29 = 10000, kR30 = 10000;
constexpr double kC7  = 0.047e-6;

double parallel(double a, double b) { return a * b / (a + b); }

double divider(double shunt) { return shunt / (kR26 + shunt); }

const char* const kVolume[4] = {"full", "medium", "low", "off"};

} // namespace

PaiaSs100::PaiaSs100() { chip_.power(); }

PaiaSs100::~PaiaSs100() {
    if (clock_) clock_->cancel(arWake_);
}

uint32_t PaiaSs100::masterHz(uint8_t control, uint32_t r21) {
    // The scale is fixed by the booklet's calibration with R21 at mid-travel. A board whose
    // R21 sits elsewhere runs at other frequencies, 08 included.
    const double f = VotraxSc01::kStandardClockHz * sourceCurrent(control, r21) /
                     sourceCurrent(kCalibration, kR21Mid);
    return (uint32_t)std::max(1.0, std::round(f));
}

double PaiaSs100::volumeGain(uint8_t control) {
    double shunt = kR30;
    switch ((control >> 5) & 3) {
    case 0: break;
    case 1: shunt = parallel(shunt, kR29); break;
    case 2: shunt = parallel(shunt, kR28); break;
    default: return 0;   // Q4 shuts the amplifier down
    }
    return divider(shunt) / divider(kR30);
}

// ---------------------------------------------------------------------------
// Bus: four output strobes from BASE; two are used.
// ---------------------------------------------------------------------------
bool PaiaSs100::decodes(const BusCycle& c) const {
    if (!enabled_ || c.type != Cycle::IoWrite) return false;
    const uint8_t p = c.port();
    return p >= base_ && p < (uint8_t)(base_ + 4);
}

void PaiaSs100::write(const BusCycle& c) {
    const uint64_t now = clock_ ? clock_->now() : 0;
    switch (c.port() - base_) {
    case 0:   // SPORT: IC8 takes the inflection, and the SC-01 the phoneme, on one strobe
        syncClocks(now);
        pitch_ = (uint8_t)(c.data >> 6);
        chip_.setInflection(now, pitch_);
        chip_.strobe(now, c.data);
        arHigh_ = false;
        spoke_  = true;
        armAr();
        intChanged();
        break;
    case 1:   // PPORT: IC10
        syncClocks(now);
        control_ = c.data;
        syncClocks(now);   // the new master clock, from now
        noteControl(now);
        armAr();
        intChanged();
        break;
    default:  // n2, n3: decoded, not used
        break;
    }
}

bool PaiaSs100::assertsInt() const { return irq_ == IrqJumper::Int && requesting(); }

uint8_t PaiaSs100::assertsVi() const { return requesting() ? viBit(irq_) : 0; }

// ---------------------------------------------------------------------------
// A/R.
// ---------------------------------------------------------------------------
void PaiaSs100::syncClocks(uint64_t t) {
    if (!clock_) return;
    // A change of the CPU's rate since the last look counts from when it happened.
    chip_.setClocks(clock_->rateChangedAt(), clock_->hz(), masterHz(control_, r21_));
    chip_.setClocks(t, clock_->hz(), masterHz(control_, r21_));
}

void PaiaSs100::armAr() {
    if (!clock_) return;
    clock_->cancel(arWake_);
    arWake_ = Clock::kNone;
    const uint64_t due = chip_.arDeadline();
    if (arHigh_ || due == VotraxSc01::kNever) return;
    quietAt_ = due;
    arWake_  = clock_->at(due, [this] { arRose(); });
}

void PaiaSs100::arRose() {
    arWake_ = Clock::kNone;
    const uint64_t now = clock_->now();
    syncClocks(now);
    if (!chip_.ar(now)) {   // the CPU's rate moved the deadline
        armAr();
        return;
    }
    arHigh_ = true;
    intChanged();
}

// ---------------------------------------------------------------------------
// Lifecycle.
// ---------------------------------------------------------------------------
void PaiaSs100::reset(Reset r) {
    if (r != Reset::PowerOn) return;   // RESET* does not reach the board; POC* does
    const uint64_t now = clock_ ? clock_->now() : 0;
    syncClocks(now);
    control_ = 0;
    pitch_   = 0;
    syncClocks(now);
    chip_.setInflection(now, 0);
    noteControl(now);
    armAr();
    intChanged();
}

void PaiaSs100::power() {
    if (clock_) clock_->cancel(arWake_);
    arWake_ = Clock::kNone;
    chip_.power();
    control_ = 0;
    pitch_   = 0;
    arHigh_  = true;
    ctlChanges_.clear();
    heardCtl_   = 0;
    renderedTo_ = 0;
    quietAt_    = 0;
    spoke_      = false;
    lowpass_    = 0;
    syncClocks(0);
    intChanged();
}

void PaiaSs100::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u8(control_);
    w.u8(pitch_);
    chip_.serialize(w);
}

void PaiaSs100::deserialize(StateReader& r) {
    Board::deserialize(r);
    control_ = r.u8();
    pitch_   = (uint8_t)(r.u8() & 3);
    chip_.deserialize(r);
    const uint64_t now = clock_ ? clock_->now() : 0;
    if (clock_) clock_->cancel(arWake_);
    arWake_ = Clock::kNone;
    syncClocks(now);
    arHigh_ = chip_.ar(now);
    spoke_  = !arHigh_;
    armAr();
    skipSound(now);
    intChanged();
}

// ---------------------------------------------------------------------------
// The sound, in the host turn.
// ---------------------------------------------------------------------------
bool PaiaSs100::driven(uint64_t now) const {
    // Speaking, or within half a second of the last phoneme's end: the filters ring.
    return spoke_ && clock_ && now <= quietAt_ + (uint64_t)clock_->hz() / 2;
}

void PaiaSs100::noteControl(uint64_t now) {
    // A voice that is quiet hears the new word from the start of the next slice: the
    // time before it is silence at any volume.
    if (!driven(now)) {
        ctlChanges_.clear();
        heardCtl_ = control_;
        return;
    }
    ctlChanges_.push_back({now, control_});
}

void PaiaSs100::skipSound(uint64_t now) {
    chip_.skipTo(now);
    renderedTo_ = now;
    heardCtl_   = control_;
    ctlChanges_.clear();
    lowpass_ = 0;
}

void PaiaSs100::renderTo(uint64_t t, uint8_t ctl) {
    Audio* audio = Speaker::audio();
    const int    rate  = audio->rate();
    const size_t first = buf_.size();
    chip_.render(t, rate, buf_);

    const double fc   = 1.0 / (2 * kPi * parallel(kR25, kR26 + kR30) * kC7);
    const double a    = 1.0 - std::exp(-2 * kPi * fc / rate);
    const double gain = volumeGain(ctl);
    for (size_t i = first; i < buf_.size(); ++i) {
        const double x = buf_[i];
        lowpass_ = (ctl & kMode) ? x : lowpass_ + a * (x - lowpass_);
        buf_[i]  = (int16_t)std::clamp(std::lround(lowpass_ * gain), -32767L, 32767L);
    }
}

void PaiaSs100::pump() {
    if (!clock_) return;
    const uint64_t now = clock_->now();
    syncClocks(now);

    const bool wired = ((control_ >> 5) & 3) != 3 ||
                       std::any_of(ctlChanges_.begin(), ctlChanges_.end(),
                                   [](const CtlChange& c) { return ((c.control >> 5) & 3) != 3; });
    const bool playing = voiceCanPlay(*clock_, wired) && driven(now);
    const VoiceSlice slice = planSlice(*clock_, renderedTo_, playing, this);
    if (slice.act == VoiceSlice::Act::Skip) {
        skipSound(now);
        return;
    }
    if (slice.act == VoiceSlice::Act::Wait) return;

    buf_.assign(slice.cushion, 0);
    for (const CtlChange& c : ctlChanges_) {
        renderTo(c.t, heardCtl_);
        heardCtl_ = c.control;
    }
    ctlChanges_.clear();
    renderTo(now, heardCtl_);
    renderedTo_ = now;
    if (!buf_.empty()) Speaker::audio()->push(this, buf_);
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> PaiaSs100::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Base of the 4-port block (S1-S4 set A7-A4): data at BASE, control at "
                  "BASE+1. A multiple of 10; default 30";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xF0;
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string& err) {
            if (v.i() & 0x0F) {
                err = "S1-S4 set only A7-A4, and A3-A2 must be 0 -- the base must be a "
                      "multiple of 10";
                return false;
            }
            base_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "r21";
        x.help  = "The R21 trim pot, in ohms (0-1000): it sets the master clock in speech "
                  "mode. Default 500, mid-travel: 720 kHz at XF8, as the booklet calibrates it";
        x.kind  = Kind::Int;
        x.min   = 0;
        x.max   = 1000;
        x.get   = [this] { return Value::ofInt(r21_); };
        x.set   = [this](const Value& v, std::string&) {
            const uint64_t now = clock_ ? clock_->now() : 0;
            syncClocks(now);
            r21_ = (uint32_t)v.i();
            syncClocks(now);   // the new master clock, from now
            armAr();
            return true;
        };
        p.push_back(std::move(x));
    }
    p.push_back(irqJumperProperty(
        "interrupt",
        "Where the A/R request goes (the jumper): vi0-vi7, or int for PINT. Default vi0, "
        "RST 7 as PAiA's software expects",
        irq_));
    return p;
}

std::vector<std::string> PaiaSs100::statusLines() const {
    const uint64_t now = clock_ ? clock_->now() : 0;
    char line[128];
    std::vector<std::string> out;
    std::snprintf(line, sizeof line, "phoneme   %02X %-5s inflection %u   A/R %s",
                  chip_.phoneme(), VotraxSc01::name(chip_.phoneme()), pitch_,
                  arHigh_ ? "high (ready)" : "low (speaking)");
    out.emplace_back(line);
    std::snprintf(line, sizeof line,
                  "control   %02X  interrupt %s, volume %s, %s mode, XF%u = %.1f kHz",
                  control_, (control_ & kEnable) ? "enabled" : "disabled",
                  kVolume[(control_ >> 5) & 3], (control_ & kMode) ? "effects" : "speech",
                  control_ & 0x0Fu, masterHz(control_, r21_) / 1000.0);
    out.emplace_back(line);
    out.push_back("sound     " + voiceStatus(clock_, driven(now)));
    return out;
}

std::vector<MapEntry> PaiaSs100::ioMap() const {
    return {
        {(uint32_t)base_, (uint32_t)base_, "write",
         "SS-100 -- data: phoneme (b0-5), inflection I1/I2 (b6-7)"},
        {(uint32_t)(base_ + 1), (uint32_t)(base_ + 1), "write",
         "SS-100 -- control: enable, volume, mode, master clock"},
    };
}

} // namespace altair
