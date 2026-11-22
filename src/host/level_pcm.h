#pragma once
//
// LevelPcm -- a held level that changes at emulated instants, rendered to fixed-rate PCM.
//
// This is the conversion a D/A-driven speaker needs. The guest writes a level to a latch
// at a T-state; the latch holds it until the next write; a sound device wants a sample
// every 1/rate of a second. Neither a board nor a host backend: no Clock, no Audio, no
// SDL. A board feeds it edges and asks for samples; a test does the same with numbers.
//
// EACH SAMPLE IS THE AVERAGE OF THE LEVEL OVER THAT SAMPLE'S TIME, not the level at one
// instant in it. A guest's square wave has its edges wherever its timing loop puts them,
// almost never on a sample boundary. Taking the level at one instant moves every edge to
// a boundary, and the error is a tone of its own (an alias) under the real one. The
// average puts an edge that falls 30% into a sample at 30% of the step.
//
// TIME IS COUNTED IN WHOLE UNITS, so slices join exactly. One unit is 1/(hz * rate) of a
// second: a T-state is `rate` units and a sample is `hz` units. The part of a sample that
// a render() call leaves unfinished is carried to the next call, so rendering one second
// in a thousand calls gives the same samples as rendering it in one -- no click at a
// slice boundary, and no drift however long the machine runs.

#include <cstdint>
#include <vector>

namespace altair {

class LevelPcm {
public:
    // One step of the 8-bit level in the 16-bit sample. +127 -> 32512 and -128 -> -32768:
    // the D/A's full swing is the whole 16-bit range, as loud as other sound on the host.
    // The user's `volume` (host/volume.h) comes after this, and its limiter rounds off
    // anything past full scale.
    static constexpr int kGain = 256;

    // The level changes to `level` at T-state `t`. Edges arrive in time order (the clock
    // only runs forward); one earlier than the rendered point takes effect at that point.
    void edge(uint64_t t, int8_t level);

    // Append to `out` every sample that COMPLETES by T-state `untilT`, with `hz` T-states
    // and `rate` samples to the second. A sample still in progress stays here for the next
    // call. An `untilT` behind the rendered point renders nothing.
    void render(uint64_t untilT, long long hz, int rate, std::vector<int16_t>& out);

    // Move the rendered point to `t` and produce nothing: the edges up to `t` take effect
    // (the level is what the last of them left), the sample in progress is dropped. For
    // time that is not to be played -- a machine running flat out, a speaker that has
    // been quiet. A `t` behind the rendered point moves it BACK (a POWER resets the clock).
    void skipTo(uint64_t t);

    // Forget everything: 0 V, time zero, no edges.
    void clear();

    // level() is the newest: what the latch holds now. held() is the level at the rendered
    // point, which is where the next sample starts.
    int8_t   level() const { return edges_.empty() ? level_ : edges_.back().level; }
    int8_t   held() const { return level_; }
    uint64_t renderedTo() const { return t_; }
    bool     pending() const { return !edges_.empty(); }

private:
    struct Edge {
        uint64_t t;
        int8_t   level;
    };

    // Hold `level_` for `dt` T-states, emitting each sample that fills.
    void hold(uint64_t dt, std::vector<int16_t>& out);

    std::vector<Edge> edges_;      // not yet rendered, in time order
    uint64_t          t_     = 0;  // rendered up to here (T-states)
    int8_t            level_ = 0;  // the level held at t_

    // The sample in progress, in units (see the header): how much of it is filled, and
    // the sum of level * units over that part.
    long long hz_    = 0;
    int       rate_  = 0;
    uint64_t  phase_ = 0;
    int64_t   sum_   = 0;
};

} // namespace altair
