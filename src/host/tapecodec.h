#pragma once
//
// The codec seam -- where a WAV stops being audio and becomes a tape (DESIGN.md 7.3).
//
// media.h already draws the line this file lives on:
//
//     TapeImage      what the bytes MEAN    (the board's business)
//     MediaFile      where the bytes ARE    (the host's business)
//
// A WAV's FILE bytes are not the tape's DATA bytes, and that difference is entirely a
// question of where the bytes are. So it belongs on the MediaFile side of the line, and
// TapeImage needs NO CHANGES AT ALL: pos(), size(), atEnd() and rewind() go on meaning
// what they meant, so SHOW's "at N of M bytes" stays literally true and REWIND still
// winds the tape rather than seeking an audio file.
//
// DECODE AT MOUNT, ENCODE AT SYNC -- which is precisely the bargain media.h already
// declares ("the whole image is read at MOUNT and written back at sync()"). The
// demodulator therefore runs ONCE, off the run loop, on the operator's thread. No DSP
// ever executes inside a bus cycle, and the guest provably cannot observe a tone.
//
// Implemented as a DECORATOR rather than a new resolver: openTapeMedia() calls
// openMedia() and either hands back what it got or wraps it. That is why there is no
// new install point, and why every existing test that hands out a MemoryMedia keeps
// passing untouched -- those buffers carry no RIFF magic, so they come back unwrapped.
//
// ---- A CARD DECLARES WHAT ITS MODEM CAN HEAR, AND REFUSES THE REST -----------------
//
// This is the rule that matters, and it is NOT "try everything and keep what sticks".
//
// A board names the format(s) its physical hardware demodulates. A tape in any other
// modulation is REFUSED, with a message saying what the tape actually is. Not all
// published Altair cassette audio is in the 88-ACR's modulation -- the archives hold
// Kansas City tapes too -- and the 88-ACR physically cannot read those: its PLL sits at
// 2125 Hz and takes about +/-100 Hz, and a 1200 Hz space tone is some 925 Hz outside
// that. A real card fed such a tape does not read it badly; it reads NOTHING.
//
// A simulator that decoded it anyway would hand the guest data no 88-ACR could ever
// have produced. That is inventing hardware, and it is the one thing DESIGN.md forbids
// outright. The refusal is the feature.
//
// Choosing among the formats a card CAN hear is a different question and is settled by
// confidence: the Sol's CUTS UART genuinely runs at 300 or 1200 baud and the GUEST picks
// which at OUT FAh D5, so both are its hardware and neither is a guess about a standard.
//
// ---- REPORT, DO NOT HIDE -----------------------------------------------------------
//
// A mount narrates what it found through the board's drainLog(), which monitor.cpp
// already flushes right after MOUNT:
//
//     TRK80.WAV: CUTS 1200 baud, 7932 bytes, 27 framing errors (99.7% clean)
//
// The difference between that line and silence is the difference between a feature and
// a support burden. A tape that decoded at 40% is noise, and saying so beats handing the
// guest three thousand bytes of garbage and letting them debug the loader.

#include "host/media.h"
#include "host/tapemodem.h"

#include <memory>
#include <string>
#include <vector>

namespace altair {

// ---------------------------------------------------------------------------
// A WAV wearing a tape's clothes.
//
// The decoded bytes ARE the medium: readAt/writeAt address them, and size() is their
// count, so everything above this -- TapeImage, TapeStream, the UART -- sees exactly
// what a .TAP would have given it.
//
// READ-ONLY FOR NOW. Recording back out to audio is Phase 4 (it needs
// MediaFile::resize(), because re-encoding a shorter recording over a longer WAV would
// otherwise leave stale audio past the end that the next mount decodes as trailing
// garbage). Until then a WAV mounts read-only REGARDLESS of what the operator typed --
// and says so via readOnlyForced(), which is the existing machinery for exactly this:
// a difference between what was asked for and what happened must never be silent.
// ---------------------------------------------------------------------------
class AudioTapeMedia : public MediaFile {
public:
    AudioTapeMedia(std::unique_ptr<MediaFile> under, std::vector<uint8_t> decoded,
                   TapeFormat fmt, uint32_t rate);

    uint64_t size() const override { return bytes_.size(); }
    bool     readOnly() const override { return true; }        // Phase 4 relaxes this
    bool     readOnlyForced() const override { return true; }  // ...and so this
    bool     readAt(uint64_t off, uint8_t* buf, size_t n) override;
    bool     writeAt(uint64_t off, const uint8_t* buf, size_t n) override;
    void     sync() override {}
    const std::string& describe() const override { return under_->describe(); }

    // What this tape turned out to be. Reported by the read-only `detected` property.
    const TapeFormat& format() const { return fmt_; }
    uint32_t          sampleRate() const { return rate_; }

private:
    std::unique_ptr<MediaFile> under_;
    std::vector<uint8_t>       bytes_;
    TapeFormat                 fmt_;
    uint32_t                   rate_ = 0;
};

// ---------------------------------------------------------------------------
// The entry point both boards call INSTEAD OF openMedia(), so neither duplicates a
// line of this and the one seam is preserved.
//
//   `candidates`  what THIS CARD's modem can physically demodulate. Never empty.
//   `want`        the unit's `format` property: "auto", or one format's name.
//   `detected`    out: what it turned out to be ("raw" for a byte tape).
//   `log`         out: the narration line(s), for the board to hand to drainLog().
//
// Null with `err` set on refusal -- never a silent empty medium, per media.h.
// ---------------------------------------------------------------------------
std::unique_ptr<MediaFile> openTapeMedia(const std::string& path, bool ro,
                                         const std::vector<TapeFormat>& candidates,
                                         const std::string& want, std::string& detected,
                                         std::vector<std::string>& log, std::string& err);

// The choices a `format` property offers for a card with this candidate list:
// "auto", "raw", then each candidate's name. Kept here so the two boards cannot drift.
std::vector<std::string> tapeFormatChoices(const std::vector<TapeFormat>& candidates);

// Below this, a decode is not a tape -- it is noise that happened to contain start
// bits. Mounting it would hand the guest garbage; refusing names the override.
inline constexpr double kTapeConfidenceFloor = 0.90;

} // namespace altair
