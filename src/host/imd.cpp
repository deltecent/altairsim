#include "host/imd.h"

#include <algorithm>

namespace altair {

namespace {

constexpr uint8_t kEof  = 0x1A;  // ends the ASCII header
constexpr uint8_t kFill = 0xE5;  // CP/M blank -- what an UNAVAILABLE sector becomes

// One parsed track, in the order the sectors appear on the medium PLUS the numbering
// map that says which logical sector each physical one is. The payload is stored
// physical-order; emit() sorts it into ID order (the de-interleave).
struct Track {
    int                  cyl = 0, head = 0;
    uint8_t              mode = 0;        // 0-2 FM, 3-5 MFM (for the human line)
    int                  sectors = 0;
    int                  sectorSize = 0;
    int                  baseId = 0;      // the smallest sector ID -> startSector
    std::vector<uint8_t> ids;             // physical -> logical sector ID (the numbering map)
    std::vector<std::vector<uint8_t>> payload;  // one per sector, in PHYSICAL order
};

// A forward cursor over the buffer that refuses to read past the end -- so a truncated
// IMD is a clean `false`, never an out-of-bounds read.
struct Reader {
    const std::vector<uint8_t>& b;
    size_t                      i = 0;
    bool ok(size_t n) const { return i + n <= b.size(); }
    uint8_t u8() { return b[i++]; }  // caller checked ok(1) first
    bool copy(size_t n, std::vector<uint8_t>& out) {
        if (!ok(n)) return false;
        out.assign(b.begin() + (ptrdiff_t)i, b.begin() + (ptrdiff_t)(i + n));
        i += n;
        return true;
    }
};

const char* modeName(uint8_t mode) { return mode <= 2 ? "FM" : "MFM"; }

// The per-head signature of a track, so coalesce() can tell two cylinders apart.
struct Sig {
    uint8_t mode = 0;
    int     sectors = 0, sectorSize = 0, baseId = 0;
    bool operator==(const Sig& o) const {
        return mode == o.mode && sectors == o.sectors && sectorSize == o.sectorSize &&
               baseId == o.baseId;
    }
};

std::string trim(const std::string& s) {
    size_t a = 0, z = s.size();
    while (a < z && (unsigned char)s[a] <= ' ') ++a;
    while (z > a && (unsigned char)s[z - 1] <= ' ') --z;
    return s.substr(a, z - a);
}

} // namespace

bool convertImdToRaw(const std::vector<uint8_t>& imd, std::vector<uint8_t>& raw, ImdInfo& info,
                     std::string& err, const std::function<bool(uint64_t rawBytes)>& wantInterleaved) {
    raw.clear();
    info = ImdInfo{};

    Reader r{imd};

    // ---- the ASCII header, up to the 0x1A EOF marker --------------------------------
    size_t eof = imd.size();
    for (size_t k = 0; k < imd.size(); ++k)
        if (imd[k] == kEof) { eof = k; break; }
    if (eof == imd.size()) {
        err = "not an ImageDisk file: no 0x1A header terminator";
        return false;
    }
    info.description = trim(std::string(imd.begin(), imd.begin() + (ptrdiff_t)eof));
    r.i = eof + 1;  // past the 0x1A

    // ---- the track records ----------------------------------------------------------
    std::vector<Track> tracks;
    while (r.i < imd.size()) {
        if (!r.ok(5)) { err = "truncated track header"; return false; }
        Track t;
        t.mode      = r.u8();
        t.cyl       = r.u8();
        uint8_t hb  = r.u8();
        t.sectors   = r.u8();
        uint8_t szc = r.u8();

        t.head = hb & 0x3F;  // low bits are the physical head; 6 = cyl map, 7 = head map
        if (t.mode > 5)     { err = "unknown track mode " + std::to_string(t.mode); return false; }
        if (szc == 0xFF)    { err = "per-sector size tables (sizeCode 0xFF) are not supported"; return false; }
        if (szc > 6)        { err = "unknown sector size code " + std::to_string(szc); return false; }
        t.sectorSize = 128 << szc;

        if (t.sectors > 0) {
            if (!r.copy((size_t)t.sectors, t.ids)) { err = "truncated sector numbering map"; return false; }
            // Optional cylinder / head maps -- read PAST them; they do not affect the raw
            // payload ordering (which comes from the numbering map alone).
            std::vector<uint8_t> skip;
            if (hb & 0x40) if (!r.copy((size_t)t.sectors, skip)) { err = "truncated cylinder map"; return false; }
            if (hb & 0x80) if (!r.copy((size_t)t.sectors, skip)) { err = "truncated head map"; return false; }
        }

        // The sector data records: one per sector, in the numbering-map's physical order.
        t.payload.resize((size_t)t.sectors);
        for (int s = 0; s < t.sectors; ++s) {
            if (!r.ok(1)) { err = "truncated sector data record"; return false; }
            uint8_t type = r.u8();
            std::vector<uint8_t>& out = t.payload[(size_t)s];
            switch (type) {
                case 0x00:  // unavailable -- no bytes stored; fill blank
                    out.assign((size_t)t.sectorSize, kFill);
                    break;
                case 0x01: case 0x03: case 0x05: case 0x07:  // normal (+deleted/+error): size bytes
                    if (!r.copy((size_t)t.sectorSize, out)) { err = "truncated sector data"; return false; }
                    break;
                case 0x02: case 0x04: case 0x06: case 0x08: {  // compressed: one fill byte
                    if (!r.ok(1)) { err = "truncated compressed sector"; return false; }
                    out.assign((size_t)t.sectorSize, r.u8());
                    break;
                }
                default:
                    err = "unknown sector data type " + std::to_string(type);
                    return false;
            }
        }

        t.baseId = t.sectors > 0 ? *std::min_element(t.ids.begin(), t.ids.end()) : 0;
        tracks.push_back(std::move(t));
    }

    if (tracks.empty()) { err = "no tracks in ImageDisk file"; return false; }

    // ---- the track grid: cyl 0..maxCyl x head {0} or {0,1} --------------------------
    int maxCyl = 0;
    bool head1 = false;
    for (const auto& t : tracks) {
        if (t.head != 0 && t.head != 1) {
            err = "unsupported head number " + std::to_string(t.head);
            return false;
        }
        maxCyl = std::max(maxCyl, t.cyl);
        head1  = head1 || t.head == 1;
    }
    const int nTracks = maxCyl + 1;
    const int nHeads  = head1 ? 2 : 1;
    info.heads = nHeads;

    // Index the tracks by (cyl,head) and require the grid be COMPLETE -- a missing track
    // would shift every later track's offset and the board (which re-probes by size)
    // would read the disk scrambled.
    std::vector<const Track*> grid((size_t)nTracks * (size_t)nHeads, nullptr);
    for (const auto& t : tracks) {
        const Track*& slot = grid[(size_t)t.cyl * (size_t)nHeads + (size_t)t.head];
        if (slot) { err = "duplicate track cyl " + std::to_string(t.cyl) + " head " +
                          std::to_string(t.head); return false; }
        slot = &t;
    }
    for (int c = 0; c < nTracks; ++c)
        for (int h = 0; h < nHeads; ++h)
            if (!grid[(size_t)c * (size_t)nHeads + (size_t)h]) {
                err = "missing track cyl " + std::to_string(c) + " head " + std::to_string(h);
                return false;
            }

    // A track's raw bytes, de-interleaved: sectors concatenated in ascending ID order.
    auto emitTrack = [](const Track& t, std::vector<uint8_t>& out) {
        std::vector<int> order((size_t)t.sectors);
        for (int s = 0; s < t.sectors; ++s) order[(size_t)s] = s;
        std::sort(order.begin(), order.end(),
                  [&](int a, int b) { return t.ids[(size_t)a] < t.ids[(size_t)b]; });
        for (int s : order)
            out.insert(out.end(), t.payload[(size_t)s].begin(), t.payload[(size_t)s].end());
    };

    // ---- the raw byte count (order-independent) -> ask the controller ---------------
    uint64_t rawBytes = 0;
    for (const auto& t : tracks) rawBytes += (uint64_t)t.sectors * (uint64_t)t.sectorSize;
    info.rawBytes = rawBytes;

    // Head order matters only for a double-sided disk (slotIndex is identical at 1 head),
    // so a single-sided disk never troubles the controller.
    const bool interleaved = nHeads > 1 ? wantInterleaved(rawBytes) : true;
    info.interleaved = interleaved;

    raw.reserve((size_t)rawBytes);
    if (interleaved) {  // cylinder-major / head-minor: T0H0, T0H1, T1H0, ...
        for (int c = 0; c < nTracks; ++c)
            for (int h = 0; h < nHeads; ++h)
                emitTrack(*grid[(size_t)c * (size_t)nHeads + (size_t)h], raw);
    } else {            // head-major: all of head 0, then all of head 1
        for (int h = 0; h < nHeads; ++h)
            for (int c = 0; c < nTracks; ++c)
                emitTrack(*grid[(size_t)c * (size_t)nHeads + (size_t)h], raw);
    }

    // ---- the human summary: coalesce consecutive identical cylinders ----------------
    auto sigAt = [&](int c, int h) {
        const Track* t = grid[(size_t)c * (size_t)nHeads + (size_t)h];
        return Sig{t->mode, t->sectors, t->sectorSize, t->baseId};
    };
    auto sameCyl = [&](int a, int b) {
        for (int h = 0; h < nHeads; ++h)
            if (!(sigAt(a, h) == sigAt(b, h))) return false;
        return true;
    };
    auto lineFor = [&](int cLo, int cHi, int hLo, int hHi) {
        Sig s = sigAt(cLo, hLo);
        std::string line = "cyl " + std::to_string(cLo);
        if (cHi != cLo) line += "-" + std::to_string(cHi);
        if (nHeads > 1) {
            line += " head " + std::to_string(hLo);
            if (hHi != hLo) line += "-" + std::to_string(hHi);
        }
        line += std::string("  ") + modeName(s.mode) + "  " + std::to_string(s.sectors) +
                " x " + std::to_string(s.sectorSize) + "  (from sec " + std::to_string(s.baseId) + ")";
        return line;
    };
    for (int c = 0; c < nTracks;) {
        int e = c;
        while (e + 1 < nTracks && sameCyl(c, e + 1)) ++e;
        // Within this cylinder range, both heads share a signature: one line for "head 0-1"
        // if the two heads match, else a line per head (the mixed-density Cromemco case).
        if (nHeads > 1 && !(sigAt(c, 0) == sigAt(c, 1))) {
            info.tracks.push_back(lineFor(c, e, 0, 0));
            info.tracks.push_back(lineFor(c, e, 1, 1));
        } else {
            info.tracks.push_back(lineFor(c, e, 0, nHeads - 1));
        }
        c = e + 1;
    }

    return true;
}

} // namespace altair
