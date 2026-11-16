#pragma once
//
// A Read Track decoder for the floppy tests (#937). It is written from the IBM track layout and
// not from the drive's code, so a test that uses it checks the drive against the layout.

#include "chips/floppy.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace altair {

// It walks the track the way a controller's data separator does: a run of 00 sync bytes, in
// MFM three A1 (or C2 for the index mark), then an address mark. An ID field is FE and four
// bytes; a data field is FB or F8 and `dataLen` bytes; each is followed by its CRC. A field
// is skipped whole, so zeros in the data are never taken for sync.
struct TrackField {
    size_t               at;    // offset of the address mark
    uint8_t              mark;  // FC, FE, FB or F8
    std::vector<uint8_t> body;  // C H R N, or the data
    bool                 crcOk;
};

inline std::vector<TrackField> decodeTrack(const std::vector<uint8_t>& t, bool mfm, size_t dataLen) {
    std::vector<TrackField> out;
    size_t                  i = 0;
    while (i < t.size()) {
        if (t[i] != 0x00) { ++i; continue; }
        while (i < t.size() && t[i] == 0x00) ++i;
        if (mfm) {
            if (i + 3 > t.size()) break;
            const uint8_t s = t[i];
            if ((s != 0xA1 && s != 0xC2) || t[i + 1] != s || t[i + 2] != s) continue;
            i += 3;
        }
        if (i >= t.size()) break;
        const uint8_t m = t[i];
        size_t        len;
        if (m == 0xFC) len = 0;
        else if (m == 0xFE) len = 4;
        else if (m == 0xFB || m == 0xF8) len = dataLen;
        else continue;
        const size_t crcAt = i + 1 + len;
        if (len && crcAt + 2 > t.size()) break;
        TrackField f{i, m, std::vector<uint8_t>(t.begin() + (long)i + 1, t.begin() + (long)(i + 1 + len)),
                     true};
        if (len) {
            const uint16_t crc = fieldCrc(mfm, &t[i], 1 + len);
            f.crcOk = t[crcAt] == (uint8_t)(crc >> 8) && t[crcAt + 1] == (uint8_t)(crc & 0xFF);
            i = crcAt + 2;
        } else {
            ++i;
        }
        out.push_back(std::move(f));
    }
    return out;
}

} // namespace altair
