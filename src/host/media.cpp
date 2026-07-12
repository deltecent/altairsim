#include "host/media.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace altair {

// ---------------------------------------------------------------------------
// The resolver seam
// ---------------------------------------------------------------------------
namespace {
MediaResolver g_resolver;
}

void setMediaResolver(MediaResolver r) { g_resolver = std::move(r); }

std::unique_ptr<MediaFile> openMedia(const std::string& path, bool readOnly, std::string& err) {
    if (!g_resolver) {
        // Not a user error and not survivable: some entry point forgot to install
        // the resolver. Say which, rather than reporting a missing file.
        err = "no media resolver installed (see setMediaResolver in host/media.h)";
        return nullptr;
    }
    return g_resolver(path, readOnly, err);
}

// ---------------------------------------------------------------------------
// HostFile
// ---------------------------------------------------------------------------
std::unique_ptr<MediaFile> openHostFile(const std::string& path, bool readOnly, std::string& err) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        err = "'" + path + "': no such file";
        return nullptr;
    }
    if (std::filesystem::is_directory(path, ec)) {
        err = "'" + path + "' is a directory";
        return nullptr;
    }

    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "'" + path + "': cannot open for reading";
        return nullptr;
    }

    std::unique_ptr<HostFile> h(new HostFile());
    h->path_     = path;
    h->readOnly_ = readOnly;
    h->bytes_.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (f.bad()) {
        err = "'" + path + "': read error";
        return nullptr;
    }

    // WRITE-PROTECT IS DISCOVERED AT MOUNT, NOT AT SYNC -- and it is not an ERROR.
    // A file the host will not let us write is a disk with the write-protect tab
    // out, which is an entirely ordinary disk: it mounts, and it mounts read-only.
    // (Patrick, 2026-07-12: "just enable the RO flag automatically and let the user
    // know.")
    //
    // What must not happen is the SILENT version of it, and that is what
    // readOnlyForced() is for: the operator typed no RO, so the board has to say
    // that it put the tab in on their behalf. Discovering it at sync() instead --
    // after CP/M has spent an afternoon writing to the thing and the flush fails
    // with the work gone -- is the failure this is here to prevent.
    if (!readOnly) {
        auto perms = std::filesystem::status(path, ec).permissions();
        if ((perms & std::filesystem::perms::owner_write) == std::filesystem::perms::none) {
            h->readOnly_ = true;
            h->forced_   = true;
        }
    }
    return h;
}

HostFile::~HostFile() {
    // The last line of defence, not the plan. UNMOUNT and shutdown both sync
    // explicitly, because a destructor cannot report that the write FAILED.
    sync();
}

bool HostFile::readAt(uint64_t off, uint8_t* buf, size_t n) {
    if (off > bytes_.size() || bytes_.size() - off < n) return false;
    std::copy(bytes_.begin() + (ptrdiff_t)off, bytes_.begin() + (ptrdiff_t)(off + n), buf);
    return true;
}

bool HostFile::writeAt(uint64_t off, const uint8_t* buf, size_t n) {
    if (readOnly_) return false;
    if (off + n > bytes_.size()) bytes_.resize(off + n, 0);  // a tape, recording
    std::copy(buf, buf + n, bytes_.begin() + (ptrdiff_t)off);
    dirty_ = true;
    return true;
}

void HostFile::sync() {
    if (!dirty_ || readOnly_) return;
    std::ofstream f(path_, std::ios::binary | std::ios::trunc);
    if (!f) return;
    f.write(reinterpret_cast<const char*>(bytes_.data()), (std::streamsize)bytes_.size());
    if (!f) return;
    dirty_ = false;
}

// ---------------------------------------------------------------------------
// MemoryMedia
// ---------------------------------------------------------------------------
bool MemoryMedia::readAt(uint64_t off, uint8_t* buf, size_t n) {
    if (off > bytes_.size() || bytes_.size() - off < n) return false;
    std::copy(bytes_.begin() + (ptrdiff_t)off, bytes_.begin() + (ptrdiff_t)(off + n), buf);
    return true;
}

bool MemoryMedia::writeAt(uint64_t off, const uint8_t* buf, size_t n) {
    if (readOnly_) return false;
    if (off + n > bytes_.size()) bytes_.resize(off + n, 0);
    std::copy(buf, buf + n, bytes_.begin() + (ptrdiff_t)off);
    return true;
}

} // namespace altair
