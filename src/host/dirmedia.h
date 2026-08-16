#pragma once
//
// DirectoryMedia -- a host DIRECTORY standing in for one CF/microSD card
// (reference/dual-sd-card.md; DESIGN.md 7.7 seam).
//
// The Dual SD board and its CF/SD cousins present a modern flash card to the S-100
// bus as one contiguous run of 512-byte sectors -- gigabytes of them. A HostFile
// slurps the whole medium into RAM at MOUNT (media.h): fine for an 8 MB floppy, a
// non-starter for a 1 GB card. And Patrick did not want one opaque multi-GB binary
// image either: a card holds several CP/M volumes, and each ought to be its own
// file you can back up, swap and mount on its own.
//
// So a card is a DIRECTORY. A small descriptor file in it -- `card.geometry` --
// declares the sector size and an ordered list of PARTITIONS, each mapping a run of
// the card's LBA space to its own backing file. The board still sees one flat byte
// space [0, size()); this medium routes each access to the covering backing file and
// streams it with seek+read/write -- LAZY, one block at a time, the way the hardware
// does. No whole-card image is ever held in RAM.
//
//   card.geometry:
//     # a line comment
//     sector_size 512
//     partition A cpm3.img  15616      # name  backing-file  sector-count
//     partition B data.img  15616
//
// GROWABLE, AND HONEST ABOUT AN ERASED CARD. A backing file may start empty or
// short; blocks at or past its current end (but within the partition's declared
// sector count) read the ERASED-CARD byte -- what a never-written CF/SD sector
// actually returns (0xFF), NOT the CP/M `0xE5` directory constant. This is a
// filesystem-agnostic block medium: it never synthesizes a CP/M structure. A blank
// card is genuinely UNFORMATTED, and the guest FORMATs it (the same model as the
// growable hard-sector disk, disk.h). A write grows its backing file up to the
// declared end; a write past the last partition's declared end -- i.e. past size() --
// is refused. It is a fixed-geometry card: resize() is false.
//
// This is a MediaFile sibling of HostFile/MemoryMedia and reaches the program the
// same way: openHostMedia() (below) is what both mains install as the resolver, and
// it hands back a DirectoryMedia when the path is a directory and a HostFile
// otherwise.

#include "host/media.h"

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace altair {

// The erased-flash byte a never-written CF/SD sector reads back
// (reference/dual-sd-card.md open item 2). Deliberately NOT 0xE5: that is a CP/M
// directory-fill constant, written only by CP/M's FORMAT or the board's FORMAT
// command -- never a property of the raw medium.
inline constexpr uint8_t kErasedByte = 0xFF;

// The well-known descriptor filename inside a card directory.
inline constexpr const char* kGeometryFile = "card.geometry";

class DirectoryMedia : public MediaFile {
public:
    ~DirectoryMedia() override;

    uint64_t size() const override { return totalBytes_; }
    bool     readOnly() const override { return readOnly_; }
    bool     readOnlyForced() const override { return forced_; }
    bool     readAt(uint64_t off, uint8_t* buf, size_t n) override;
    bool     writeAt(uint64_t off, const uint8_t* buf, size_t n) override;
    void     sync() override;
    // A card is a fixed geometry -- growth is per-partition on write, never a medium
    // resize. Same answer DiskImage gives (media.h).
    bool     resize(uint64_t) override { return false; }
    const std::string& describe() const override { return dir_; }

private:
    friend std::unique_ptr<MediaFile> openDirectoryMedia(const std::string&, bool,
                                                        std::string&);
    DirectoryMedia() = default;

    // One partition = one backing file mapped onto a run of the card's LBA space.
    struct Partition {
        std::string  name;      // the label from card.geometry (for diagnostics)
        std::string  path;      // full host path to the backing file
        uint64_t     lbaBase = 0;  // byte offset of this partition in the card space
        uint64_t     bytes   = 0;  // DECLARED size = sectors * sector_size
        uint64_t     onDisk  = 0;  // current backing-file size in bytes (grows on write)
        std::fstream io;           // held open, like HostFile::out_ (no open() per sector)
        bool         dirty   = false;  // written since the last sync()
    };

    // Read/write one contiguous chunk that lies wholly inside partition p, at byte
    // offset `within` from the partition's start.
    bool readChunk(Partition& p, uint64_t within, uint8_t* buf, size_t n);
    bool writeChunk(Partition& p, uint64_t within, const uint8_t* buf, size_t n);
    Partition* partitionAt(uint64_t off);

    std::string            dir_;         // the directory path -- what the operator typed
    std::vector<Partition> parts_;
    uint64_t               totalBytes_ = 0;   // Σ declared partition bytes
    uint32_t               sectorSize_ = 512;
    bool                   readOnly_   = false;
    bool                   forced_     = false;  // the host would not let us write it
};

// The resolver both mains install (src/main.cpp, tests/main.cpp): a DIRECTORY becomes
// a DirectoryMedia, anything else falls through to openHostFile(). This is the one
// place that decides a card is a folder of volume files rather than a single image.
std::unique_ptr<MediaFile> openHostMedia(const std::string& path, bool readOnly,
                                         std::string& err);

// Open a directory card directly (the backend openHostMedia dispatches to). Public so
// a test can build a card without going through the resolver.
std::unique_ptr<MediaFile> openDirectoryMedia(const std::string& dir, bool readOnly,
                                             std::string& err);

} // namespace altair
