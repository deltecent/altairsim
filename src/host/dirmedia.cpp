#include "host/dirmedia.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace altair {

// ---------------------------------------------------------------------------
// The geometry descriptor -- a tiny, host-standalone parser (NO config/toml
// dependency, so the medium is testable without the config layer).
//
// Line-oriented; `#` starts a comment; blank lines ignored. Two directives:
//   sector_size <n>
//   partition <name> <backing-file> <sector-count>
// Partitions are laid out in the order they appear, back to back.
// ---------------------------------------------------------------------------
namespace {

struct GeoPart {
    std::string name;
    std::string file;
    uint64_t    sectors = 0;
};

struct Geometry {
    uint32_t             sectorSize = 512;
    std::vector<GeoPart> parts;
};

bool parseGeometry(const std::string& text, Geometry& g, std::string& err) {
    std::istringstream in(text);
    std::string        line;
    int                lineNo   = 0;
    bool               sawSize  = false;
    while (std::getline(in, line)) {
        ++lineNo;
        // Strip a trailing comment and surrounding whitespace by tokenizing.
        std::istringstream ls(line);
        std::string        tok;
        if (!(ls >> tok)) continue;      // blank line
        if (tok[0] == '#') continue;     // whole-line comment

        auto fail = [&](const std::string& why) {
            err = kGeometryFile + std::string(":") + std::to_string(lineNo) + ": " + why;
            return false;
        };

        if (tok == "sector_size") {
            uint64_t n = 0;
            if (!(ls >> n) || n == 0) return fail("sector_size needs a positive value");
            g.sectorSize = (uint32_t)n;
            sawSize      = true;
        } else if (tok == "partition") {
            GeoPart p;
            if (!(ls >> p.name >> p.file >> p.sectors))
                return fail("partition needs: <name> <backing-file> <sector-count>");
            if (p.sectors == 0) return fail("partition '" + p.name + "' has zero sectors");
            if (p.file.find('/') != std::string::npos ||
                p.file.find('\\') != std::string::npos)
                return fail("backing file '" + p.file + "' must be a bare name in the card dir");
            g.parts.push_back(std::move(p));
        } else {
            return fail("unknown directive '" + tok + "'");
        }
    }
    (void)sawSize;  // sector_size is optional; the default (512) is the card's own.
    if (g.parts.empty()) {
        err = std::string(kGeometryFile) + ": no partitions declared";
        return false;
    }
    return true;
}

// Is the file writable by the host? Mirrors openHostFile's owner_write probe
// (media.cpp) -- a file the host will not let us write makes the whole card
// read-only, and the board says so via readOnlyForced().
bool hostWritable(const fs::path& p) {
    std::error_code ec;
    auto perms = fs::status(p, ec).permissions();
    if (ec) return false;
    return (perms & fs::perms::owner_write) != fs::perms::none;
}

} // namespace

// ---------------------------------------------------------------------------
// Open
// ---------------------------------------------------------------------------
std::unique_ptr<MediaFile> openDirectoryMedia(const std::string& dir, bool readOnly,
                                             std::string& err) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        err = "'" + dir + "': no such file";
        return nullptr;
    }
    if (!fs::is_directory(dir, ec)) {
        err = "'" + dir + "' is not a directory";
        return nullptr;
    }

    fs::path geoPath = fs::path(dir) / kGeometryFile;
    if (!fs::exists(geoPath, ec)) {
        err = "'" + dir + "' is not a card: no " + kGeometryFile;
        return nullptr;
    }

    std::ifstream gf(geoPath, std::ios::binary);
    if (!gf) {
        err = "'" + geoPath.string() + "': cannot open for reading";
        return nullptr;
    }
    std::string text((std::istreambuf_iterator<char>(gf)), std::istreambuf_iterator<char>());
    if (gf.bad()) {
        err = "'" + geoPath.string() + "': read error";
        return nullptr;
    }

    Geometry geo;
    if (!parseGeometry(text, geo, err)) return nullptr;

    // Decide writability once, for the whole card: an explicit RO request, a
    // non-writable descriptor, or any non-writable backing file. If the operator did
    // NOT ask for RO but the host forces it, we say so (forced_) -- exactly HostFile's
    // rule (media.cpp), so an afternoon of writes never bounces silently at sync().
    bool forced = false;
    if (!readOnly && !hostWritable(geoPath)) forced = true;

    std::unique_ptr<DirectoryMedia> card(new DirectoryMedia());
    card->dir_        = dir;
    card->sectorSize_ = geo.sectorSize;
    card->parts_.reserve(geo.parts.size());  // reserve: no fstream moves after we open them

    uint64_t base = 0;
    for (const auto& gp : geo.parts) {
        fs::path bpath = fs::path(dir) / gp.file;
        if (!fs::exists(bpath, ec)) {
            err = "'" + dir + "': backing file '" + gp.file + "' is missing";
            return nullptr;
        }
        if (!fs::is_regular_file(bpath, ec)) {
            err = "'" + bpath.string() + "' is not a regular file";
            return nullptr;
        }
        uint64_t onDisk   = (uint64_t)fs::file_size(bpath, ec);
        uint64_t declared = gp.sectors * (uint64_t)geo.sectorSize;
        if (onDisk > declared) {
            // The file overflows the slot the geometry gave it -- a malformed card,
            // not something to silently truncate.
            err = "'" + gp.file + "' is larger (" + std::to_string(onDisk) +
                  ") than its declared partition (" + std::to_string(declared) + " bytes)";
            return nullptr;
        }
        if (!readOnly && !hostWritable(bpath)) forced = true;

        DirectoryMedia::Partition part;
        part.name    = gp.name;
        part.path    = bpath.string();
        part.lbaBase = base;
        part.bytes   = declared;
        part.onDisk  = onDisk;
        card->parts_.push_back(std::move(part));
        base += declared;
    }
    card->totalBytes_ = base;
    card->readOnly_   = readOnly || forced;
    card->forced_     = forced;

    // Open each backing file's handle and HOLD it. Writable cards open in|out (which
    // needs the file to exist -- it does, checked above -- and does NOT truncate);
    // read-only cards open in.
    auto mode = std::ios::binary | std::ios::in;
    if (!card->readOnly_) mode |= std::ios::out;
    for (auto& p : card->parts_) {
        p.io.open(p.path, mode);
        if (!p.io.is_open()) {
            err = "'" + p.path + "': cannot open";
            return nullptr;
        }
    }
    return card;
}

// The resolver both mains install.
std::unique_ptr<MediaFile> openHostMedia(const std::string& path, bool readOnly,
                                         std::string& err) {
    std::error_code ec;
    if (fs::is_directory(path, ec)) return openDirectoryMedia(path, readOnly, err);
    return openHostFile(path, readOnly, err);
}

// ---------------------------------------------------------------------------
// Authoring a blank card (MOUNT ... CREATE)
// ---------------------------------------------------------------------------
namespace {

// The backing filename a volume gets: its name plus `.img`. So `volume=A:...` lands in
// `A.img`, which is what card.geometry then names -- one obvious file per volume.
std::string backingFileFor(const std::string& name) { return name + ".img"; }

// A volume name is also a bare filename inside the card dir, so it must be a plain token:
// no path separators, whitespace, or the comment character the geometry parser splits on.
bool cleanVolumeName(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (c == '/' || c == '\\' || c == '#') return false;
        if (static_cast<unsigned char>(c) <= ' ') return false;
    }
    return true;
}

// Strict unsigned parse -- the whole token must be digits (no trailing junk, no sign),
// so `volume=A:12x` is the error it looks like rather than a silent 12.
bool parseU64(const std::string& s, uint64_t& out) {
    if (s.empty()) return false;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (uint64_t)(c - '0');
    }
    out = v;
    return true;
}

// A named `format=` template -- the standard geometry for a card family, so the common
// case needs no explicit volume list. Grounded in reference/dual-sd-card.md (the Dual SD
// CP/M 3 DPB: 512-byte sectors, 256 tracks x 61 sectors = 15616 sectors ~= 7.99 MB per
// volume); NOT invented. Returns false (with err) for an unknown name.
bool applyCardTemplate(const std::string& name, CardSpec& spec, std::string& err) {
    if (name == "dualsd") {
        spec.sectorSize = 512;
        spec.volumes    = {{"A", 15616}};  // one blank CP/M 3 data volume
        return true;
    }
    err = "unknown format '" + name + "' (known: dualsd)";
    return false;
}

} // namespace

bool hasCardSpecKeys(const std::vector<std::pair<std::string, std::string>>& opts) {
    for (const auto& kv : opts) {
        std::string k = kv.first;
        for (auto& c : k) c = (char)std::tolower((unsigned char)c);
        if (k == "format" || k == "sector_size" || k == "volume") return true;
    }
    return false;
}

bool parseCardSpec(const std::vector<std::pair<std::string, std::string>>& opts,
                   CardSpec& spec, std::string& err) {
    spec = CardSpec{};

    // Two order-independent passes: gather the explicit bits first, then apply a `format=`
    // template UNDERNEATH them so the operator's own sector_size/volume always win no matter
    // where in the command they appeared. (Applying the template inline would let a
    // `volume=` typed before `format=` be clobbered by the template's own volume list.)
    bool                         haveFormat = false, haveSize = false;
    std::string                  formatName;
    uint32_t                     explicitSize = 512;
    std::vector<CardSpec::Volume> explicitVols;

    for (const auto& kv : opts) {
        std::string k = kv.first;
        for (auto& c : k) c = (char)std::tolower((unsigned char)c);
        const std::string& v = kv.second;

        if (k == "format") {
            formatName = v;
            haveFormat = true;
        } else if (k == "sector_size") {
            uint64_t n = 0;
            if (!parseU64(v, n) || n == 0) {
                err = "sector_size needs a positive number";
                return false;
            }
            explicitSize = (uint32_t)n;
            haveSize     = true;
        } else if (k == "volume") {
            // volume=<name>:<sectors>
            size_t colon = v.find(':');
            if (colon == std::string::npos || colon == 0 || colon + 1 >= v.size()) {
                err = "volume needs <name>:<sectors> (e.g. volume=A:15616)";
                return false;
            }
            std::string name = v.substr(0, colon);
            uint64_t    sec  = 0;
            if (!cleanVolumeName(name)) {
                err = "volume name '" + name + "' must be a bare label";
                return false;
            }
            if (!parseU64(v.substr(colon + 1), sec) || sec == 0) {
                err = "volume '" + name + "' needs a positive sector count";
                return false;
            }
            explicitVols.push_back({name, sec});
        } else {
            err = "'" + kv.first + "' is not a card option (format, sector_size, volume)";
            return false;
        }
    }

    if (haveFormat && !applyCardTemplate(formatName, spec, err)) return false;
    if (haveSize) spec.sectorSize = explicitSize;               // explicit overrides template
    for (auto& v : explicitVols) spec.volumes.push_back(v);     // explicit APPEND to template

    if (spec.volumes.empty()) {
        err = "no volumes: give format=<name> or one or more volume=<name>:<sectors>";
        return false;
    }
    // Reject duplicate volume names -- each is a distinct backing file, and two `A`s would
    // collide on A.img.
    for (size_t i = 0; i < spec.volumes.size(); ++i)
        for (size_t j = i + 1; j < spec.volumes.size(); ++j)
            if (spec.volumes[i].name == spec.volumes[j].name) {
                err = "duplicate volume '" + spec.volumes[i].name + "'";
                return false;
            }
    return true;
}

bool createDirectoryMedia(const std::string& dir, const CardSpec& spec, std::string& err) {
    if (spec.volumes.empty()) {
        err = "a card needs at least one volume";
        return false;
    }
    if (spec.sectorSize == 0) {
        err = "sector size must be positive";
        return false;
    }
    for (const auto& v : spec.volumes) {
        if (!cleanVolumeName(v.name)) {
            err = "volume name '" + v.name + "' must be a bare label";
            return false;
        }
        if (v.sectors == 0) {
            err = "volume '" + v.name + "' has zero sectors";
            return false;
        }
    }

    std::error_code ec;
    if (fs::exists(dir, ec)) {
        // CREATE never clobbers -- an existing folder is mounted, not overwritten.
        err = "'" + dir + "' already exists";
        return false;
    }
    if (!fs::create_directory(dir, ec) || ec) {
        err = "cannot create directory '" + dir + "'";
        return false;
    }

    // The descriptor first, then the (empty, growable) backing files. If anything fails
    // partway, tear the half-built card back down so a retry starts clean and MOUNT does
    // not find a directory that only looks like a card.
    auto bail = [&](const std::string& why) {
        err = why;
        std::error_code rmec;
        fs::remove_all(dir, rmec);
        return false;
    };

    std::ostringstream geo;
    geo << "# altairsim directory card (MOUNT ... CREATE) -- blank, UNFORMATTED volumes.\n";
    geo << "sector_size " << spec.sectorSize << "\n";
    for (const auto& v : spec.volumes)
        geo << "partition " << v.name << " " << backingFileFor(v.name) << " " << v.sectors
            << "\n";

    std::string geoText = geo.str();
    std::string werr;
    fs::path    geoPath = fs::path(dir) / kGeometryFile;
    if (!writeHostFile(geoPath.string(),
                       std::vector<uint8_t>(geoText.begin(), geoText.end()), werr))
        return bail(werr);

    for (const auto& v : spec.volumes) {
        fs::path bpath = fs::path(dir) / backingFileFor(v.name);
        if (fs::exists(bpath, ec)) return bail("'" + bpath.string() + "' already exists");
        if (!writeHostFile(bpath.string(), {}, werr)) return bail(werr);
    }
    return true;
}

// ---------------------------------------------------------------------------
// DirectoryMedia
// ---------------------------------------------------------------------------
DirectoryMedia::~DirectoryMedia() {
    // Last line of defence; UNMOUNT/shutdown sync explicitly (a dtor cannot report a
    // failed write).
    sync();
}

DirectoryMedia::Partition* DirectoryMedia::partitionAt(uint64_t off) {
    for (auto& p : parts_)
        if (off >= p.lbaBase && off < p.lbaBase + p.bytes) return &p;
    return nullptr;  // only reached with an out-of-range offset, guarded by the callers
}

bool DirectoryMedia::readChunk(Partition& p, uint64_t within, uint8_t* buf, size_t n) {
    // What of this chunk is actually on the backing file, and what is erased tail.
    uint64_t avail    = within < p.onDisk ? p.onDisk - within : 0;
    size_t   fromFile = (size_t)std::min<uint64_t>(n, avail);
    if (fromFile) {
        p.io.clear();  // a prior EOF/read may have left flags set
        p.io.seekg((std::streamoff)within, std::ios::beg);
        p.io.read(reinterpret_cast<char*>(buf), (std::streamsize)fromFile);
        if (!p.io) {
            p.io.clear();
            return false;  // a real read error, not EOF (we clamped inside onDisk)
        }
    }
    // Everything past the backing file's current end reads as the erased card byte.
    std::memset(buf + fromFile, kErasedByte, n - fromFile);
    return true;
}

bool DirectoryMedia::writeChunk(Partition& p, uint64_t within, const uint8_t* buf, size_t n) {
    // Writing past the backing file's current end first fills the gap with the erased
    // byte, so a later read of the gap returns what an erased card would -- not zeros,
    // and not whatever the filesystem left there.
    if (within > p.onDisk) {
        uint8_t fill[512];
        std::memset(fill, kErasedByte, sizeof(fill));  // the loop writes in chunks of this
        p.io.clear();
        p.io.seekp((std::streamoff)p.onDisk, std::ios::beg);
        uint64_t gap = within - p.onDisk;
        while (gap) {
            size_t step = (size_t)std::min<uint64_t>(gap, sizeof(fill));
            p.io.write(reinterpret_cast<const char*>(fill), (std::streamsize)step);
            if (!p.io) return false;
            gap -= step;
        }
        p.onDisk = within;
    }

    p.io.clear();
    p.io.seekp((std::streamoff)within, std::ios::beg);
    p.io.write(reinterpret_cast<const char*>(buf), (std::streamsize)n);
    if (!p.io) return false;
    if (within + n > p.onDisk) p.onDisk = within + n;
    p.dirty = true;
    return true;
}

bool DirectoryMedia::readAt(uint64_t off, uint8_t* buf, size_t n) {
    if (n == 0) return true;
    // All-or-nothing, and never off the end of the card (media.h contract).
    if (off > totalBytes_ || totalBytes_ - off < n) return false;

    size_t done = 0;
    while (done < n) {
        Partition* p = partitionAt(off + done);
        if (!p) return false;  // cannot happen given the bound above, but be safe
        uint64_t within = (off + done) - p->lbaBase;
        size_t   chunk  = (size_t)std::min<uint64_t>(n - done, p->bytes - within);
        if (!readChunk(*p, within, buf + done, chunk)) return false;
        done += chunk;
    }
    return true;
}

bool DirectoryMedia::writeAt(uint64_t off, const uint8_t* buf, size_t n) {
    if (readOnly_) return false;
    if (n == 0) return true;
    // A write past the last partition's declared end -- past size() -- is refused: the
    // geometry bound. Within [0, size()) every slice lands inside a declared partition.
    if (off > totalBytes_ || totalBytes_ - off < n) return false;

    size_t done = 0;
    while (done < n) {
        Partition* p = partitionAt(off + done);
        if (!p) return false;
        uint64_t within = (off + done) - p->lbaBase;
        size_t   chunk  = (size_t)std::min<uint64_t>(n - done, p->bytes - within);
        if (!writeChunk(*p, within, buf + done, chunk)) return false;
        done += chunk;
    }
    return true;
}

void DirectoryMedia::sync() {
    if (readOnly_) return;
    // Flush the touched backing files -- the per-sector durability the disk
    // controllers rely on (they sync() after every sector write; mits-88hdsk.cpp,
    // tarbell.cpp). Only the dirty ones do host I/O.
    for (auto& p : parts_) {
        if (!p.dirty) continue;
        p.io.flush();
        if (p.io) p.dirty = false;
    }
}

} // namespace altair
