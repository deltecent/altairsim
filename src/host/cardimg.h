#pragma once
//
// CardImage -- a host IMAGE FILE plus a sidecar geometry descriptor, standing in for one
// CF/microSD card (reference/dual-sd-card.md; DESIGN.md 7.7 seam).
//
// The Dual SD board and its CF/SD cousins present a modern flash card to the S-100 bus as
// one contiguous run of 512-byte sectors -- potentially gigabytes of them. A HostFile slurps
// the whole medium into RAM at MOUNT (media.h): fine for an 8 MB floppy, a non-starter for a
// 1 GB card. And the raw file alone cannot say how big the card really is: a card is a fixed
// geometry, and a boot image is usually TRUNCATED to just its live filesystem -- the rest of
// the card is erased space the guest may still address.
//
// So a card is a raw image file, `foo.img`, paired with a SIDECAR descriptor of the same base
// name, `foo.geo`, that declares the card's true geometry:
//
//   foo.geo:
//     # a line comment
//     sector_size 512
//     sectors     769920      # the card's real size; the .img may be shorter
//
// The board sees one flat byte space [0, size()) = sectors * sector_size. This medium streams
// it with seek + read/write -- LAZY, one block at a time, the way the hardware does; no whole
// card is ever held in RAM. Keeping the geometry beside the image (rather than a per-directory
// descriptor) lets every card live as two files in one shared folder.
//
// GROWABLE, AND HONEST ABOUT AN ERASED CARD. The backing file may start empty or SHORT: blocks
// at or past its current end (but within the declared card) read the ERASED-CARD byte -- what a
// never-written CF/SD sector actually returns (0xFF), NOT the CP/M `0xE5` directory constant.
// This is a filesystem-agnostic block medium: it never synthesizes a CP/M structure. A blank
// card is genuinely UNFORMATTED, and the guest FORMATs it (the same model as the growable
// hard-sector disk, disk.h). A write grows the backing file up to the declared end; a write
// past that end -- i.e. past size() -- is refused. It is a fixed-geometry card: resize() is
// false.
//
// This is a MediaFile sibling of HostFile/MemoryMedia, paralleling DiskImage. It reaches the
// program through openHostMedia() (below), the resolver both mains install: a file WITH a
// sibling `.geo` becomes a CardImage; a `.img` file WITHOUT one is an error (a card needs its
// geometry); every other file falls through to a plain HostFile (floppies, tapes, ...).

#include "host/media.h"

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace altair {

// The erased-flash byte a never-written CF/SD sector reads back
// (reference/dual-sd-card.md open item 2). Deliberately NOT 0xE5: that is a CP/M
// directory-fill constant, written only by CP/M's FORMAT or the board's FORMAT
// command -- never a property of the raw medium.
inline constexpr uint8_t kErasedByte = 0xFF;

// The sidecar geometry file's extension: `foo.img` is described by `foo.geo`.
inline constexpr const char* kGeoExt = ".geo";

class CardImage : public MediaFile {
public:
    ~CardImage() override;

    uint64_t size() const override { return declaredBytes_; }
    bool     readOnly() const override { return readOnly_; }
    bool     readOnlyForced() const override { return forced_; }
    bool     readAt(uint64_t off, uint8_t* buf, size_t n) override;
    bool     writeAt(uint64_t off, const uint8_t* buf, size_t n) override;
    void     sync() override;
    // A card is a fixed geometry -- growth is the backing file filling on write, never a
    // medium resize. Same answer DiskImage gives (media.h).
    bool     resize(uint64_t) override { return false; }
    const std::string& describe() const override { return path_; }

private:
    friend std::unique_ptr<MediaFile> openCardImage(const std::string&, bool, std::string&);
    CardImage() = default;

    std::string  path_;                // the backing image path -- what the operator typed
    std::fstream io_;                  // held open, like HostFile::out_ (no open() per sector)
    uint64_t     declaredBytes_ = 0;   // DECLARED card size = sectors * sectorSize
    uint64_t     onDisk_        = 0;   // current backing-file size in bytes (grows on write)
    uint32_t     sectorSize_    = 512;
    bool         readOnly_      = false;
    bool         forced_        = false;  // the host would not let us write it
    bool         dirty_         = false;  // written since the last sync()
};

// The resolver both mains install (src/main.cpp, tests/main.cpp). A file that has a sibling
// `.geo` descriptor becomes a CardImage; a `.img` file with no sidecar is an error (a card
// image is meaningless without its declared geometry); anything else falls through to
// openHostFile() -- a plain single-file image (floppies, tapes, ...). This is the one place
// that decides a mounted path is a lazy card rather than a whole-file image.
std::unique_ptr<MediaFile> openHostMedia(const std::string& path, bool readOnly,
                                         std::string& err);

// Open a card image directly: the backing image `imgPath`, with its `.geo` sidecar alongside.
// Public so a test can build a card without going through the resolver.
std::unique_ptr<MediaFile> openCardImage(const std::string& imgPath, bool readOnly,
                                         std::string& err);

// ---------------------------------------------------------------------------
// Authoring a blank card -- MOUNT ... CREATE.
//
// A CardSpec is what the operator asked for: a sector size and the card's declared sector
// count. createCardImage() turns that into a real card on disk -- an EMPTY (0-byte, growable)
// backing image and its `.geo` sidecar. The card is therefore genuinely UNFORMATTED: every
// block reads the erased byte until the guest FORMATs it, exactly the hard-sector blank-disk
// model (disk.h). CREATE never writes a CP/M structure.
// ---------------------------------------------------------------------------
struct CardSpec {
    uint32_t sectorSize = 512;
    uint64_t sectors    = 0;   // the declared card size, in sectors
};

// Build a CardSpec from MOUNT `key=value` options. Recognized keys:
//   format=<name>        a named template, applied FIRST (sector size + sectors)
//   sector_size=<n>      override the sector size
//   sectors=<n>          the card's declared sector count
// A non-zero sector count must result. A key that is not one of these, a malformed value, or
// an unknown template is an error.
bool parseCardSpec(const std::vector<std::pair<std::string, std::string>>& opts,
                   CardSpec& spec, std::string& err);

// True if any of these options names a card-authoring key (format/sector_size/sectors) --
// what the monitor uses to tell a card CREATE from a plain-file CREATE.
bool hasCardSpecKeys(const std::vector<std::pair<std::string, std::string>>& opts);

// Author a blank card at `imgPath`: create the EMPTY backing image and write its `.geo`
// sidecar from `spec`. Neither the image nor its sidecar may already exist -- CREATE never
// clobbers.
bool createCardImage(const std::string& imgPath, const CardSpec& spec, std::string& err);

} // namespace altair
