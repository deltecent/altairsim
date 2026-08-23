#include "host/cardimg.h"

#include "host/tnfs.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace altair {

// ---------------------------------------------------------------------------
// The geometry sidecar -- a tiny, host-standalone parser (NO config/toml
// dependency, so the medium is testable without the config layer).
//
// Line-oriented; `#` starts a comment; blank lines ignored. Two directives:
//   sector_size <n>       (optional; default 512)
//   sectors     <n>       (required; the card's declared sector count)
// ---------------------------------------------------------------------------
namespace {

struct Geometry {
    uint32_t sectorSize  = 512;
    uint64_t sectors     = 0;
    bool     sawSectors  = false;
};

bool parseGeometry(const std::string& text, const std::string& geoName, Geometry& g,
                   std::string& err) {
    std::istringstream in(text);
    std::string        line;
    int                lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        std::istringstream ls(line);
        std::string        tok;
        if (!(ls >> tok)) continue;      // blank line
        if (tok[0] == '#') continue;     // whole-line comment

        auto fail = [&](const std::string& why) {
            err = geoName + ":" + std::to_string(lineNo) + ": " + why;
            return false;
        };

        if (tok == "sector_size") {
            uint64_t n = 0;
            if (!(ls >> n) || n == 0) return fail("sector_size needs a positive value");
            g.sectorSize = (uint32_t)n;
        } else if (tok == "sectors") {
            uint64_t n = 0;
            if (!(ls >> n) || n == 0) return fail("sectors needs a positive value");
            g.sectors    = n;
            g.sawSectors = true;
        } else {
            return fail("unknown directive '" + tok + "'");
        }
    }
    if (!g.sawSectors) {
        err = geoName + ": no 'sectors' declared";
        return false;
    }
    return true;
}

// Is the file writable by the host? Mirrors openHostFile's owner_write probe (media.cpp) --
// a file the host will not let us write makes the card read-only, and the board says so via
// readOnlyForced().
bool hostWritable(const fs::path& p) {
    std::error_code ec;
    auto perms = fs::status(p, ec).permissions();
    if (ec) return false;
    return (perms & fs::perms::owner_write) != fs::perms::none;
}

// The sidecar path for a backing image: same base name, `.geo` extension.
fs::path sidecarFor(const fs::path& img) {
    fs::path g = img;
    g.replace_extension(kGeoExt);
    return g;
}

// Case-insensitive extension test.
bool hasExt(const fs::path& p, const char* dotExt) {
    std::string e = p.extension().string();
    std::string want = dotExt;
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    std::transform(want.begin(), want.end(), want.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return e == want;
}

} // namespace

// ---------------------------------------------------------------------------
// Open
// ---------------------------------------------------------------------------
std::unique_ptr<MediaFile> openCardImage(const std::string& imgPath, bool readOnly,
                                         std::string& err) {
    std::error_code ec;
    fs::path img(imgPath);
    fs::path geoPath = sidecarFor(img);

    if (!fs::exists(img, ec)) {
        err = "'" + imgPath + "': no such file";
        return nullptr;
    }
    if (!fs::is_regular_file(img, ec)) {
        err = "'" + imgPath + "' is not a regular file";
        return nullptr;
    }
    if (!fs::exists(geoPath, ec)) {
        err = "'" + imgPath + "': missing sidecar geometry '" + geoPath.filename().string() + "'";
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
    if (!parseGeometry(text, geoPath.filename().string(), geo, err)) return nullptr;

    uint64_t declared = geo.sectors * (uint64_t)geo.sectorSize;
    uint64_t onDisk   = (uint64_t)fs::file_size(img, ec);
    if (onDisk > declared) {
        // The image overflows the card the geometry declared -- a malformed pair, not
        // something to silently truncate.
        err = "'" + imgPath + "' is larger (" + std::to_string(onDisk) +
              ") than its declared card (" + std::to_string(declared) + " bytes)";
        return nullptr;
    }

    // Decide writability once: an explicit RO request, or a host that will not let us write
    // the image or its sidecar. If the operator did NOT ask for RO but the host forces it, we
    // say so (forced_) -- exactly HostFile's rule (media.cpp).
    bool forced = false;
    if (!readOnly && (!hostWritable(img) || !hostWritable(geoPath))) forced = true;

    std::unique_ptr<CardImage> card(new CardImage());
    card->path_          = imgPath;
    card->sectorSize_    = geo.sectorSize;
    card->declaredBytes_ = declared;
    card->onDisk_        = onDisk;
    card->readOnly_      = readOnly || forced;
    card->forced_        = forced;

    // Hold the backing handle open. Writable cards open in|out (which needs the file to
    // exist -- it does, checked above -- and does NOT truncate); read-only cards open in.
    auto mode = std::ios::binary | std::ios::in;
    if (!card->readOnly_) mode |= std::ios::out;
    card->io_.open(imgPath, mode);
    if (!card->io_.is_open()) {
        err = "'" + imgPath + "': cannot open";
        return nullptr;
    }
    return card;
}

// The resolver both mains install.
std::unique_ptr<MediaFile> openHostMedia(const std::string& path, bool readOnly,
                                         std::string& err) {
    // A TNFS URL is a network medium, not a host path -- route it before any of the
    // filesystem reasoning below (which would take tnfs://... for a relative filename).
    // Scope: slurp-sized single-file images; a lazy .img+.geo card is not served this
    // way (a round trip per sector would stall the guest), so refuse that combination.
    if (isTnfsUrl(path)) {
        if (hasExt(fs::path(path), ".img")) {
            err = "'" + path + "': card images (.img) are not supported over TNFS";
            return nullptr;
        }
        return openTnfsMedia(path, readOnly, err);
    }

    std::error_code ec;
    fs::path p(path);

    // Mounting the sidecar itself is a mistake -- point the operator at the image.
    if (hasExt(p, kGeoExt)) {
        fs::path img = p;
        img.replace_extension(".img");
        err = "'" + path + "' is a geometry sidecar, not a card image; mount '" +
              img.filename().string() + "'";
        return nullptr;
    }

    // A sibling `.geo` makes this a lazy card.
    fs::path geo = sidecarFor(p);
    if (fs::exists(geo, ec) && fs::is_regular_file(geo, ec))
        return openCardImage(path, readOnly, err);

    // No sidecar. A `.img` is a card and REQUIRES its geometry -- refuse rather than mount a
    // truncated image at its (wrong) file size.
    if (hasExt(p, ".img")) {
        err = "'" + path + "': a card image needs a sidecar geometry '" +
              geo.filename().string() + "' (create one, or MOUNT ... CREATE)";
        return nullptr;
    }

    // Anything else: a plain single-file image (floppy .dsk, tape, ...).
    return openHostFile(path, readOnly, err);
}

// ---------------------------------------------------------------------------
// Authoring a blank card (MOUNT ... CREATE)
// ---------------------------------------------------------------------------
namespace {

// Strict unsigned parse -- the whole token must be digits (no trailing junk, no sign), so
// `sectors=12x` is the error it looks like rather than a silent 12.
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

// A named `format=` template -- the standard geometry for a card family, so the common case
// needs no explicit sector count. Grounded in reference/dual-sd-card.md (the Dual SD CP/M 3
// DPB: 512-byte sectors, 256 tracks x 61 sectors = 15616 sectors ~= 7.99 MB per card); NOT
// invented. Returns false (with err) for an unknown name.
bool applyCardTemplate(const std::string& name, CardSpec& spec, std::string& err) {
    // `dualsd` and `dualide` are the two halves of the same physical card and share this
    // geometry -- the CP/M 3 DPB and LBA math are identical (reference/dual-ide-card.md 1), so a
    // card is portable between them; `dualide` is an alias that just reads naturally for CF.
    if (name == "dualsd" || name == "dualide") {
        spec.sectorSize = 512;
        spec.sectors    = 15616;   // one blank CP/M 3 card
        return true;
    }
    err = "unknown format '" + name + "' (known: dualsd, dualide)";
    return false;
}

} // namespace

bool hasCardSpecKeys(const std::vector<std::pair<std::string, std::string>>& opts) {
    for (const auto& kv : opts) {
        std::string k = kv.first;
        for (auto& c : k) c = (char)std::tolower((unsigned char)c);
        if (k == "format" || k == "sector_size" || k == "sectors") return true;
    }
    return false;
}

bool parseCardSpec(const std::vector<std::pair<std::string, std::string>>& opts,
                   CardSpec& spec, std::string& err) {
    spec = CardSpec{};

    // Two order-independent passes: gather the explicit bits first, then apply a `format=`
    // template UNDERNEATH them so the operator's own sector_size/sectors always win no matter
    // where in the command they appeared.
    bool        haveFormat = false, haveSize = false, haveSectors = false;
    std::string formatName;
    uint32_t    explicitSize    = 512;
    uint64_t    explicitSectors = 0;

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
        } else if (k == "sectors") {
            uint64_t n = 0;
            if (!parseU64(v, n) || n == 0) {
                err = "sectors needs a positive number";
                return false;
            }
            explicitSectors = n;
            haveSectors     = true;
        } else {
            err = "'" + kv.first + "' is not a card option (format, sector_size, sectors)";
            return false;
        }
    }

    if (haveFormat && !applyCardTemplate(formatName, spec, err)) return false;
    if (haveSize) spec.sectorSize = explicitSize;         // explicit overrides template
    if (haveSectors) spec.sectors = explicitSectors;      // explicit overrides template

    if (spec.sectors == 0) {
        err = "no size: give format=<name> or sectors=<n>";
        return false;
    }
    return true;
}

bool createCardImage(const std::string& imgPath, const CardSpec& spec, std::string& err) {
    if (spec.sectors == 0) {
        err = "a card needs a positive sector count";
        return false;
    }
    if (spec.sectorSize == 0) {
        err = "sector size must be positive";
        return false;
    }

    std::error_code ec;
    fs::path img(imgPath);
    fs::path geoPath = sidecarFor(img);

    // CREATE never clobbers -- an existing image or sidecar is a hard error, not overwritten.
    if (fs::exists(img, ec)) {
        err = "'" + imgPath + "' already exists";
        return false;
    }
    if (fs::exists(geoPath, ec)) {
        err = "'" + geoPath.string() + "' already exists";
        return false;
    }

    // The empty backing image first, then the sidecar. If the sidecar write fails, tear the
    // half-built pair back down so a retry starts clean.
    std::string werr;
    if (!writeHostFile(imgPath, {}, werr)) {
        err = werr;
        return false;
    }

    std::ostringstream geo;
    geo << "# altairsim card image (MOUNT ... CREATE) -- blank, UNFORMATTED.\n";
    geo << "sector_size " << spec.sectorSize << "\n";
    geo << "sectors     " << spec.sectors << "\n";
    std::string geoText = geo.str();

    if (!writeHostFile(geoPath.string(),
                       std::vector<uint8_t>(geoText.begin(), geoText.end()), werr)) {
        err = werr;
        std::error_code rmec;
        fs::remove(img, rmec);   // undo the backing image so a retry is clean
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// CardImage
// ---------------------------------------------------------------------------
CardImage::~CardImage() {
    // Last line of defence; UNMOUNT/shutdown sync explicitly (a dtor cannot report a failed
    // write).
    sync();
}

bool CardImage::readAt(uint64_t off, uint8_t* buf, size_t n) {
    if (n == 0) return true;
    // All-or-nothing, and never off the end of the card (media.h contract).
    if (off > declaredBytes_ || declaredBytes_ - off < n) return false;

    // What of this request is actually on the backing file, and what is erased tail.
    uint64_t avail    = off < onDisk_ ? onDisk_ - off : 0;
    size_t   fromFile = (size_t)std::min<uint64_t>(n, avail);
    if (fromFile) {
        io_.clear();  // a prior EOF/read may have left flags set
        io_.seekg((std::streamoff)off, std::ios::beg);
        io_.read(reinterpret_cast<char*>(buf), (std::streamsize)fromFile);
        if (!io_) {
            io_.clear();
            return false;  // a real read error, not EOF (we clamped inside onDisk_)
        }
    }
    // Everything past the backing file's current end reads as the erased card byte.
    std::memset(buf + fromFile, kErasedByte, n - fromFile);
    return true;
}

bool CardImage::writeAt(uint64_t off, const uint8_t* buf, size_t n) {
    if (readOnly_) return false;
    if (n == 0) return true;
    // A write past the declared end -- past size() -- is refused: the geometry bound.
    if (off > declaredBytes_ || declaredBytes_ - off < n) return false;

    // Writing past the backing file's current end first fills the gap with the erased byte, so
    // a later read of the gap returns what an erased card would -- not zeros, and not whatever
    // the filesystem left there.
    if (off > onDisk_) {
        uint8_t fill[512];
        std::memset(fill, kErasedByte, sizeof(fill));
        io_.clear();
        io_.seekp((std::streamoff)onDisk_, std::ios::beg);
        uint64_t gap = off - onDisk_;
        while (gap) {
            size_t step = (size_t)std::min<uint64_t>(gap, sizeof(fill));
            io_.write(reinterpret_cast<const char*>(fill), (std::streamsize)step);
            if (!io_) return false;
            gap -= step;
        }
        onDisk_ = off;
    }

    io_.clear();
    io_.seekp((std::streamoff)off, std::ios::beg);
    io_.write(reinterpret_cast<const char*>(buf), (std::streamsize)n);
    if (!io_) return false;
    if (off + n > onDisk_) onDisk_ = off + n;
    dirty_ = true;
    return true;
}

void CardImage::sync() {
    if (readOnly_ || !dirty_) return;
    // Flush the backing file -- the per-sector durability the disk controllers rely on (they
    // sync() after every sector write; mits-88hdsk.cpp, tarbell.cpp).
    io_.flush();
    if (io_) dirty_ = false;
}

} // namespace altair
