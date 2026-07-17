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

    // What is on the host, right now. sync() patches in place while this still matches
    // bytes_.size(), and only rewrites the whole file when it does not -- which is a tape
    // that has grown, and nothing else.
    h->onDisk_ = h->bytes_.size();

    // WRITE-PROTECT IS DISCOVERED AT MOUNT, NOT AT SYNC -- and it is not an ERROR.
    // A file the host will not let us write is a disk with the write-protect tab
    // out, which is an entirely ordinary disk: it mounts, and it mounts read-only.
    // The read-only flag goes on by itself, and the operator is told that it did
    // (Patrick, 2026-07-12).
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
    if (!n) return true;
    if (off + n > bytes_.size()) bytes_.resize(off + n, 0);  // a tape, recording
    std::copy(buf, buf + n, bytes_.begin() + (ptrdiff_t)off);

    // Widen the dirty range. Two sectors written either end of a track leave the middle
    // marked dirty too, and that is fine: it is one contiguous write of bytes that are
    // already correct, and tracking a set of ranges to avoid it would cost more than the
    // write does.
    if (dirtyLo_ >= dirtyHi_) {
        dirtyLo_ = off;
        dirtyHi_ = off + n;
    } else {
        if (off < dirtyLo_) dirtyLo_ = off;
        if (off + n > dirtyHi_) dirtyHi_ = off + n;
    }
    return true;
}

// WRITE BACK ONLY WHAT CHANGED. See the comment on dirtyLo_ in media.h -- this used to
// rewrite the entire image on every call, and the floppy controller calls it once per
// sector.
void HostFile::sync() {
    if (readOnly_ || dirtyLo_ >= dirtyHi_) return;

    // THE FILE GREW, so it cannot be patched in place. Only a tape does this -- a floppy
    // image is a fixed platter and never changes size -- and a recording tape grows by
    // one byte at a time, so the rewrite is rare and small. Truncate and start again.
    if (bytes_.size() != onDisk_) {
        std::ofstream f(path_, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f.write(reinterpret_cast<const char*>(bytes_.data()), (std::streamsize)bytes_.size());
        if (!f) return;
        f.close();
        onDisk_  = bytes_.size();
        dirtyLo_ = dirtyHi_ = 0;
        out_.close();  // the handle we held is stale now: it was opened on the old file
        return;
    }

    // The ordinary case: a disk image, the same size it has always been. Open it ONCE and
    // keep it, then patch the bytes that moved.
    if (!out_.is_open()) {
        out_.open(path_, std::ios::binary | std::ios::in | std::ios::out);  // NOT trunc
        if (!out_.is_open()) return;
    }

    out_.seekp((std::streamoff)dirtyLo_, std::ios::beg);
    if (!out_) return;
    out_.write(reinterpret_cast<const char*>(bytes_.data() + dirtyLo_),
               (std::streamsize)(dirtyHi_ - dirtyLo_));
    if (!out_) return;
    out_.flush();  // the guest asked for this to be ON THE DISK, not in libc's buffer
    if (!out_) return;

    dirtyLo_ = dirtyHi_ = 0;
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
