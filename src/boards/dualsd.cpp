#include "boards/dualsd.h"

#include "core/bus.h"
#include "core/statefile.h"
#include "core/value.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace altair {

DualSdBoard::DualSdBoard() { drive_.resize((size_t)kDrives); }

// ---------------------------------------------------------------------------
// The command/handshake engine -- a faithful port of the ESP32 firmware
// (S100_ESP32_Firmware_v1.5, loop() + runCmd()). See dualsd.h for the port model.
// ---------------------------------------------------------------------------

// One host->board byte (an OUT to the DATA port). It is the 33H lead, a command code, or a
// command's argument/write byte, depending on where we are in the stream.
void DualSdBoard::feedInput(uint8_t v) {
    switch (in_) {
        case In::Idle:
            if (v == kLead) in_ = In::Cmd;   // else: unexpected prefix byte, ignored (firmware warns)
            return;

        case In::Cmd:
            startCommand(v);
            return;

        case In::Collect:                    // a fixed count of argument/write bytes
            if (got_ < kBufMax) inBuf_[got_] = v;
            ++got_;
            if (got_ >= need_) completeCollect();
            return;

        case In::CollectDisp:                // DISP: a NUL-terminated string, discarded
            if (v == 0x00) reply(kStatOk);   // (else keep swallowing the display text)
            return;

        case In::CollectEchoLen:             // ECHO: 2-byte length, MSB first
            if (got_ < 2) inBuf_[got_] = v;
            ++got_;
            if (got_ >= 2) {
                echoLen_ = ((uint32_t)inBuf_[0] << 8) | inBuf_[1];
                if (echoLen_ > kBufMax) echoLen_ = kBufMax;   // safety cap (firmware bufData is 1024)
                if (echoLen_ == 0) {
                    reply(kStatOk);
                } else {
                    got_ = 0;
                    need_ = echoLen_;
                    in_ = In::CollectEchoData;
                }
            }
            return;

        case In::CollectEchoData:            // ECHO: the payload, echoed straight back
            if (got_ < kBufMax) inBuf_[got_] = v;
            ++got_;
            if (got_ >= need_) {
                beginReply();
                for (uint32_t i = 0; i < echoLen_; ++i) queueByte(inBuf_[i]);
                queueByte(kStatOk);
                in_ = In::Idle;
            }
            return;
    }
}

// A freshly received command code (firmware runCmd, plus the loop() range check). An
// in-range command always ends by returning a STATUS byte; RESET reboots and returns nothing;
// an out-of-range code is ignored with no reply (firmware "Invalid Command", no SendData).
void DualSdBoard::startCommand(uint8_t cmd) {
    cmd_ = cmd;
    switch (cmd) {
        case cInit1: reply(doInit(0)); return;
        case cInit2: reply(doInit(1)); return;
        case cSel1:  curDrive_ = 0; reply(curMedia() ? kStatOk : kStatErr); return;
        case cSel2:  curDrive_ = 1; reply(curMedia() ? kStatOk : kStatErr); return;

        case cSetTrkSec: need_ = 2; got_ = 0; in_ = In::Collect; return;   // track, sector
        case cRead: {
            beginReply();
            uint8_t st = doRead();     // queues 512 data bytes
            queueByte(st);
            in_ = In::Idle;
            return;
        }
        case cWrite:  need_ = kSectorSize; got_ = 0; in_ = In::Collect; return;  // 512 data bytes
        case cFormat: need_ = 2; got_ = 0; in_ = In::Collect; return;            // 16-bit count, LSB first
        case cReset:  resetEngine(); return;                                     // reboots: no status

        case cFwVer:
            beginReply();
            queueByte(kBoardId);
            queueByte(kFwMajor);
            queueByte(kFwMinor);
            queueByte(kStatOk);
            in_ = In::Idle;
            return;

        case cSetLba: need_ = 4; got_ = 0; in_ = In::Collect; return;   // 32-bit LBA, MS byte first

        case cType:
            beginReply();
            queueByte(kCardType);                       // no physical card modeled
            queueByte(curMedia() ? kStatOk : kStatErr);
            in_ = In::Idle;
            return;

        case cCap: {
            beginReply();
            uint32_t caps = curMedia() ? (uint32_t)(curMedia()->size() / kSectorSize) : 0;
            queueByte((uint8_t)(caps >> 24));
            queueByte((uint8_t)(caps >> 16));
            queueByte((uint8_t)(caps >> 8));
            queueByte((uint8_t)caps);
            queueByte(curMedia() ? kStatOk : kStatErr);
            in_ = In::Idle;
            return;
        }

        case cCid:
        case cCsd:
            beginReply();
            for (int i = 0; i < 16; ++i) queueByte(0x00);   // card identity not modeled
            queueByte(curMedia() ? kStatOk : kStatErr);
            in_ = In::Idle;
            return;

        case cDisp: in_ = In::CollectDisp; return;
        case cEcho: got_ = 0; in_ = In::CollectEchoLen; return;

        default: in_ = In::Idle; return;   // out-of-range command: no reply (firmware sends none)
    }
}

// A fixed-count Collect just finished; run the command it was gathering bytes for.
void DualSdBoard::completeCollect() {
    switch (cmd_) {
        case cSetTrkSec:
            // getTrkSec: track byte first (high), sector byte second (low). LBA = track*256 + sector.
            lba_ = ((uint32_t)inBuf_[0] << 8) | inBuf_[1];
            reply(kStatOk);
            return;
        case cWrite:
            reply(doWrite());
            return;
        case cFormat: {
            uint32_t count = (uint32_t)inBuf_[0] | ((uint32_t)inBuf_[1] << 8);  // LSB, MSB
            reply(doFormat(count));
            return;
        }
        case cSetLba:
            lba_ = ((uint32_t)inBuf_[0] << 24) | ((uint32_t)inBuf_[1] << 16) |
                   ((uint32_t)inBuf_[2] << 8)  |  (uint32_t)inBuf_[3];
            reply(kStatOk);
            return;
        default:
            in_ = In::Idle;
            return;
    }
}

// ---- the output FIFO and the STATUS-port handshake ----

// IN DATA -- the next byte the ESP32 is returning. Reading it advances to the following byte;
// draining the FIFO drops DI7. A read past the end floats 0xFF (the driver never reads when
// DI7 is low).
uint8_t DualSdBoard::inData() {
    if (outPtr_ < outLen_) {
        readGap_ = true;   // SENDACT drops the instant the CPU reads the byte (see readGap_)
        return out_[outPtr_++];
    }
    return 0xFF;   // a read past the end floats 0xFF (the driver never reads when DI7 is low)
}

// The STATUS byte. DI7 (bit7) says a byte is presented on the DATA port RIGHT NOW; the ESP32
// drives it per byte, so it drops for one poll after each read (readGap_) before the next byte
// raises it again. bit0 (write-buffer busy) stays 0 -- the engine takes each written byte at
// once, so a guest's "wait until bit0 low" before a write falls straight through.
//
// bits 1 and 2 are the two sockets' CARD-DETECT lines: bit1 (0x02) = SD card 1 (drive C:)
// inserted, bit2 (0x04) = SD card 2 (drive D:) inserted. The CP/M 3 BIOS reads these to decide
// a drive is there before it touches it -- reverse-engineered from the shipping non-banked
// BIOS3 binary, which has no published source: BC63 `AND 02` prints "SD Drive 1 ... not
// Present" when bit1 is clear, BC70 `AND 04` / C28D `BIT 2,A` gate drive 2, and BC57 `CP FF`
// treats an all-ones read (a floating bus, i.e. NO board in the slot) as "No Dual SD Card Board
// Detected". A present board with no cards therefore returns 0x00 here -- never 0xFF. See
// reference/dual-sd-card.md section 3.
uint8_t DualSdBoard::statusByte() {
    uint8_t cd = 0;                                             // card-detect, one bit per socket
    if (drive_.size() > 0 && drive_[0].media) cd |= 0x02;      // SD card 1 present (drive C:)
    if (drive_.size() > 1 && drive_[1].media) cd |= 0x04;      // SD card 2 present (drive D:)

    if (readGap_) {           // the interval after a read, before the ESP32 presents the next
        readGap_ = false;
        return cd;
    }
    return cd | ((outPtr_ < outLen_) ? 0x80 : 0x00);
}

void DualSdBoard::beginReply() { outLen_ = 0; outPtr_ = 0; readGap_ = false; }

void DualSdBoard::queueByte(uint8_t b) {
    if (outLen_ < sizeof out_) out_[outLen_++] = b;
}

// A command with no data payload: clear the FIFO, queue just the STATUS byte, and idle.
void DualSdBoard::reply(uint8_t status) {
    beginReply();
    queueByte(status);
    in_ = In::Idle;
}

// ---- the disk commands ----

// INIT (firmware sdInit): select the drive; OK if a card is mounted in that socket, else ERR.
uint8_t DualSdBoard::doInit(int drive) {
    curDrive_ = drive;
    return curMedia() ? kStatOk : kStatErr;
}

// READ_SECTOR: queue the current 512-byte sector, then the STATUS byte. On a failed read (no
// media, or an offset past the medium) the firmware still sends 512 bytes -- of 0x00 -- and
// STATUS = ERR. A never-written but in-range sector is not a failure: the medium hands back
// the erased-card fill (a DirectoryMedia returns 0xFF), and STATUS is OK.
uint8_t DualSdBoard::doRead() {
    uint8_t sec[kSectorSize];
    MediaFile* m = curMedia();
    if (!m || !m->readAt((uint64_t)lba_ * kSectorSize, sec, kSectorSize)) {
        for (size_t i = 0; i < kSectorSize; ++i) queueByte(0x00);
        return kStatErr;
    }
    for (size_t i = 0; i < kSectorSize; ++i) queueByte(sec[i]);
    return kStatOk;
}

// WRITE_SECTOR: commit the buffered sector and sync it (the per-sector durability the disk
// drivers rely on). A write-protected or over-the-end write fails with STATUS = ERR and is
// reported through drainLog().
uint8_t DualSdBoard::doWrite() {
    MediaFile* m = curMedia();
    if (!m) return kStatErr;
    if (m->readOnly()) {
        say("write to a write-protected card ignored");
        return kStatErr;
    }
    if (m->writeAt((uint64_t)lba_ * kSectorSize, inBuf_, kSectorSize)) {
        m->sync();
        return kStatOk;
    }
    say("write past the end of the card ignored (LBA " + std::to_string(lba_) + ")");
    return kStatErr;
}

// FORMAT_SECTOR: fill `count` sectors with E5, starting at the current sector and advancing it
// (firmware advances `sector` as it goes). Sector 0 is never formatted -- the firmware guards
// the boot sector. STATUS = ERR the moment a sector cannot be written.
uint8_t DualSdBoard::doFormat(uint32_t count) {
    MediaFile* m = curMedia();
    if (!m) return kStatErr;
    if (m->readOnly()) {
        say("format of a write-protected card ignored");
        return kStatErr;
    }
    uint8_t fill[kSectorSize];
    std::memset(fill, kFormatFill, kSectorSize);

    uint8_t  status = kStatOk;
    bool     wrote  = false;
    uint32_t s      = lba_;
    while (count > 0) {
        if (s != 0) {   // never format sector 0 (the boot sector) -- firmware
            if (!m->writeAt((uint64_t)s * kSectorSize, fill, kSectorSize)) {
                say("format past the end of the card ignored (LBA " + std::to_string(s) + ")");
                status = kStatErr;
                break;
            }
            wrote = true;
        }
        --count;
        ++s;
    }
    if (wrote) m->sync();
    lba_ = s;   // firmware leaves the current sector advanced past the formatted run
    return status;
}

void DualSdBoard::resetEngine() {
    in_       = In::Idle;
    cmd_      = 0;
    lba_      = 0;
    need_     = 0;
    got_      = 0;
    echoLen_  = 0;
    outLen_   = 0;
    outPtr_   = 0;
    readGap_  = false;
    curDrive_ = 0;
    std::memset(inBuf_, 0, sizeof inBuf_);
}

MediaFile* DualSdBoard::curMedia() const {
    if (curDrive_ < 0 || curDrive_ >= (int)drive_.size()) return nullptr;
    return drive_[(size_t)curDrive_].media.get();
}

// ---------------------------------------------------------------------------
// The bus. Two I/O ports, no memory window (the board carries no boot PROM -- CP/M is loaded
// by the CPU board's monitor; reference section 5).
// ---------------------------------------------------------------------------
bool DualSdBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type == Cycle::IoRead || c.type == Cycle::IoWrite) {
        uint8_t p = c.port();
        return p == (uint8_t)port_ || p == (uint8_t)(port_ + 1);
    }
    return false;
}

uint8_t DualSdBoard::read(const BusCycle& c) {
    if (c.type != Cycle::IoRead) return 0xFF;
    if (c.port() == (uint8_t)port_) return statusByte();
    return inData();
}

void DualSdBoard::write(const BusCycle& c) {
    if (c.type != Cycle::IoWrite) return;
    if (c.port() == (uint8_t)port_)
        beginReply();          // STATUS-port write: flush pending read data (driver housekeeping)
    else
        feedInput(c.data);     // DATA-port write: one byte into the ESP32 input stream
}

// ---------------------------------------------------------------------------
// Lifecycle.
// ---------------------------------------------------------------------------
void DualSdBoard::reset(Reset) { resetEngine(); }

void DualSdBoard::power() { resetEngine(); }

void DualSdBoard::configChanged() { decodeChanged(); }  // `port` moved the decode

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> DualSdBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Base address. The board decodes two ports: BASE (status/command) and BASE+1 (data)";
        x.kind  = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xFE;  // two ports must fit under 0xFF
        x.get   = [this] { return Value::ofInt(port_); };
        x.set   = [this](const Value& v, std::string&) {
            port_ = (uint16_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<MapEntry> DualSdBoard::ioMap() const {
    uint8_t b = (uint8_t)port_;
    return {
        {(uint32_t)(b + 0), (uint32_t)(b + 0), "read/write",
         "STATUS (IN: bit7 data-ready, bit0 write-busy) / flush (OUT)"},
        {(uint32_t)(b + 1), (uint32_t)(b + 1), "read/write",
         "DATA (IN: returned byte + status / OUT: 33H lead, command, arguments)"},
    };
}

// ---------------------------------------------------------------------------
// Units, MOUNT, UNMOUNT, and the [[board.drive]] sub-unit table (the two SD sockets).
// ---------------------------------------------------------------------------
static int dualsdDriveIndex(const std::string& unit, int count) {
    if (unit.rfind("drive", 0) != 0) return -1;
    const std::string n = unit.substr(5);
    if (n.empty()) return -1;
    for (char ch : n)
        if (ch < '0' || ch > '9') return -1;
    int i = std::stoi(n);
    return (i >= 0 && i < count) ? i : -1;
}

std::vector<UnitDef> DualSdBoard::units() const {
    std::vector<UnitDef> u;
    for (int i = 0; i < kDrives; ++i) {
        UnitDef x;
        const auto& d = drive_[(size_t)i];
        x.name  = "drive" + std::to_string(i);
        x.kind  = UnitKind::Disk;
        x.state = d.media ? d.path : "(empty)";
        if (d.media) {
            x.readOnly       = d.media->readOnly();
            x.readOnlyForced = d.media->readOnlyForced();
        }
        u.push_back(std::move(x));
    }
    return u;
}

bool DualSdBoard::mount(const std::string& unit, const std::string& path, bool ro, std::string& err) {
    int i = dualsdDriveIndex(unit, kDrives);
    if (i < 0) {
        err = "no unit `" + unit + "` on " + id + " (it has drive0.." +
              std::to_string(kDrives - 1) + ")";
        return false;
    }

    // The card is addressed directly by byte offset, so the medium is held raw -- no
    // DiskImage geometry probe. A DirectoryMedia card owns its own geometry; any MediaFile
    // (a plain image, a MemoryMedia in a test) works the same way.
    auto media = openMedia(resolvePath(path), ro, err);
    if (!media) { err += pathNote(path); return false; }

    if (media->readOnlyForced()) {
        char m[192];
        std::snprintf(m, sizeof m,
                      "%s: drive%d mounted WRITE-PROTECTED -- the host will not let us write %s",
                      id.c_str(), i, path.c_str());
        say(m);
    }

    drive_[(size_t)i].media = std::move(media);
    drive_[(size_t)i].path  = path;
    return true;
}

bool DualSdBoard::unmount(const std::string& unit, std::string& err) {
    int i = dualsdDriveIndex(unit, kDrives);
    if (i < 0) { err = "no unit `" + unit + "` on " + id; return false; }

    Drive& d = drive_[(size_t)i];
    if (!d.media) { err = id + ":" + unit + " is empty"; return false; }

    d.media->sync();
    d.media.reset();
    d.path.clear();
    return true;
}

std::vector<std::string> DualSdBoard::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    for (auto& s : out) s = id + ":" + s;
    return out;
}

std::vector<Property> DualSdBoard::subUnitProperties(const std::string& table) const {
    if (table != "drive") return {};
    std::vector<Property> p;
    {
        Property x;
        x.name  = "unit";
        x.help  = "Which SD socket (0 = drive 1 / C:, 1 = drive 2 / D:)";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.min   = 0;
        x.max   = kDrives - 1;
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "mount";
        x.help = "The card (a directory card, or an image) to put in it. Relative to THIS FILE.";
        x.kind = Kind::Str;
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "readonly";
        x.help    = "Write-protect the card";
        x.kind    = Kind::Bool;
        x.aliases = {"writeprotect"};
        p.push_back(std::move(x));
    }
    return p;
}

bool DualSdBoard::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
    if (table != "drive") {
        err = type() + " has no [[board." + table + "]] table";
        return false;
    }

    int         unit = -1;
    std::string path;
    bool        ro = false;
    for (const auto& [k, v] : kv) {
        if (k == "unit") unit = std::stoi(v);
        else if (k == "mount") path = v;
        else if (k == "readonly") {
            Value       bv;
            std::string e;
            if (parseValue(v, Kind::Bool, bv, e)) ro = bv.b();
        }
    }

    if (unit < 0) { err = "[[board.drive]] needs a `unit`"; return false; }
    if (unit >= kDrives) {
        err = "[[board.drive]] unit " + std::to_string(unit) + " but the card has " +
              std::to_string(kDrives) + " sockets";
        return false;
    }
    if (path.empty()) return true;
    return mount("drive" + std::to_string(unit), path, ro, err);
}

std::vector<Board::SubUnit> DualSdBoard::subUnits() const {
    std::vector<SubUnit> out;
    for (int i = 0; i < kDrives; ++i) {
        const Drive& d = drive_[(size_t)i];
        if (!d.media) continue;

        SubUnit su;
        su.table = "drive";
        su.fields.push_back({"unit", std::to_string(i), false});
        su.fields.push_back({"mount", d.path, true});
        if (d.media->readOnly()) su.fields.push_back({"readonly", "true", false});
        out.push_back(std::move(su));
    }
    return out;
}

// ---------------------------------------------------------------------------
// SNAPSHOT / RESTORE (DESIGN.md 13). The engine state and the buffers travel; the mounted
// media are host-backed (reloaded from `mount` in the config on restore) and the port strap
// is config. Nothing is on the Clock.
// ---------------------------------------------------------------------------
void DualSdBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u8((uint8_t)in_);
    w.u8(cmd_);
    w.u32(lba_);
    w.u32(need_);
    w.u32(got_);
    w.u32(echoLen_);
    w.raw(inBuf_, sizeof inBuf_);
    w.u32(outLen_);
    w.u32(outPtr_);
    w.raw(out_, sizeof out_);
    w.u8(readGap_ ? 1 : 0);
    w.u32((uint32_t)(int32_t)curDrive_);
}

void DualSdBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    in_       = (In)r.u8();
    cmd_      = r.u8();
    lba_      = r.u32();
    need_     = r.u32();
    got_      = r.u32();
    echoLen_  = r.u32();
    r.raw(inBuf_, sizeof inBuf_);
    outLen_   = r.u32();
    outPtr_   = r.u32();
    r.raw(out_, sizeof out_);
    readGap_  = r.u8() != 0;
    curDrive_ = (int)(int32_t)r.u32();
}

} // namespace altair
