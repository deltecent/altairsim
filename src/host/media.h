#pragma once
//
// MediaFile -- the host file under any medium (DESIGN.md 7.3, 7.7).
//
// A disk and a tape have NOTHING in common as media: one is a geometry you can
// address at random, the other is a position on a strip that only moves forward.
// But as a HOST FILE they are the same thing, exactly: a byte range, a
// write-protect tab, and a sync. That is what this is -- and it is the only place
// in the program that opens one.
//
// So the layering is:
//
//     DiskImage / TapeImage      what the bytes MEAN   (the board's business)
//     MediaFile                  where the bytes ARE   (the host's business)
//
// A board holds a DiskImage or a TapeImage; it asks openMedia() for the file and
// never sees a file handle, a path grammar, or an ifstream. That is the same rule
// ByteStream enforces for serial lines, for the same reasons: cross-platform,
// scriptable, and testable with no filesystem in sight (MemoryMedia, below, is the
// ScriptedStream of the disk world).
//
// BUFFERED, WITH DIRTY WRITE-BACK. The whole image is read at MOUNT and written
// back at sync(). An 8 MB FDC+ image is 8 MB of RAM, and the machine it is
// emulating had 64K -- the asymmetry is not close enough to matter, and the
// alternative is a seek per sector.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class MediaFile {
public:
    virtual ~MediaFile() = default;

    virtual uint64_t size() const = 0;
    virtual bool     readOnly() const = 0;

    // DID WE PUT THE TAB IN FOR YOU? A file the host will not let us write is a disk
    // with the write-protect tab out, and that is a perfectly ordinary disk -- so it
    // MOUNTS, read-only, rather than being refused. (Patrick, 2026-07-12.)
    //
    // But it mounts read-only when the operator did not ASK for read-only, and a
    // difference between what was typed and what happened must never be silent: CP/M
    // will spend an afternoon writing to it and every write will bounce. So the board
    // that mounted it SAYS SO -- via drainLog(), which is exactly what drainLog() is
    // for and why it is on Board and no longer on the memory card.
    virtual bool readOnlyForced() const { return false; }

    // Both are all-or-nothing: a partial read is a failure, not a short count.
    // A sector is a unit; half of one is not a smaller sector, it is a bad disk.
    virtual bool readAt(uint64_t off, uint8_t* buf, size_t n) = 0;

    // MAY EXTEND THE MEDIUM. Recording past the end of a tape is a real thing a
    // real cassette does. A DISK never does it -- not because this forbids it, but
    // because DiskImage bounds every access against the declared geometry first,
    // so a write can never reach the XMODEM pad at the end of a .DSK, let alone
    // past it. (See disk.h.)
    virtual bool writeAt(uint64_t off, const uint8_t* buf, size_t n) = 0;

    // Push the dirty bytes at the host. Called on UNMOUNT and on shutdown; a board
    // may call it whenever it likes.
    virtual void sync() = 0;

    // What the operator typed: the path. Reported by SHOW.
    virtual const std::string& describe() const = 0;
};

// ---------------------------------------------------------------------------
// THE ONE SEAM (DESIGN.md 7.7).
//
// The same shape as resolveEndpoint()/Sio2Board::setResolver(): the program's
// entry point installs the resolver, and everything below it -- boards included --
// knows only that it can ask for a path and get a medium back. Installed in BOTH
// src/main.cpp and tests/main.cpp, deliberately with the SAME resolver, so the
// tests exercise the configuration a user will actually run.
//
// A test that wants a disk without a filesystem installs its own for the duration
// and hands back a MemoryMedia. That is the point of the seam, and the only
// legitimate reason to move it.
// ---------------------------------------------------------------------------
using MediaResolver =
    std::function<std::unique_ptr<MediaFile>(const std::string& path, bool readOnly,
                                             std::string& err)>;

void setMediaResolver(MediaResolver r);

// Null and `err` set on anything that will not open. Never a silent empty medium:
// an operator who mistypes a path has a disk that is *missing*, not blank, and a
// blank disk that boots to nothing is ten minutes of confusion.
std::unique_ptr<MediaFile> openMedia(const std::string& path, bool readOnly, std::string& err);

// The real one -- the host filesystem. This is what both mains install.
std::unique_ptr<MediaFile> openHostFile(const std::string& path, bool readOnly, std::string& err);

// ---------------------------------------------------------------------------
// The host file. Slurped at open, written back at sync.
//
// THE FILE MUST EXIST. `MOUNT dcdd0:drive0 cmp22.dsk` with the name spelt wrong is
// a typo, and creating an empty file for it would turn a typo into a disk that
// formats clean and boots to nothing. Making blank media is a separate verb, and
// it is not this one.
// ---------------------------------------------------------------------------
class HostFile : public MediaFile {
public:
    ~HostFile() override;

    uint64_t size() const override { return bytes_.size(); }
    bool     readOnly() const override { return readOnly_; }
    bool     readOnlyForced() const override { return forced_; }
    bool     readAt(uint64_t off, uint8_t* buf, size_t n) override;
    bool     writeAt(uint64_t off, const uint8_t* buf, size_t n) override;
    void     sync() override;
    const std::string& describe() const override { return path_; }

    bool dirty() const { return dirty_; }

private:
    friend std::unique_ptr<MediaFile> openHostFile(const std::string&, bool, std::string&);
    HostFile() = default;

    std::string          path_;
    std::vector<uint8_t> bytes_;
    bool                 readOnly_ = false;
    bool                 forced_   = false;  // the host would not let us write it
    bool                 dirty_    = false;
};

// ---------------------------------------------------------------------------
// A medium made of nothing. The ScriptedStream of the disk world.
//
// NO BOARD TEST SHOULD TOUCH THE FILESYSTEM: a test that writes a temp file to
// check a sector write is testing the operating system, is slow, and fails on a
// read-only checkout. This holds the image in a vector, and the test asserts on
// the vector afterwards.
// ---------------------------------------------------------------------------
class MemoryMedia : public MediaFile {
public:
    MemoryMedia(std::string name, std::vector<uint8_t> bytes, bool readOnly = false)
        : name_(std::move(name)), bytes_(std::move(bytes)), readOnly_(readOnly) {}

    uint64_t size() const override { return bytes_.size(); }
    bool     readOnly() const override { return readOnly_; }
    bool     readAt(uint64_t off, uint8_t* buf, size_t n) override;
    bool     writeAt(uint64_t off, const uint8_t* buf, size_t n) override;
    void     sync() override { ++syncs_; }
    const std::string& describe() const override { return name_; }

    // What the guest actually wrote, and whether it was ever flushed.
    const std::vector<uint8_t>& bytes() const { return bytes_; }
    int syncs() const { return syncs_; }

private:
    std::string          name_;
    std::vector<uint8_t> bytes_;
    bool                 readOnly_ = false;
    int                  syncs_    = 0;
};

} // namespace altair
