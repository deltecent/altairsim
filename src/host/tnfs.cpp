#include "host/tnfs.h"

#include "platform/socket.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace altair {

// The connector seam (tnfs.h). Default is the real network; a test swaps in a fake.
namespace {
UdpConnector g_connector;
}
void setTnfsUdpConnector(UdpConnector c) { g_connector = std::move(c); }

namespace {

std::unique_ptr<platform::UdpSocket> connect(const std::string& host, uint16_t port,
                                             std::string& err) {
    if (g_connector) return g_connector(host, port, err);
    return platform::connectUdp(host, port, err);
}

// --- Wire constants (tnfs-protocol.md; verified against tnfsd/src) ---------------
constexpr uint16_t kDefaultPort = 16384;

constexpr uint8_t CMD_MOUNT  = 0x00;
constexpr uint8_t CMD_UMOUNT = 0x01;
constexpr uint8_t CMD_OPEN   = 0x29;
constexpr uint8_t CMD_READ   = 0x21;
constexpr uint8_t CMD_WRITE  = 0x22;
constexpr uint8_t CMD_CLOSE  = 0x23;
constexpr uint8_t CMD_STAT   = 0x24;
constexpr uint8_t CMD_LSEEK  = 0x25;

constexpr uint16_t O_RDONLY = 0x0001;
constexpr uint16_t O_RDWR   = 0x0003;

constexpr uint8_t SEEK_SET_ = 0x00;

constexpr uint8_t ST_SUCCESS = 0x00;
constexpr uint8_t ST_EACCES  = 0x09;
constexpr uint8_t ST_EROFS   = 0x14;
constexpr uint8_t ST_EAGAIN  = 0x07;
constexpr uint8_t ST_EOF     = 0x21;

// The server clamps a READ to 512 data bytes regardless of transport, so a whole image
// is read (and written back) in chunks of this. A datagram tops out at 532 bytes.
constexpr size_t   kIoChunk = 512;
constexpr uint8_t  kHeaderSz = 4;   // sid(2) + seqno(1) + cmd(1)
constexpr size_t   kMaxDatagram = 1024;

// Everything multi-byte on the wire is little endian (tnfsd/src/endian.c).
void putU16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back((uint8_t)(x & 0xFF));
    v.push_back((uint8_t)(x >> 8));
}
void putU32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x & 0xFF));
    v.push_back((uint8_t)((x >> 8) & 0xFF));
    v.push_back((uint8_t)((x >> 16) & 0xFF));
    v.push_back((uint8_t)((x >> 24) & 0xFF));
}
uint16_t getU16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t getU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

// Human name for a status byte, for error messages.
std::string statusName(uint8_t s) {
    switch (s) {
        case 0x01: return "operation not permitted";
        case 0x02: return "no such file or directory";
        case 0x03: return "I/O error";
        case ST_EAGAIN: return "try again";
        case ST_EACCES: return "permission denied";
        case 0x0D: return "is a directory";
        case ST_EROFS: return "read-only filesystem";
        case ST_EOF: return "end of file";
        case 0xFF: return "invalid TNFS handle";
        default: {
            const char* h = "0123456789ABCDEF";
            return std::string("error 0x") + h[s >> 4] + h[s & 0xF];
        }
    }
}

// tnfs://host[:port]/path  ->  (host, port, path). The path keeps its leading slash and
// is what OPEN/STAT address; the session MOUNTs the server's default root. Returns false
// (and `err`) on a URL that is not this shape.
bool parseUrl(const std::string& url, std::string& host, uint16_t& port, std::string& path,
              std::string& err) {
    const std::string scheme = "tnfs://";
    if (url.rfind(scheme, 0) != 0) {
        err = "'" + url + "': not a tnfs:// URL";
        return false;
    }
    std::string rest = url.substr(scheme.size());
    size_t      slash = rest.find('/');
    std::string authority = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    path = (slash == std::string::npos) ? "/" : rest.substr(slash);

    if (authority.empty()) {
        err = "'" + url + "': no host in tnfs:// URL";
        return false;
    }
    size_t colon = authority.find(':');
    host = authority.substr(0, colon);
    port = kDefaultPort;
    if (colon != std::string::npos) {
        std::string ps = authority.substr(colon + 1);
        // Strict: the whole port token must be digits, or the URL is a typo, not a :16384.
        if (ps.empty() || ps.find_first_not_of("0123456789") != std::string::npos) {
            err = "'" + url + "': bad port '" + ps + "'";
            return false;
        }
        unsigned long n = std::stoul(ps);
        if (n == 0 || n > 65535) {
            err = "'" + url + "': port out of range";
            return false;
        }
        port = (uint16_t)n;
    }
    if (path == "/" || path.empty()) {
        err = "'" + url + "': no file path (need tnfs://host/path/to/image.dsk)";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// TnfsSession -- one request/reply conversation with a server, over a blocking,
// connect()ed UDP socket. All I/O is synchronous: a command has no meaning until its
// reply is in hand (protocol note: never more than one request in flight).
// ---------------------------------------------------------------------------
class TnfsSession {
public:
    TnfsSession(std::unique_ptr<platform::UdpSocket> sock) : sock_(std::move(sock)) {}

    // MOUNT the server's default root; capture the session id and the min retry time.
    bool mount(std::string& err) {
        // reserve() up front on every builder: it is the capacity the bytes need, and it
        // stops GCC's inliner mis-reading a push_back sequence as an overflow of a
        // freshly-allocated small vector (-Werror=stringop-overflow=, a known GCC 12/13
        // false positive on exactly this idiom).
        std::vector<uint8_t> payload;
        payload.reserve(5);
        putU16(payload, 0x0100);   // protocol version we speak: v1.0 (LSB minor, MSB major)
        payload.push_back(0x00);   // mount location: empty -> server DEFAULT_ROOT
        payload.push_back(0x00);   // user: none
        payload.push_back(0x00);   // password: none

        std::vector<uint8_t> reply;
        uint8_t              status = 0;
        if (!transact(CMD_MOUNT, payload, reply, status, err)) return false;
        if (status != ST_SUCCESS) {
            err = "TNFS mount refused: " + statusName(status);
            return false;
        }
        // sid was set from the reply header by transact(). Honor the server's minimum
        // retry interval if it gave us a sane one (reply = ver(2) + retry(2)).
        if (reply.size() >= 4) {
            uint16_t retry = getU16(reply.data() + 2);
            if (retry > 0 && retry < 10000) timeoutMs_ = retry;
        }
        return true;
    }

    // STAT a path for its size in bytes.
    bool statSize(const std::string& path, uint64_t& size, std::string& err) {
        std::vector<uint8_t> payload;
        payload.reserve(path.size() + 1);
        payload.assign(path.begin(), path.end());
        payload.push_back(0x00);
        std::vector<uint8_t> reply;
        uint8_t              status = 0;
        if (!transact(CMD_STAT, payload, reply, status, err)) return false;
        if (status != ST_SUCCESS) {
            err = "TNFS stat '" + path + "': " + statusName(status);
            return false;
        }
        // stat struct: mode(2) uid(2) gid(2) size(4) ... -- size at offset 6.
        if (reply.size() < 10) {
            err = "TNFS stat '" + path + "': short reply";
            return false;
        }
        size = getU32(reply.data() + 6);
        return true;
    }

    // OPEN a path with the given flags; returns the 1-byte handle. `status` carries the
    // server's code so the caller can distinguish "no write" from "no file".
    bool open(const std::string& path, uint16_t flags, uint8_t& handle, uint8_t& status,
              std::string& err) {
        std::vector<uint8_t> payload;
        payload.reserve(5 + path.size());
        putU16(payload, flags);
        putU16(payload, 0x01A4);   // mode 0644, used only if creating (we never O_CREAT)
        payload.insert(payload.end(), path.begin(), path.end());
        payload.push_back(0x00);
        std::vector<uint8_t> reply;
        if (!transact(CMD_OPEN, payload, reply, status, err)) return false;
        if (status != ST_SUCCESS) return true;   // caller inspects status
        if (reply.empty()) {
            err = "TNFS open '" + path + "': no handle in reply";
            status = 0xFF;
            return true;
        }
        handle = reply[0];
        return true;
    }

    // Read `n` bytes from the current file position into buf; returns bytes actually read
    // (0 at EOF). One datagram; the caller loops for more than kIoChunk.
    bool readChunk(uint8_t handle, uint16_t n, uint8_t* buf, uint16_t& got, std::string& err) {
        std::vector<uint8_t> payload;
        payload.reserve(3);
        payload.push_back(handle);
        putU16(payload, n);
        std::vector<uint8_t> reply;
        uint8_t              status = 0;
        if (!transact(CMD_READ, payload, reply, status, err)) return false;
        if (status == ST_EOF) {
            got = 0;
            return true;
        }
        if (status != ST_SUCCESS) {
            err = std::string("TNFS read: ") + statusName(status);
            return false;
        }
        if (reply.size() < 2) {
            err = "TNFS read: short reply";
            return false;
        }
        got = getU16(reply.data());
        if (got > n || (size_t)got + 2 > reply.size()) {
            err = "TNFS read: server returned more than asked";
            return false;
        }
        std::memcpy(buf, reply.data() + 2, got);
        return true;
    }

    bool writeChunk(uint8_t handle, const uint8_t* buf, uint16_t n, std::string& err) {
        std::vector<uint8_t> payload;
        payload.reserve(3 + n);
        payload.push_back(handle);
        putU16(payload, n);
        payload.insert(payload.end(), buf, buf + n);
        std::vector<uint8_t> reply;
        uint8_t              status = 0;
        if (!transact(CMD_WRITE, payload, reply, status, err)) return false;
        if (status != ST_SUCCESS) {
            err = std::string("TNFS write: ") + statusName(status);
            return false;
        }
        if (reply.size() < 2 || getU16(reply.data()) != n) {
            err = "TNFS write: short write";
            return false;
        }
        return true;
    }

    bool seek(uint8_t handle, uint32_t off, std::string& err) {
        std::vector<uint8_t> payload;
        payload.reserve(6);
        payload.push_back(handle);
        payload.push_back(SEEK_SET_);
        putU32(payload, off);
        std::vector<uint8_t> reply;
        uint8_t              status = 0;
        if (!transact(CMD_LSEEK, payload, reply, status, err)) return false;
        if (status != ST_SUCCESS) {
            err = std::string("TNFS seek: ") + statusName(status);
            return false;
        }
        return true;
    }

    // Best-effort teardown: a failed CLOSE/UMOUNT on the way out is not worth reporting
    // (the session is ending anyway), so these swallow errors.
    void close(uint8_t handle) {
        std::vector<uint8_t> payload{handle};
        std::vector<uint8_t> reply;
        uint8_t              status = 0;
        std::string          err;
        transact(CMD_CLOSE, payload, reply, status, err);
    }
    void umount() {
        std::vector<uint8_t> payload;
        std::vector<uint8_t> reply;
        uint8_t              status = 0;
        std::string          err;
        transact(CMD_UMOUNT, payload, reply, status, err);
    }

private:
    // One request -> one reply, with UDP retransmit. On a plain timeout the SAME
    // datagram (same seqno) is resent, which makes the server idempotently resend its
    // cached reply rather than re-execute. On EAGAIN the server is asking us to back
    // off: we sleep the requested time and reissue as a fresh request (new seqno).
    bool transact(uint8_t cmd, const std::vector<uint8_t>& payload, std::vector<uint8_t>& reply,
                  uint8_t& status, std::string& err) {
        constexpr int kMaxTries = 8;

        uint8_t              seq = ++seqno_;
        std::vector<uint8_t> dgram = frame(seq, cmd, payload);

        for (int tries = 0; tries < kMaxTries; ++tries) {
            if (!sock_->send(dgram.data(), dgram.size(), err)) return false;

            std::array<uint8_t, kMaxDatagram> buf{};
            ptrdiff_t r = sock_->recv(buf.data(), buf.size(), timeoutMs_, err);
            if (r < 0) return false;             // hard socket error
            if (r == 0) continue;                // timeout: resend the same datagram, which
                                                 // the server answers from its reply cache
            if (r < kHeaderSz + 1) continue;     // runt
            if (buf[2] != seq) continue;         // a stale reply to an earlier request

            // MOUNT is the one command that learns the session id from the reply.
            if (cmd == CMD_MOUNT) sid_ = getU16(buf.data());

            status = buf[4];
            reply.assign(buf.begin() + kHeaderSz + 1, buf.begin() + r);

            if (status == ST_EAGAIN) {
                // The server is busy and told us how long to wait. Reissue as a FRESH
                // request (new seqno) so its cache does not just replay the EAGAIN.
                uint16_t backoff = reply.size() >= 2 ? getU16(reply.data()) : (uint16_t)timeoutMs_;
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
                seq   = ++seqno_;
                dgram = frame(seq, cmd, payload);
                continue;
            }
            return true;
        }
        err = "TNFS server " + sock_->peer() + " did not respond";
        return false;
    }

    std::vector<uint8_t> frame(uint8_t seq, uint8_t cmd, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> d;
        d.reserve(kHeaderSz + payload.size());
        putU16(d, sid_);
        d.push_back(seq);
        d.push_back(cmd);
        d.insert(d.end(), payload.begin(), payload.end());
        return d;
    }

    std::unique_ptr<platform::UdpSocket> sock_;
    uint16_t                             sid_       = 0x0000;
    uint8_t                              seqno_     = 0;
    int                                  timeoutMs_ = 1000;
};

// ---------------------------------------------------------------------------
// TnfsMedia -- a HostFile with a network under it. Slurped at open, dirty range
// written back at sync(). See tnfs.h and media.h for the model and why it fits.
// ---------------------------------------------------------------------------
class TnfsMedia : public MediaFile {
public:
    static std::unique_ptr<MediaFile> open(const std::string& url, bool readOnly,
                                           std::string& err) {
        std::string host, path;
        uint16_t    port = kDefaultPort;
        if (!parseUrl(url, host, port, path, err)) return nullptr;

        auto sock = connect(host, port, err);
        if (!sock) return nullptr;

        auto session = std::make_unique<TnfsSession>(std::move(sock));
        if (!session->mount(err)) return nullptr;

        uint64_t size = 0;
        if (!session->statSize(path, size, err)) return nullptr;

        // Open read/write unless WP was asked; a server that refuses write serves the
        // image read-only, the same "mount it anyway, and SAY SO" rule as a local file
        // the host will not let us write (media.h).
        bool    forced = false;
        uint8_t handle = 0, status = 0;
        if (!readOnly) {
            if (!session->open(path, O_RDWR, handle, status, err)) return nullptr;
            if (status == ST_EROFS || status == ST_EACCES) {
                readOnly = true;
                forced   = true;
            } else if (status != ST_SUCCESS) {
                err = "TNFS open '" + path + "': " + statusName(status);
                return nullptr;
            }
        }
        if (readOnly) {
            if (!session->open(path, O_RDONLY, handle, status, err)) return nullptr;
            if (status != ST_SUCCESS) {
                err = "TNFS open '" + path + "': " + statusName(status);
                return nullptr;
            }
        }

        auto media    = std::unique_ptr<TnfsMedia>(new TnfsMedia);
        media->url_   = url;
        media->path_  = path;
        media->sess_  = std::move(session);
        media->handle_    = handle;
        media->readOnly_  = readOnly;
        media->forced_    = forced;

        // Slurp the whole image, sequentially, in <=512-byte reads.
        media->bytes_.reserve((size_t)size);
        std::array<uint8_t, kIoChunk> chunk{};
        while (media->bytes_.size() < size) {
            uint16_t want = (uint16_t)std::min<uint64_t>(kIoChunk, size - media->bytes_.size());
            uint16_t got  = 0;
            if (!media->sess_->readChunk(handle, want, chunk.data(), got, err)) return nullptr;
            if (got == 0) break;   // server hit EOF earlier than STAT said; take what we got
            media->bytes_.insert(media->bytes_.end(), chunk.data(), chunk.data() + got);
        }
        media->onServer_ = media->bytes_.size();
        return media;
    }

    ~TnfsMedia() override {
        sync();
        if (sess_) {
            sess_->close(handle_);
            sess_->umount();
        }
    }

    uint64_t size() const override { return bytes_.size(); }
    bool     readOnly() const override { return readOnly_; }
    bool     readOnlyForced() const override { return forced_; }

    bool readAt(uint64_t off, uint8_t* buf, size_t n) override {
        if (off + n > bytes_.size()) return false;   // all-or-nothing (media.h)
        std::memcpy(buf, bytes_.data() + off, n);
        return true;
    }

    bool writeAt(uint64_t off, const uint8_t* src, size_t n) override {
        if (readOnly_) return false;
        if (off + n > bytes_.size()) bytes_.resize((size_t)(off + n), 0);
        std::memcpy(bytes_.data() + off, src, n);
        if (dirtyLo_ >= dirtyHi_) {
            dirtyLo_ = off;
            dirtyHi_ = off + n;
        } else {
            dirtyLo_ = std::min<uint64_t>(dirtyLo_, off);
            dirtyHi_ = std::max<uint64_t>(dirtyHi_, off + n);
        }
        return true;
    }

    void sync() override {
        // sync() returns void and a disk board throws its result away, so a failure here
        // would vanish -- for a local file that is fine (writes don't fail), but a TNFS
        // write that the network eats must not be silent. narrateFlush() posts one line to
        // the media log (media.h) on the good->failing edge and one on the way back, which
        // Machine::drainBoardLog() surfaces to the operator. Not per sector: latched.
        std::string err;
        (void)narrateFlush(err);
    }

    bool commit(std::string& err) override {
        // UNMOUNT's hook. Same narration, and it still RETURNS the error the way it always
        // did -- a tape board (sol/acr) reports commit()'s err directly; a disk board that
        // only ever calls sync() gets the same news through the media log.
        return narrateFlush(err);
    }

    const std::string& describe() const override { return url_; }

private:
    TnfsMedia() = default;

    // Flush the dirty range, and narrate the edges. Returns true when there was nothing to
    // do or the flush reached the server; false (with `err`) when it did not. The message
    // fires ONCE per transition -- a network that stays down would otherwise print a line
    // per sector -- and names the mount, since a media-log line carries no board id.
    bool narrateFlush(std::string& err) {
        if (readOnly_ || dirtyLo_ >= dirtyHi_ || !sess_) return true;
        if (flushDirty(err)) {
            if (writeFailing_) {
                writeFailing_ = false;
                logMediaMessage(url_ + ": saving to the server works again -- "
                                       "changes are being saved");
            }
            return true;
        }
        if (!writeFailing_) {
            writeFailing_ = true;
            logMediaMessage(url_ + ": cannot save changes to the server (" + err +
                            ") -- they are being held in memory only until it recovers");
        }
        return false;
    }

    bool flushDirty(std::string& err) {
        uint64_t off = dirtyLo_;
        if (!sess_->seek(handle_, (uint32_t)off, err)) return false;
        while (off < dirtyHi_) {
            uint16_t n = (uint16_t)std::min<uint64_t>(kIoChunk, dirtyHi_ - off);
            if (!sess_->writeChunk(handle_, bytes_.data() + off, n, err)) return false;
            off += n;
        }
        dirtyLo_ = dirtyHi_ = 0;
        onServer_ = std::max<uint64_t>(onServer_, bytes_.size());
        return true;
    }

    std::string                  url_;
    std::string                  path_;
    std::unique_ptr<TnfsSession> sess_;
    std::vector<uint8_t>         bytes_;
    uint8_t                      handle_   = 0;
    bool                         readOnly_ = false;
    bool                         forced_   = false;
    uint64_t                     dirtyLo_  = 0;
    uint64_t                     dirtyHi_  = 0;
    uint64_t                     onServer_ = 0;
    bool                         writeFailing_ = false;  // latch: warn once, recover once
};

} // namespace

bool isTnfsUrl(const std::string& path) { return path.rfind("tnfs://", 0) == 0; }

std::unique_ptr<MediaFile> openTnfsMedia(const std::string& url, bool readOnly,
                                         std::string& err) {
    return TnfsMedia::open(url, readOnly, err);
}

} // namespace altair
