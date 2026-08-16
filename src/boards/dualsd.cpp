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
// The command/handshake engine (reference sections 2-3).
// ---------------------------------------------------------------------------

// OUT STATUS -- the command port. Every command is two bytes: a 33H lead (a safety
// sync) then the command code. A byte that arrives without the lead is ignored, which
// is exactly what the lead is for.
void DualSdBoard::outCmd(uint8_t v) {
    if (v == kLead) { armed_ = true; return; }
    if (!armed_) return;
    armed_ = false;
    dispatch(v);
}

void DualSdBoard::dispatch(uint8_t cmd) {
    switch (cmd) {
        case cInitA: curDrive_ = 0; break;   // initialize/mount + select drive A:
        case cInitB: curDrive_ = 1; break;   // initialize/mount + select drive B:
        case cSelA:  curDrive_ = 0; break;   // (re)select an already-initialized drive
        case cSelB:  curDrive_ = 1; break;
        case cSetTrkSec:                                   // collect the address bytes (see decodeAddr)
            phase_   = Phase::CollectAddr;
            addrPtr_ = 0;
            break;
        case cRead:  doRead();  break;
        case cWrite:                                       // the guest now streams a sector to DATA
            phase_   = Phase::WriteXfer;
            xferPtr_ = 0;
            break;
        case cFormat: doFormat(); break;
        case cReset:  resetEngine(); break;
        default:      break;                               // an unassigned opcode is a harmless no-op
    }
}

// OUT DATA -- a byte a command consumes. What it means depends on the phase the last
// command left us in: a SET_TRK_SEC address byte, or a WRITE_SECTOR buffer byte.
void DualSdBoard::outData(uint8_t v) {
    switch (phase_) {
        case Phase::CollectAddr:
            if (addrPtr_ < (int)sizeof addrBuf_) addrBuf_[addrPtr_] = v;
            ++addrPtr_;
            if (addrPtr_ >= kAddrBytes) {
                lba_   = decodeAddr();
                phase_ = Phase::Idle;
            }
            break;
        case Phase::WriteXfer:
            if (xferPtr_ < kSectorSize) buf_[xferPtr_++] = v;
            if (xferPtr_ >= kSectorSize) {
                doWrite();
                phase_ = Phase::Idle;
            }
            break;
        default:
            break;   // a DATA write with nothing expecting it is dropped
    }
}

// IN DATA -- the next byte of a READ_SECTOR transfer. Reading it clears DI7 and advances
// to the following byte, so one IN-per-byte drains the whole sector (reference section 3).
uint8_t DualSdBoard::inData() {
    if (phase_ == Phase::ReadXfer && xferPtr_ < kSectorSize) {
        uint8_t b = buf_[xferPtr_++];
        if (xferPtr_ >= kSectorSize) phase_ = Phase::Idle;
        return b;
    }
    return 0xFF;
}

// The status byte (reference section 3). DI7 (bit7) is high while a read byte is waiting;
// bit0 (write-buffer busy) stays 0 -- the engine takes each written byte immediately, so a
// guest's "wait until bit0 low" before a write falls straight through.
uint8_t DualSdBoard::statusByte() const {
    uint8_t s = 0;
    if (phase_ == Phase::ReadXfer && xferPtr_ < kSectorSize) s |= 0x80;
    return s;
}

// READ_SECTOR: pull the current 512-byte sector into the buffer and arm the read handshake.
// An unreadable or short block (a never-written sector on a blank card, or a read past the
// medium's end) leaves the erased-flash fill in place -- what the real card returns (open
// item 2), not a hard error.
void DualSdBoard::doRead() {
    std::memset(buf_, kErasedFill, kSectorSize);
    if (MediaFile* m = curMedia())
        m->readAt((uint64_t)lba_ * kSectorSize, buf_, kSectorSize);  // leaves erased fill on failure
    phase_   = Phase::ReadXfer;
    xferPtr_ = 0;
}

// WRITE_SECTOR: commit the buffered sector to the medium and sync it, the per-sector
// durability the disk drivers rely on. A write-protected or over-the-end write is dropped
// and reported through drainLog().
void DualSdBoard::doWrite() {
    MediaFile* m = curMedia();
    if (!m) return;
    if (m->readOnly()) {
        say("write to a write-protected card ignored");
        return;
    }
    if (m->writeAt((uint64_t)lba_ * kSectorSize, buf_, kSectorSize))
        m->sync();
    else
        say("write past the end of the card ignored (LBA " + std::to_string(lba_) + ")");
}

// FORMAT_SECTOR (87H): fill the current sector with E5 and commit it. This is the board
// COMMAND's fill (reference section 2), distinct from what an untouched sector reads.
void DualSdBoard::doFormat() {
    MediaFile* m = curMedia();
    if (!m) return;
    if (m->readOnly()) {
        say("format of a write-protected card ignored");
        return;
    }
    uint8_t fill[kSectorSize];
    std::memset(fill, kFormatFill, kSectorSize);
    if (m->writeAt((uint64_t)lba_ * kSectorSize, fill, kSectorSize))
        m->sync();
    else
        say("format past the end of the card ignored (LBA " + std::to_string(lba_) + ")");
}

void DualSdBoard::resetEngine() {
    armed_    = false;
    phase_    = Phase::Idle;
    xferPtr_  = 0;
    addrPtr_  = 0;
    lba_      = 0;
    curDrive_ = 0;
    std::memset(buf_, 0, sizeof buf_);
}

uint32_t DualSdBoard::decodeAddr() const {
    // ⚠ PROVISIONAL -- see the seam comment in dualsd.h. Flat 16-bit sector number, low
    // byte then high, taken as the card LBA. Confirm against SD_CARD.Z80 before trusting
    // this to boot a real image.
    return (uint32_t)addrBuf_[0] | ((uint32_t)addrBuf_[1] << 8);
}

MediaFile* DualSdBoard::curMedia() const {
    if (curDrive_ < 0 || curDrive_ >= (int)drive_.size()) return nullptr;
    return drive_[(size_t)curDrive_].media.get();
}

// ---------------------------------------------------------------------------
// The bus. Two I/O ports, no memory window (the board carries no boot PROM -- CP/M is
// loaded by the CPU board's monitor from track 0; reference section 5).
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
    if (c.port() == (uint8_t)port_) outCmd(c.data);
    else                            outData(c.data);
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
         "STATUS (IN: bit7 data-ready, bit0 write-busy) / command (OUT: 33H lead + code)"},
        {(uint32_t)(b + 1), (uint32_t)(b + 1), "read/write",
         "DATA (IN: read-sector byte / OUT: address or write-sector byte)"},
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
        x.help  = "Which SD socket (0 = drive A:, 1 = drive B:)";
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
// SNAPSHOT / RESTORE (DESIGN.md 13). The engine registers and the sector buffer travel;
// the mounted media are host-backed (reloaded from `mount` in the config on restore) and
// the port strap is config. Nothing is on the Clock.
// ---------------------------------------------------------------------------
void DualSdBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.boolean(armed_);
    w.u8((uint8_t)phase_);
    w.u32(lba_);
    w.raw(buf_, sizeof buf_);
    w.u32((uint32_t)xferPtr_);
    w.raw(addrBuf_, sizeof addrBuf_);
    w.u32((uint32_t)(int32_t)addrPtr_);
    w.u32((uint32_t)(int32_t)curDrive_);
}

void DualSdBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    armed_    = r.boolean();
    phase_    = (Phase)r.u8();
    lba_      = r.u32();
    r.raw(buf_, sizeof buf_);
    xferPtr_  = r.u32();
    r.raw(addrBuf_, sizeof addrBuf_);
    addrPtr_  = (int)(int32_t)r.u32();
    curDrive_ = (int)(int32_t)r.u32();
}

} // namespace altair
