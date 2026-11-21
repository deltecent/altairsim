#pragma once
//
// Speaker -- one voice: a held level that a guest moves, played through the host Audio
// service (host/audio.h, DESIGN.md 7.4).
//
// This is the part every D/A-driven speaker shares, so a board does not write it again:
// the Cromemco D+7A's JS-1 speakers and the Newtech Model 6 are both a latch into a D/A
// that a program writes in a timed loop. The board keeps the latch. This keeps the sound
// that the latch has been sent and is not yet rendered, and the rules for when a slice of
// emulated time becomes sound and when it is dropped (see pump()).
//
// A BOARD CALLS IT IN TWO PLACES. In write(), edge() with the T-state and the new level:
// that is all a bus cycle does. In Board::pump(), pump(): render the slice and push it.
// Nothing is pushed from inside a bus cycle.
//
// THE ADDRESS OF A Speaker IS ITS Audio::Owner. One Speaker is one voice, and the service
// mixes the voices, so a board with two speakers holds two of these.
//
// SOUND NEEDS A CRYSTAL. The samples are made at one second of sound per EMULATED second.
// A machine running flat out (Clock::free()) has no fixed relation to real time, so
// nothing is played then, and status() says why.
// With a crystal every run keeps real time, script or terminal (#731), so the sound
// is the sound wherever the machine is started from.

#include "host/level_pcm.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class Audio;
class Clock;

// ---------------------------------------------------------------------------
// THE RULES FOR ONE VOICE'S HOST TURN, shared by every board that plays sound: a Speaker
// below, and a voice that is not a held level (the PAiA SS-100's SC-01). The board keeps
// its own sound; these say what a slice of emulated time becomes. The reasons are with
// planSlice() in speaker.cpp.
// ---------------------------------------------------------------------------
struct VoiceSlice {
    enum class Act {
        Skip,     // leave the time up to now unplayed
        Wait,     // no time has gone by: do nothing
        Render,   // make the samples up to now and push them
    };
    Act    act     = Act::Skip;
    size_t cushion = 0;   // Render: samples of rest to push first (the device ran dry)
};

// Could a voice play now? A sound service with a device that has not failed, the voice
// `wired`, and a crystal. The guest driving it is the board's own question.
bool voiceCanPlay(const Clock& clock, bool wired);

// One slice for the voice `owner`, which has rendered up to T-state `from`. `playing` is
// voiceCanPlay() and the guest driving the voice.
VoiceSlice planSlice(const Clock& clock, uint64_t from, bool playing, const void* owner);

// For SHOW: "-> playing", "-> silent", or the reason nothing can play.
std::string voiceStatus(const Clock* clock, bool driven);

class Speaker {
public:
    // The host sound service, wired once in main.cpp / tests/main.cpp -- an SdlAudio in
    // the shipping binary, a NullAudio headless, a stub in a test. Borrowed; the
    // composition root owns it. One service for every speaker on every board.
    static void   setAudio(Audio* a);
    static Audio* audio();

    // The guest moved the level to `level` at T-state `t`. From Board::write().
    void edge(uint64_t t, int8_t level);

    // The level moved and no program moved it (a bus reset clearing a latch). The change
    // is heard if the speaker is playing, and it does not count as the guest driving it.
    void drop(uint64_t t, int8_t level);

    // The host turn. One slice of emulated time becomes the same length of sound, or
    // nothing. `wired` false is a speaker that is not connected: the slice is dropped.
    void pump(const Clock& clock, bool wired);

    // Has the guest moved the level in the last half second of emulated time? A level
    // that only sits there is silence, and is not sent to the device.
    bool driven(const Clock& clock) const;

    // Would a slice of time go to the sound device now? A sound service with a device
    // that has not failed, the speaker `wired`, a crystal, and the guest driving it.
    // This is pump()'s own test.
    bool playing(const Clock& clock, bool wired) const;

    // Start again from `level` at T-state `now`, with nothing waiting to be rendered:
    // after a strap change, and after a RESTORE moved the clock under the board.
    void resync(uint64_t now, int8_t level);

    // Power-on: 0 V, time zero, never driven.
    void clear();

    // The capacitor between the D/A and the amplifier, as a high-pass with its corner at
    // `cornerHz`. 0 is none (the default): the level goes to the device as it is. With a
    // capacitor, a level that sits anywhere is 0 V at the amplifier, so a wave that goes
    // between a value and zero starts and stops with no step.
    void setAcCoupled(double cornerHz) { cornerHz_ = cornerHz; }

    // For SHOW: "-> playing", "-> silent", or the reason nothing can play.
    std::string status(const Clock* clock) const;

    // What the latch holds now, as the level last given.
    int8_t level() const { return pcm_.level(); }

private:
    // Leave the time up to `now` unplayed.
    void skip(uint64_t now);

    // Run the samples in buf_ from `from` on through the capacitor.
    void couple(size_t from, int rate);

    LevelPcm pcm_;                  // the level against emulated time
    bool     heard_      = false;   // has the guest moved it since power-on
    uint64_t lastChange_ = 0;       // T-state of the guest's last change
    std::vector<int16_t> buf_;      // pump()'s scratch, kept to save the allocation

    // The capacitor: the corner, the last sample that went in, the last that came out.
    double cornerHz_ = 0;
    double in_       = 0;
    double out_      = 0;
};

} // namespace altair
