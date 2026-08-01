#pragma once
//
// IMD -> raw, at the MOUNT layer (DESIGN.md 7.3, and the reversed note in host/disk.h).
//
// An ImageDisk (`.IMD`) is a CONTAINER: it carries a per-track sector map, per-sector
// data types, and compression. `DiskImage` deliberately reads none of that -- it is
// raw sector-linear and stays that way (host/disk.h). So an `.IMD` a user mounts is
// converted to a raw sibling `.DSK` FIRST, by this, and only the `.DSK` is ever
// mounted. The convert happens above `DiskImage`, not inside it, which is what keeps
// that class one-class-no-container-map.
//
// THIS FILE IS PURE. Bytes in, bytes out -- no filesystem, no board, no Clock -- so it
// unit-tests with synthetic buffers and no machine. The MOUNT handler
// (src/cli/monitor.cpp) does the I/O around it: slurp the `.IMD`, call this, write the
// `.DSK`, mount it.
//
// ---- WHAT `interleaved` DECIDES, AND WHY A CALLBACK ----------------------------------
//
// A raw `.DSK` has no geometry of its own; for a DOUBLE-SIDED disk the byte order of
// the two heads is fixed by whichever controller reads it back (DiskImage::slotIndex):
//
//   cylinder-major / head-minor (interleaved)  -- T0H0, T0H1, T1H0, ...  (Cromemco CDOS)
//   head-major (not interleaved)               -- all H0, then all H1    (VersaFloppy)
//
// The two controllers genuinely disagree, so there is no universal order. The target
// controller is named in the MOUNT command, so the converter ASKS it: `wantInterleaved`
// is consulted -- ONCE, and ONLY when the disk has more than one head -- with the raw
// byte count the board will re-probe. A single-sided disk never calls it (slotIndex is
// identical either way). Keeping it a callback is what lets this file stay board-free.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace altair {

// What the conversion produced -- for the MOUNT handler to narrate, so the operator
// sees how the raw file was built and can sanity-check it against the disk they expect.
struct ImdInfo {
    std::string              description;  // the ASCII header text up to 0x1A, trimmed
    std::vector<std::string> tracks;       // coalesced human lines, one per uniform range:
                                           //   "cyl 0-76 head 0-1  MFM  16 x 512  (from sec 1)"
    int                      heads    = 1;  // 1 or 2
    bool                     interleaved = true;  // the head order actually emitted (see header)
    uint64_t                 rawBytes = 0;  // == the emitted image size (order-independent)
};

// Convert an ImageDisk (`.IMD`) byte buffer into a raw sector-linear image.
//
//   - de-interleaves each track into ascending sector-ID order (DiskImage::locate
//     assumes logical-sequential layout from startSector);
//   - expands COMPRESSED sectors (type 0x02/04/06/08) to `size` fill bytes;
//   - fills UNAVAILABLE sectors (type 0x00) with 0xE5 (the CP/M blank byte);
//   - drops deleted/error/DAM flags -- raw carries payload only (fine for the clean
//     period images this serves; a corrupt-data flag is not reconstructable in raw);
//   - orders the two heads of a double-sided disk per `wantInterleaved` (see header).
//
// Fills `info`. Returns false + `err` on a malformed file (no 0x1A header terminator,
// a truncated record, an unknown record/data type, a non-rectangular track grid, or a
// per-sector size table `sizeCode == 0xFF`, which no target format needs yet).
bool convertImdToRaw(const std::vector<uint8_t>& imd, std::vector<uint8_t>& raw, ImdInfo& info,
                     std::string& err, const std::function<bool(uint64_t rawBytes)>& wantInterleaved);

} // namespace altair
