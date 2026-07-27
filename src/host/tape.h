#pragma once
//
// TapeImage -- the sequential counterpart of DiskImage (the 88-ACR's .TAP).
//
// A cassette has exactly one thing a disk does not: A POSITION. It is not that a
// tape is a worse disk -- it is that the head is where it is, and the only way to
// get to the start of the program is to REWIND. That is the entire difference, and
// it is why this is its own class and not a DiskImage with one track.
//
// So the CLI gets a verb (`REW acr0:tape`), SHOW gets a number, and the guest gets
// bytes in the order they were recorded.
//
// AND THEN THE ADAPTER: a tape IS a ByteStream. TapeStream, below, is the whole of
// what makes the 88-ACR nearly free -- the shared 1602 UART hands its bytes to a
// stream, and it never learns whether the stream is a socket, a terminal, or a
// cassette turning at 300 baud. That is DESIGN.md 7.1's promise being cashed: the
// board knows it has a serial line, and does not know what is on the end of it.

#include "host/media.h"
#include "host/stream.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace altair {

class TapeImage {
public:
    explicit TapeImage(std::unique_ptr<MediaFile> m) : media_(std::move(m)) {}

    // False AT THE END OF THE TAPE -- which is not an error, it is the end of the
    // tape. The guest's loader hits it and stops, exactly as it would have when the
    // recorder ran off the leader.
    bool read(uint8_t& b);

    // False if the write-protect tab is out. Otherwise it records -- OVER what is
    // there if the head is in the middle of the tape, EXTENDING it if the head is
    // at the end. Both are what a cassette recorder does.
    bool write(uint8_t b);

    void rewind() { pos_ = 0; }  // <- what the CLI verb drives

    // Put the head back where a snapshot found it (SNAPSHOT/RESTORE, DESIGN.md 13).
    // The tape's bytes are host-backed and travel with the file; only this position
    // -- and the stop mark below -- is runtime state, so it is what the ACR/Sol deck
    // restores by hand.
    void setPos(uint64_t p) { pos_ = p; }

    // A MOVABLE SECOND END OF TAPE -- the operator's STOP button at a counter mark, and
    // NOTHING the guest can reach. When the head reaches `stopAt_` the tape stops handing
    // bytes BACK -- a quiet line, exactly as the physical end is -- so a multi-program
    // cassette can be cued to stop at a boundary instead of running into the next program.
    // `kNoStop` means "play to the physical end", the default. It gates READS only: a
    // recording writes straight through it (playback-only, by construction). Exclusive:
    // everything strictly before the mark plays, then the line falls silent.
    static constexpr uint64_t kNoStop = ~uint64_t(0);
    void     setStop(uint64_t p) { stopAt_ = p; }
    uint64_t stopAt() const { return stopAt_; }
    bool     atStop() const { return pos_ >= stopAt_; }

    uint64_t pos() const { return pos_; }
    uint64_t size() const { return media_->size(); }
    bool     atEnd() const { return pos_ >= media_->size(); }
    bool     readOnly() const { return media_->readOnly(); }
    bool     readOnlyForced() const { return media_->readOnlyForced(); }
    void     sync() { media_->sync(); }
    const std::string& describe() const { return media_->describe(); }

    // THE TRANSPORT STOPPED. What sync() is to a byte tape, this is to one that has to
    // re-encode itself before it can be written back -- see MediaFile::commit(), which
    // is where the reasoning lives. A board calls it wherever it used to call sync()
    // for a reason the OPERATOR caused: UNMOUNT, REWIND, releasing RECORD. It is
    // strictly stronger than sync(), so calling it is never wrong, only sometimes
    // more than was needed.
    bool commit(std::string& err) { return media_->commit(err); }

private:
    std::unique_ptr<MediaFile> media_;
    uint64_t                   pos_    = 0;
    uint64_t                   stopAt_ = kNoStop;  // the auto-stop mark; kNoStop = the real end
};

// ---------------------------------------------------------------------------
// A tape, wearing a serial line's clothes.
//
// NON-OWNING, and on purpose: the BOARD owns the TapeImage, because REWIND and
// SHOW are the board's business and the UART must not be able to reach either. The
// chip gets this, and this can only move bytes.
//
// readable() is `there is more tape`, which is the ByteStream contract kept
// exactly: the stream HOLDS the byte until the card takes it. A tape that dropped
// a byte because the guest was slow would be manufacturing data loss the host does
// not have -- the very thing DESIGN.md 7.1 forbids. A real recorder does drop
// data, and we do not model that. It is in the board's .md, under Limitations.
//
// ---- AND IT IS IN EXACTLY ONE MODE AT A TIME, BECAUSE THE TAPE IS ------------
//
// PLAY or RECORD. Not both. This is not a simplification, it is the machine: the
// 88-ACR has NO MOTOR CONTROL -- there is no register for it, the guest cannot
// reach the transport, and the operator worked the controls with their finger. One
// head, one transport, one direction of signal flow: a deck that is recording is
// not handing you back what used to be on the tape.
//
// NOT because "one button is down at a time" -- on a real recorder you hold PLAY
// and RECORD together, and RECORD alone does nothing. That is one action selecting
// one mode, and the mode is what excludes.
//
// AND WITHOUT IT, RECORDING SILENTLY CORRUPTS THE TAPE. There is ONE head and so
// ONE position (see pos_ above -- read and write share it, as they must). The UART
// receives EAGERLY: it pulls a byte off its line the moment it has room, because
// that is how DAV and an interrupt-driven loader work. So a tape mounted for
// writing would have its first byte read away by the card before the guest ever
// ran, the position would sit at 1, and the guest's recording would begin at byte
// ONE. Off by one, on every tape, in the direction nobody checks.
//
// The mode makes that unrepresentable rather than merely unlikely: in PLAY the
// stream is not writable, in RECORD it is not readable, and so nothing can advance
// the head except the thing the operator asked for.
// ---------------------------------------------------------------------------
class TapeStream : public ByteStream {
public:
    // The buttons on the front of the recorder. The CARD cannot press them.
    enum class Mode { Play, Record };

    // THE TAPE'S OWN CLOCK, and it is a WALL clock, not the guest's. `nsPerByte` is 0
    // for FULL SPEED -- a byte is ready the instant the register frees, so the tape
    // empties as fast as the loader reads it, at any CPU clock. A positive value paces
    // playback in real host time at that many nanoseconds per byte, which is the card's
    // baud turned into a duration the CPU's speed cannot touch (see AcrBoard::reline and
    // SolBoard::retape). `hostNs` is where wall time comes from -- injectable so a test
    // can drive it, defaulting to a monotonic host clock. It is consulted ONLY when
    // nsPerByte > 0, so full speed stays a pure function of the tape and the tests that
    // run flat out never touch the wall at all.
    explicit TapeStream(TapeImage& t, Mode m = Mode::Play, uint64_t nsPerByte = 0,
                        std::function<uint64_t()> hostNs = {});

    std::string describe() const override { return tape_.describe(); }
    size_t read(uint8_t* buf, size_t n) override;
    size_t write(const uint8_t* buf, size_t n) override;

    // Nothing plays back out of a recorder that is recording, and nothing is cut
    // into a tape that is merely playing. The write-protect tab is a SECOND, and
    // independent, reason a tape may refuse to record.
    //
    // AND, when the tape carries a baud (nsPerByte > 0), the byte is not readable until
    // wall time reaches its turn -- that is how the cassette paces itself without the
    // UART's emulated clock, and why pacesItself() is true.
    bool readable() const override;
    bool writable() const override { return mode_ == Mode::Record && !tape_.readOnly(); }
    bool pacesItself() const override { return true; }
    void flush() override { tape_.sync(); }

private:
    TapeImage& tape_;
    Mode       mode_;

    // 0 = full speed. Otherwise the wall-clock byte interval, and the machinery to hold
    // each byte back until its moment: hostNs_() is real time, nextReadyNs_ is when the
    // next byte may leave, started_ lets the FIRST byte go at once (a real transport is
    // already up to speed by the time the loader looks -- the leader saw to that).
    uint64_t                  nsPerByte_ = 0;
    std::function<uint64_t()> hostNs_;
    uint64_t                  nextReadyNs_ = 0;
    bool                      started_     = false;
};

// ---------------------------------------------------------------------------
// The tape counter, as text. Shared so the 88-ACR and the Sol format a position
// identically -- the boards supply the seconds (real audio time for a WAV, an estimate
// from baud for a .TAP), and the percentage tracks that time so the clock and the
// percent always tell the same story.
// ---------------------------------------------------------------------------

// "01:23", or "1:02:03" once past an hour. Negative or NaN reads as "00:00".
std::string tapeTimeMMSS(double secs);

// "01:23 / 04:56 (28%)". A zero total (a blank or unknowable tape) drops to "00:00 (0%)".
std::string tapeCounterText(double curSecs, double totalSecs);

// Parse a WIND target time: "MM:SS", "H:MM:SS", or a bare number of seconds ("83", "1.5").
// False on anything malformed. START/END are the caller's business, not a time.
bool parseTapeTime(const std::string& s, double& secs);

} // namespace altair
