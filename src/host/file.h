#pragma once
//
// `file:PATH` -- a host file as a write-only ByteStream (DESIGN.md 7.1).
//
// The honest model of a printer's paper, or of a capture of anything a card
// sends: bytes go in, they land on disk, nothing ever comes back. So this is a
// SINK -- readable() is always false, read() returns 0 (a quiet line, never an
// EOF), and writable() is always true. A card CONNECTed to a file behaves exactly
// as it would wired to a printhead: it transmits and never waits.
//
// 8-BIT CLEAN, like every line (host/filter.h): the file is opened BINARY, so a CR
// the guest sent stays a CR and nothing on the way to disk rewrites a byte. The
// bytes in the file are the bytes on the wire.
//
// PROVENANCE, like a mount: describe() returns the exact `file:PATH` string the
// operator gave, so SHOW prints it and CONFIG SAVE round-trips it. The board that
// resolved a relative PATH against its config dir REMEMBERS the path as written
// (mits-hardsector.cpp) -- this stream only ever sees, and only ever echoes, the
// spec it was handed.

#include "host/stream.h"

#include <fstream>
#include <string>

namespace altair {

class FileStream : public ByteStream {
public:
    // The file is already open (the resolver opened it, so it could refuse cleanly
    // with a reason). `spec` is the operator's original `file:PATH` string.
    FileStream(std::ofstream out, std::string spec)
        : out_(std::move(out)), spec_(std::move(spec)) {}

    std::string describe() const override { return spec_; }

    // A quiet line, forever: nothing to read off a printout.
    size_t read(uint8_t*, size_t) override { return 0; }
    size_t write(const uint8_t* buf, size_t n) override;

    bool readable() const override { return false; }
    bool writable() const override { return true; }

    void flush() override { out_.flush(); }

private:
    std::ofstream out_;
    std::string   spec_;
};

} // namespace altair
