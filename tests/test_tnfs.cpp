// TNFS media, driven against a fake server that answers in RAM.
//
// The whole TNFS protocol -- mount, stat, the chunked slurp, dirty write-back, the
// same-seqno retransmit and the EAGAIN back-off -- is exercised here WITHOUT a real
// socket or a thread. TnfsMedia takes its UDP socket through setTnfsUdpConnector() (the
// same seam media.h's MemoryMedia uses to stand in for the filesystem), so this installs
// a connector that hands back a FakeServer: a UdpSocket whose send()/recv() are a TNFS
// server reduced to one file held in a vector. Deterministic, cross-platform, nothing to
// flake. The real socket path is smoke-checked in tests/sockettest.cpp; the live boot
// off a real tnfsd is the acceptance layer's job.

#include "test.h"

#include "host/disk.h"
#include "host/tnfs.h"
#include "platform/socket.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace altair;

namespace {

// The server's state, shared between the connector's closure and the test so the test
// can seed the file, flip knobs, and read back what a write-back left behind.
struct Backing {
    std::vector<uint8_t> file;
    uint64_t             statSize    = 0;      // 0 -> report file.size(); else this (to fake EOF)
    bool                 readOnly    = false;  // server refuses O_RDWR (EROFS)
    int                  dropReplies = 0;      // make the next N recvs time out (test retransmit)
    int                  eagainCmd   = -1;     // return one EAGAIN for this command id
    uint16_t             lastOpenFlags = 0;    // what OPEN was asked for
};

// ---- little-endian helpers, matching the wire ----
void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(x & 0xFF); v.push_back(x >> 8); }
void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF);
    v.push_back((x >> 16) & 0xFF); v.push_back((x >> 24) & 0xFF);
}
uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

class FakeServer : public platform::UdpSocket {
public:
    explicit FakeServer(std::shared_ptr<Backing> b) : b_(std::move(b)) {}

    bool send(const uint8_t* buf, size_t n, std::string&) override {
        if (n < 4) return true;
        uint8_t seq = buf[2];
        uint8_t cmd = buf[3];
        const uint8_t* p = buf + 4;      // payload
        size_t         plen = n - 4;

        // The real server caches its last reply and, on a retransmit (same seqno),
        // replays it WITHOUT re-executing. Model that -- it is what makes a dropped-
        // datagram retry safe for a stateful op like READ (pos_ must not double-advance).
        if (seq == lastSeq_ && haveCached_) {
            pending_ = cached_;
            has_     = true;
            return true;
        }

        std::vector<uint8_t> payload;
        uint8_t              status = 0x00;

        if (b_->eagainCmd == cmd) {
            b_->eagainCmd = -1;
            status = 0x07;               // EAGAIN
            put16(payload, 2);           // back off 2 ms
        } else switch (cmd) {
            case 0x00:                   // MOUNT
                put16(payload, 0x0103);  // server proto v1.3
                put16(payload, 100);     // min retry 100 ms
                break;
            case 0x24: {                 // STAT (path) -> 22-byte struct, size at offset 6
                uint32_t sz = (uint32_t)(b_->statSize ? b_->statSize : b_->file.size());
                put16(payload, 0x81A4);  // mode: regular file, 0644
                put16(payload, 0); put16(payload, 0);   // uid, gid
                put32(payload, sz);
                put32(payload, 0); put32(payload, 0); put32(payload, 0);  // a/m/ctime
                break;
            }
            case 0x29: {                 // OPEN (flags, mode, path)
                uint16_t flags = get16(p);
                b_->lastOpenFlags = flags;
                if (b_->readOnly && (flags & 0x0002)) { status = 0x14; break; }  // EROFS
                payload.push_back(0x04); // handle
                break;
            }
            case 0x25:                   // LSEEK (handle, whence, off)
                pos_ = get32(p + 2);
                put32(payload, pos_);
                break;
            case 0x21: {                 // READ (handle, size)
                uint16_t want = get16(p + 1);
                uint64_t avail = b_->file.size() > pos_ ? b_->file.size() - pos_ : 0;
                if (avail == 0) { status = 0x21; break; }   // EOF
                uint16_t got = (uint16_t)std::min<uint64_t>({want, avail, 512});
                put16(payload, got);
                payload.insert(payload.end(), b_->file.begin() + pos_,
                               b_->file.begin() + pos_ + got);
                pos_ += got;
                break;
            }
            case 0x22: {                 // WRITE (handle, size, data)
                uint16_t sz = get16(p + 1);
                if (plen < 3u + sz) { status = 0x03; break; }
                if (pos_ + sz > b_->file.size()) b_->file.resize(pos_ + sz, 0);
                std::memcpy(b_->file.data() + pos_, p + 3, sz);
                pos_ += sz;
                put16(payload, sz);
                break;
            }
            case 0x23: case 0x01:        // CLOSE, UMOUNT -- status only
                break;
            default:
                status = 0x16;           // ENOSYS
                break;
        }

        std::vector<uint8_t> reply;
        put16(reply, 0xBEEF);            // session id (client picks it up from MOUNT)
        reply.push_back(seq);
        reply.push_back(cmd);
        reply.push_back(status);
        reply.insert(reply.end(), payload.begin(), payload.end());

        cached_    = reply;
        haveCached_ = true;
        lastSeq_   = seq;
        pending_   = std::move(reply);
        has_       = true;
        return true;
    }

    ptrdiff_t recv(uint8_t* buf, size_t n, int, std::string&) override {
        if (b_->dropReplies > 0) { --b_->dropReplies; return 0; }  // simulate a lost datagram
        if (!has_) return 0;
        size_t k = std::min(n, pending_.size());
        std::memcpy(buf, pending_.data(), k);
        has_ = false;
        return (ptrdiff_t)k;
    }

    const std::string& peer() const override { return peer_; }

private:
    std::shared_ptr<Backing> b_;
    std::vector<uint8_t>     pending_, cached_;
    bool                     has_ = false, haveCached_ = false;
    uint8_t                  lastSeq_ = 0xFF;
    uint32_t                 pos_ = 0;
    std::string              peer_ = "fake:16384";
};

// Install a connector that serves `b`, for the duration of one section.
void serve(std::shared_ptr<Backing> b) {
    setTnfsUdpConnector([b](const std::string&, uint16_t, std::string&) {
        return std::make_unique<FakeServer>(b);
    });
}

// A recognisable fill: byte i is i mod 251 (prime; lines up with no sector size, so a
// wrong offset shows immediately). Same trick test_media uses.
std::shared_ptr<Backing> pattern(size_t n) {
    auto b = std::make_shared<Backing>();
    b->file.resize(n);
    for (size_t i = 0; i < n; ++i) b->file[i] = (uint8_t)(i % 251);
    return b;
}

} // namespace

void test_tnfs() {
    SECTION("tnfs: URL recognition");
    {
        CHECK(isTnfsUrl("tnfs://host/x.dsk"), "a tnfs:// path is one");
        CHECK(!isTnfsUrl("cpm.dsk"), "a bare filename is not");
        CHECK(!isTnfsUrl("/abs/path.dsk"), "an absolute host path is not");
    }

    SECTION("tnfs: mount and slurp the whole image");
    {
        // 1000 bytes spans the 512-byte read chunk, so this proves the loop and the seam.
        serve(pattern(1000));
        std::string err;
        auto m = openTnfsMedia("tnfs://server/cpm.dsk", /*readOnly=*/false, err);
        CHECK(m != nullptr, ("mount succeeds: " + err).c_str());
        if (m) {
            CHECK(m->size() == 1000, "size is what STAT reported");
            CHECK(!m->readOnly() && !m->readOnlyForced(), "read/write by default");
            CHECK(m->describe() == "tnfs://server/cpm.dsk", "describe() is the URL");
            uint8_t buf[8] = {0};
            CHECK(m->readAt(0, buf, 4) && buf[0] == 0 && buf[3] == 3, "reads chunk 0");
            CHECK(m->readAt(600, buf, 4) && buf[0] == (uint8_t)(600 % 251), "reads past 512");
            CHECK(!m->readAt(998, buf, 4), "a read past the end fails (all-or-nothing)");
        }
        setTnfsUdpConnector(nullptr);
    }

    SECTION("tnfs: dirty write-back reaches the server");
    {
        auto b = pattern(1000);
        serve(b);
        std::string err;
        auto m = openTnfsMedia("tnfs://server/cpm.dsk", false, err);
        CHECK(m != nullptr, "mount");
        if (m) {
            const uint8_t data[3] = {0xAA, 0xBB, 0xCC};
            CHECK(m->writeAt(300, data, 3), "writeAt into RAM");
            CHECK(b->file[300] == (uint8_t)(300 % 251), "nothing on the server until sync()");
            m->sync();
            CHECK(b->file[300] == 0xAA && b->file[302] == 0xCC, "sync() pushed the dirty range");
            CHECK(b->file[299] == (uint8_t)(299 % 251), "and left the neighbours alone");
        }
        setTnfsUdpConnector(nullptr);
    }

    SECTION("tnfs: WP mounts read-only, opens O_RDONLY, refuses writes");
    {
        auto b = pattern(512);
        serve(b);
        std::string err;
        auto m = openTnfsMedia("tnfs://server/cpm.dsk", /*readOnly=*/true, err);
        CHECK(m != nullptr, "mount");
        if (m) {
            CHECK(m->readOnly(), "readOnly()");
            CHECK(!m->readOnlyForced(), "but not FORCED -- the operator asked");
            CHECK(b->lastOpenFlags == 0x0001, "OPEN was O_RDONLY, not O_RDWR");
            const uint8_t x = 0x55;
            CHECK(!m->writeAt(0, &x, 1), "writeAt is refused");
            m->sync();
            CHECK(b->file[0] == 0, "sync() writes nothing");
        }
        setTnfsUdpConnector(nullptr);
    }

    SECTION("tnfs: a server that refuses write mounts forced-read-only");
    {
        auto b = pattern(512);
        b->readOnly = true;
        serve(b);
        std::string err;
        auto m = openTnfsMedia("tnfs://server/cpm.dsk", /*readOnly=*/false, err);
        CHECK(m != nullptr, ("mounts anyway: " + err).c_str());
        if (m) {
            CHECK(m->readOnly() && m->readOnlyForced(), "read-only, and SAYS it was forced");
        }
        setTnfsUdpConnector(nullptr);
    }

    SECTION("tnfs: a dropped datagram is retransmitted");
    {
        auto b = pattern(300);
        b->dropReplies = 1;             // lose the MOUNT reply once
        serve(b);
        std::string err;
        auto m = openTnfsMedia("tnfs://server/cpm.dsk", false, err);
        CHECK(m != nullptr, ("resend recovers the lost reply: " + err).c_str());
        if (m) CHECK(m->size() == 300, "and the rest of the conversation completed");
        setTnfsUdpConnector(nullptr);
    }

    SECTION("tnfs: an EAGAIN back-off is honoured, then the op succeeds");
    {
        auto b = pattern(300);
        b->eagainCmd = 0x24;            // make STAT say "try again" once
        serve(b);
        std::string err;
        auto m = openTnfsMedia("tnfs://server/cpm.dsk", false, err);
        CHECK(m != nullptr, ("mount survives an EAGAIN: " + err).c_str());
        if (m) CHECK(m->size() == 300, "and STAT eventually returned the size");
        setTnfsUdpConnector(nullptr);
    }

    SECTION("tnfs: an early EOF stops the slurp at what the server actually has");
    {
        auto b = pattern(400);
        b->statSize = 4096;             // STAT lies high; the reads hit EOF at 400
        serve(b);
        std::string err;
        auto m = openTnfsMedia("tnfs://server/cpm.dsk", false, err);
        CHECK(m != nullptr, "mount");
        if (m) CHECK(m->size() == 400, "size is what was read, not what STAT claimed");
        setTnfsUdpConnector(nullptr);
    }

    SECTION("tnfs: a DiskImage reads sectors straight through the network medium");
    {
        // The point of the seam: DiskImage neither knows nor cares the bytes are remote.
        serve(pattern(128 * 32));       // 32 sectors of 128 bytes
        std::string err;
        auto m = openTnfsMedia("tnfs://server/floppy.dsk", true, err);
        CHECK(m != nullptr, ("mount: " + err).c_str());
        if (m) {
            DiskImage disk(std::move(m));
            disk.init(1, 1, false);
            disk.initFormat(0, 0, 0, 0, Density::SD, 32, 128, 0);
            uint8_t sec[128] = {0};
            size_t  got = sizeof(sec);   // the buffer's capacity, checked by readSector
            CHECK(disk.readSector(0, 0, 3, sec, &got), "readSector via TNFS");
            // sector 3 (0-based) begins at byte 3*128 = 384.
            CHECK(got == 128 && sec[0] == (uint8_t)(384 % 251), "the right bytes came back");
        }
        setTnfsUdpConnector(nullptr);
    }

    SECTION("tnfs: bad URLs are refused with a message");
    {
        std::string err;
        CHECK(openTnfsMedia("tnfs://", true, err) == nullptr && !err.empty(), "no host");
        CHECK(openTnfsMedia("tnfs://host", true, err) == nullptr, "no file path");
        CHECK(openTnfsMedia("tnfs://host:99999/x.dsk", true, err) == nullptr, "port out of range");
        CHECK(openTnfsMedia("tnfs://host:xyz/x.dsk", true, err) == nullptr, "non-numeric port");
    }
}
