#pragma once
//
// Volume -- the last step of a voice before the host Audio service (host/audio.h,
// DESIGN.md 7.4): a gain the user sets, then a soft limiter, then 16-bit PCM.
//
// IT IS A HOST SETTING, NOT PART OF THE BOARD. A board renders what its hardware puts out;
// how loud that is on the host's speakers is the host's business, the same as the window
// size. So the `volume` property changes no register, no timing and nothing a guest can
// see, and a SNAPSHOT does not carry it.
//
// THE LIMITER IS ALWAYS ON. Below the knee (0.9 of full scale) a sample goes through
// as it is. Above it, the sample is bent towards full scale and never reaches past it, so
// a gain that is too high, or a capacitor's overshoot at the first edge, rounds off and
// does not clip. It looks at one sample at a time and keeps no state, so it cannot pump,
// and the sound does not depend on how the time is cut into slices.

#include "core/value.h"

#include <cstdint>
#include <functional>
#include <span>

namespace altair {

class Volume {
public:
    static constexpr int kMinDb = -40;
    static constexpr int kMaxDb = 24;

    // Set the gain in whole dB. False, and nothing changes, outside kMinDb..kMaxDb.
    bool set(int db);
    int  db() const { return db_; }

    // One sample, in 16-bit units (it may be past full scale): the gain, the limiter, and
    // the nearest 16-bit value.
    int16_t operator()(double x) const;

    // The same, in place, for samples already in 16 bits.
    void apply(std::span<int16_t> samples) const;

private:
    int    db_   = 0;
    double gain_ = 1.0;
};

// The `volume` property, the same on every board that makes sound. `changed` runs after a
// good value is stored (a board with two speakers copies it to the second).
Property volumeProperty(Volume& v, std::function<void()> changed = {});

} // namespace altair
