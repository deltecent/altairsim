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
//     TRK80.WAV: cuts1200, 7939 bytes, 0 framing errors (100.0% of frames intact)
//
// The difference between that line and silence is the difference between a feature and
// a support burden. A tape that decoded at 40% is noise, and saying so beats handing the
// guest three thousand bytes of garbage and letting them debug the loader.
//
// But read the percentage for exactly what it is: a FRAMING rate, not a verdict on the
// data. It counts frames whose start and stop bits landed where they belonged. A tape
// can frame almost perfectly and still be mostly wrong between the stop bits -- see the
// note above the log line in tapecodec.cpp for the archived recording that does.

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
// ---- AND IT RECORDS, WHICH IS WHY sync() DOES NOTHING ------------------------------
//
// A guest recording writes DECODED bytes here, and the file bytes are a function of the
// whole decoded stream -- so writing back means re-modulating the tape end to end and
// rewriting the WAV. That is far too expensive to do per byte, and per byte is exactly
// how often sync() is called: Uart1602::writeData flushes after every character, and
// for a tape that flush is a sync. Re-encoding there would be quadratic in both CPU and
// host I/O.
//
// So sync() is a dirty MARK and commit() is the work, at the points where the operator
// stopped the transport -- UNMOUNT, REWIND, releasing RECORD. See MediaFile::commit().
//
// THE RECORDING IS RE-ENCODED IN THE TAPE'S OWN FORMAT, at its own sample rate: what
// was mounted as CUTS at 22050 Hz is written back as CUTS at 22050 Hz. Recording over
// a cassette does not change what kind of cassette it is.
//
// WHAT CANNOT BE PRESERVED, and must be synthesized instead, is TIME. The decoded byte
// stream holds no durations -- no leader, no trailer, and no inter-file gaps -- because
// a byte image cannot hold any (verified on the corpus: nothing on those tapes encodes
// data as duration). setEncoding() supplies the two the transport needs, from the
// board's properties. There is no third: this layer sees one byte stream and cannot
// know where one SOLOS file ends and the next begins, so a multi-file tape re-recorded
// here comes back as one continuous run. Documented under Limitations on both boards.
// ---------------------------------------------------------------------------
class AudioTapeMedia : public MediaFile {
public:
    AudioTapeMedia(std::unique_ptr<MediaFile> under, std::vector<uint8_t> decoded,
                   TapeFormat fmt, uint32_t rate);

    // THE LAST LINE OF DEFENCE, NOT THE PLAN -- the same bargain ~HostFile() makes, and
    // for the same reason. The plan is that a board commits when the transport stops.
    // The case this catches is the operator who records and then QUITs without ever
    // pressing stop: QUIT tears the machine down, and without this the whole recording
    // would go with it. A byte tape never had this problem, because its sync() is real.
    //
    // It cannot report a failure, which is exactly why it is not the plan.
    ~AudioTapeMedia() override;

    uint64_t size() const override { return bytes_.size(); }

    // WHAT THE FILE UNDERNEATH SAYS, and nothing of our own. A WAV is as recordable as
    // any other tape now, so the only reasons to refuse are the ordinary ones -- the
    // operator typed RO, or the host will not let us write the file -- and those are
    // the medium's answer to give, not ours.
    bool readOnly() const override { return under_->readOnly(); }
    bool readOnlyForced() const override { return under_->readOnlyForced(); }

    bool readAt(uint64_t off, uint8_t* buf, size_t n) override;
    bool writeAt(uint64_t off, const uint8_t* buf, size_t n) override;
    bool resize(uint64_t n) override;
    void sync() override {}  // deliberately nothing -- see above, and commit()
    bool commit(std::string& err) override;
    const std::string& describe() const override { return under_->describe(); }

    // How much idle tone to lay down either side of the data when this is written
    // back, in seconds. Pushed down by the board from its `leader`/`trailer`
    // properties -- at MOUNT and again whenever one is SET, so that setting it and
    // then recording does what it plainly says.
    void setEncoding(double leaderSeconds, double trailerSeconds);

    // What this tape turned out to be. Reported by the read-only `detected` property.
    const TapeFormat& format() const { return fmt_; }
    uint32_t          sampleRate() const { return rate_; }

private:
    std::unique_ptr<MediaFile> under_;
    std::vector<uint8_t>       bytes_;
    TapeFormat                 fmt_;
    uint32_t                   rate_ = 0;

    bool   dirty_   = false;
    double leader_  = 5.0;
    double trailer_ = 0.0;
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
