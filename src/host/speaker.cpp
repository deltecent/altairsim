#include "host/speaker.h"

#include "core/clock.h"
#include "host/audio.h"

#include <cmath>

namespace altair {
namespace {

// The injected host sound service (setAudio), borrowed. Null on the bench; a NullAudio
// headless, which takes the samples and plays nothing.
Audio* g_audio = nullptr;

constexpr double kPi = 3.14159265358979323846;

} // namespace

void   Speaker::setAudio(Audio* a) { g_audio = a; }
Audio* Speaker::audio() { return g_audio; }

void Speaker::edge(uint64_t t, int8_t level) {
    pcm_.edge(t, level);
    heard_      = true;
    lastChange_ = t;
}

void Speaker::drop(uint64_t t, int8_t level) { pcm_.edge(t, level); }

bool Speaker::driven(const Clock& clock) const {
    if (!heard_) return false;
    const uint64_t now = clock.now();
    return now >= lastChange_ && now - lastChange_ <= (uint64_t)clock.hz() / 2;
}

bool Speaker::playing(const Clock& clock, bool wired) const {
    return voiceCanPlay(clock, wired) && driven(clock);
}

void Speaker::resync(uint64_t now, int8_t level) {
    pcm_.clear();
    pcm_.edge(0, level);
    skip(now);
    heard_      = false;
    lastChange_ = 0;
}

void Speaker::clear() {
    pcm_.clear();
    heard_      = false;
    lastChange_ = 0;
    in_         = 0;
    out_        = 0;
}

// Time that is not played. The capacitor has had that time to settle: whatever level is
// held now is 0 V on the far side of it.
void Speaker::skip(uint64_t now) {
    pcm_.skipTo(now);
    in_  = (double)pcm_.held() * LevelPcm::kGain;
    out_ = 0;
}

// One pole: out[n] = a * (out[n-1] + in[n] - in[n-1]).
void Speaker::couple(size_t from, int rate) {
    const double a = 1.0 / (1.0 + 2.0 * kPi * cornerHz_ / (double)rate);
    for (size_t i = from; i < buf_.size(); ++i) {
        const double x = (double)buf_[i];
        out_ = a * (out_ + x - in_);
        in_  = x;
        double y = std::round(out_);
        if (y > 32767) y = 32767;
        if (y < -32768) y = -32768;
        buf_[i] = (int16_t)y;
    }
}

// ---------------------------------------------------------------------------
// One slice of emulated time becomes the same length of sound, or nothing.
//
// A device plays rate() samples each REAL second and a voice renders rate() samples each
// EMULATED second, so the two must run at the same speed for the sound to be right.
// Each rule below is one way they do not:
//
//   flat out        no crystal: emulated time has no fixed relation to real time, and
//                   the slice is dropped. Nothing is played, at any pitch.
//   not driven      a voice the guest is not driving is silence. Nothing is pushed, so a
//                   guest that never touches it never opens a sound device.
//   no device       the host's device failed to open. The slice is dropped, and the run
//                   is not held to the crystal for a sound nobody can hear.
//   queue empty     the device has caught up (or has not started). A short cushion goes
//                   in first, or the device would run dry between this slice and the
//                   next and every slice would start with a gap.
//   queue deep      the machine is ahead of the device -- a run that nothing paces, with
//                   a crystal set. The slice is dropped, so the delay cannot grow.
// ---------------------------------------------------------------------------
bool voiceCanPlay(const Clock& clock, bool wired) {
    return g_audio && g_audio->available() && wired && !clock.free();
}

VoiceSlice planSlice(const Clock& clock, uint64_t from, bool playing, const void* owner) {
    const uint64_t now = clock.now();
    const uint64_t hz  = (uint64_t)clock.hz();

    // More than a second since the last render is not a slice: the clock was moved under
    // the board. Never render it -- it could be hours of samples.
    const bool moved = now < from || now - from > hz;
    if (moved || !playing) return {VoiceSlice::Act::Skip, 0};

    // No time has gone by: the machine is stopped at the prompt, and a stopped machine
    // makes no sound. (Without this, an empty queue would get a cushion at every call.)
    if (now == from) return {VoiceSlice::Act::Wait, 0};

    const size_t rate   = (size_t)g_audio->rate();
    const size_t queued = g_audio->queued(owner);
    if (queued > rate / 4) return {VoiceSlice::Act::Skip, 0};  // a quarter second waiting
    return {VoiceSlice::Act::Render, queued == 0 ? rate / 20 : 0};  // 50 ms of rest
}

std::string voiceStatus(const Clock* clock, bool driven) {
    if (!g_audio) return "-> (no audio service in this build)";
    if (!g_audio->available()) return "-> no sound device";
    if (clock && clock->free())
        return "-> silent: the machine has no crystal (clock_hz = 0)";
    return driven ? "-> playing" : "-> silent";
}

void Speaker::pump(const Clock& clock, bool wired) {
    const uint64_t     now   = clock.now();
    const VoiceSlice   slice = planSlice(clock, pcm_.renderedTo(), playing(clock, wired), this);
    if (slice.act == VoiceSlice::Act::Skip) {
        skip(now);
        return;
    }
    if (slice.act == VoiceSlice::Act::Wait) return;

    buf_.clear();
    if (slice.cushion) {      // at the level the slice starts from
        const double rest = cornerHz_ > 0 ? out_ : (double)pcm_.held() * LevelPcm::kGain;
        buf_.assign(slice.cushion, (int16_t)rest);
    }
    const size_t first = buf_.size();
    pcm_.render(now, clock.hz(), g_audio->rate(), buf_);
    if (cornerHz_ > 0) couple(first, g_audio->rate());
    if (!buf_.empty()) g_audio->push(this, buf_);
}

std::string Speaker::status(const Clock* clock) const {
    return voiceStatus(clock, clock && driven(*clock));
}

} // namespace altair
