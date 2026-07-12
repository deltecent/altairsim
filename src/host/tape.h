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

    uint64_t pos() const { return pos_; }
    uint64_t size() const { return media_->size(); }
    bool     atEnd() const { return pos_ >= media_->size(); }
    bool     readOnly() const { return media_->readOnly(); }
    void     sync() { media_->sync(); }
    const std::string& describe() const { return media_->describe(); }

private:
    std::unique_ptr<MediaFile> media_;
    uint64_t                   pos_ = 0;
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
// ---------------------------------------------------------------------------
class TapeStream : public ByteStream {
public:
    explicit TapeStream(TapeImage& t) : tape_(t) {}

    std::string describe() const override { return tape_.describe(); }
    size_t read(uint8_t* buf, size_t n) override;
    size_t write(const uint8_t* buf, size_t n) override;
    bool   readable() const override { return !tape_.atEnd(); }
    bool   writable() const override { return !tape_.readOnly(); }
    void   flush() override { tape_.sync(); }

private:
    TapeImage& tape_;
};

} // namespace altair
