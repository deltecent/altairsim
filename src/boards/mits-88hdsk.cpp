#include "boards/mits-88hdsk.h"

#include "core/bus.h"
#include "core/statefile.h"
#include "host/media.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace altair {

HdskBoard::HdskBoard() { drive_.resize((size_t)drives_); }

// ---------------------------------------------------------------------------
// Decode. Eight consecutive ports at the base (default 0xA0).
// ---------------------------------------------------------------------------
bool HdskBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    uint8_t p = c.port();
    return p >= port_ && p < port_ + 8;
}

std::vector<MapEntry> HdskBoard::ioMap() const {
    uint8_t b = (uint8_t)port_;
    return {
        {(uint32_t)(b + 0), (uint32_t)(b + 0), "CREADY", "in: controller ready (bit7)"},
        {(uint32_t)(b + 1), (uint32_t)(b + 1), "CSTAT", "in: error/status flags; reading resets CREADY"},
        {(uint32_t)(b + 2), (uint32_t)(b + 2), "ACSTA", "in: command acknowledge (bit7)"},
        {(uint32_t)(b + 3), (uint32_t)(b + 3), "ACMD", "out: command HIGH byte (initiates); in: reset ack"},
        {(uint32_t)(b + 4), (uint32_t)(b + 4), "CDSTA", "in: read data ready (bit7)"},
        {(uint32_t)(b + 5), (uint32_t)(b + 5), "CDATA", "in: read-buffer / status data"},
        {(uint32_t)(b + 6), (uint32_t)(b + 6), "ADSTA", "in: ADATA writable (bit7)"},
        {(uint32_t)(b + 7), (uint32_t)(b + 7), "ADATA", "out: command LOW byte / write-buffer data"},
    };
}

// ---------------------------------------------------------------------------
// Reads. Status ports return bit7 of their flag; the two data ports stream.
// ---------------------------------------------------------------------------
uint8_t HdskBoard::read(const BusCycle& c) {
    uint8_t p = (uint8_t)(c.port() - port_);
    switch (p) {
        case 0:  // CREADY
            return cready_ ? 0x80 : 0x00;

        case 1:  // CSTAT -- reading it resets Controller Ready and the command-ack (section 4)
            cready_ = false;
            ack_    = false;
            return status_;

        case 2:  // ACSTA
            return ack_ ? 0x80 : 0x00;

        case 3:  // ACMD (IN) -- clears the command-ack strobe
            ack_ = false;
            return 0xFF;

        case 4:  // CDSTA
            return cdReady_ ? 0x80 : 0x00;

        case 5:  // CDATA -- the read-data path
            if (phase_ == Phase::ReadStream) {
                uint8_t v = buffers_[xferBuf_][xferPos_++];
                if (xferPos_ >= xferCount_) {
                    phase_   = Phase::Idle;
                    cdReady_ = false;
                }
                return v;
            }
            if (phase_ == Phase::StatusStream) {
                phase_   = Phase::Idle;
                cdReady_ = false;
                return statusByte_;
            }
            return 0xFF;

        case 6:  // ADSTA
            return adReady_ ? 0x80 : 0x00;

        default:  // 7: ADATA (IN) -- write-only in practice
            return 0xFF;
    }
}

// ---------------------------------------------------------------------------
// Writes. A3 initiates a command; A7 stages the low byte or feeds a write stream.
//
// The other six ports are the 88-4PIO's control/DDR registers. HDBL programs them
// during initialisation and we have nothing to do with the values -- so those writes
// are ignored, and a stray DDR-load of 0xFF to A3 assembles the unassigned opcode
// 0xF, which dispatch() treats as a harmless no-op. Modelling the 6820's data-vs-DDR
// bit would only reject those writes we already ignore, at the cost of guessing
// HDBL's exact init sequence.
// ---------------------------------------------------------------------------
void HdskBoard::write(const BusCycle& c) {
    uint8_t p = (uint8_t)(c.port() - port_);

    if (p == 3) {  // ACMD (OUT): the command HIGH byte -- initiates
        dispatch(c.data);
        return;
    }

    if (p == 7) {  // ADATA (OUT): write-stream byte, or the staged command LOW byte
        if (phase_ == Phase::WriteStream) {
            buffers_[xferBuf_][xferPos_++] = c.data;
            if (xferPos_ >= xferCount_) {
                phase_  = Phase::Idle;
                cready_ = true;
            }
            return;
        }
        if (phase_ == Phase::SetByte) {  // IV-byte data (diagnostic, stubbed): accept and finish
            phase_  = Phase::Idle;
            cready_ = true;
            return;
        }
        pendingLow_ = c.data;
        return;
    }

    // p in {0,1,2,4,5,6}: 4PIO control/DDR writes -- nothing for the controller to do.
}

// ---------------------------------------------------------------------------
// Command assembly and dispatch (reference/88-HDSK.md section 5).
// ---------------------------------------------------------------------------
void HdskBoard::dispatch(uint8_t high) {
    command_ = (uint16_t)((uint16_t)high << 8 | pendingLow_);
    ack_     = true;  // the controller has taken the command

    uint8_t op = (uint8_t)((command_ >> 12) & 0x0F);
    switch (op) {
        case 0x0:  // Seek Cylinder
            doSeek(command_);
            cready_ = true;
            break;
        case 0x2:  // Write Sector
            doSector(command_, /*write=*/true);
            cready_ = true;
            break;
        case 0x3:  // Read Sector
            doSector(command_, /*write=*/false);
            cready_ = true;
            break;
        case 0x4:  // Write Buffer
            beginBufferStream(command_, /*write=*/true);
            break;
        case 0x5:  // Read Buffer
            beginBufferStream(command_, /*write=*/false);
            break;
        case 0x6:  // Read Status (IV byte) -- stubbed: the boot BIOS never needs real IV data
            ivAddr_     = (uint8_t)(command_ & 0xFF);
            statusByte_ = 0;
            phase_      = Phase::StatusStream;
            cdReady_    = true;
            cready_     = true;
            break;
        case 0x8:  // Set Byte (IV byte) -- stubbed: latch the address, take the next A7 byte
            ivAddr_  = (uint8_t)(command_ & 0xFF);
            phase_   = Phase::SetByte;
            adReady_ = true;
            cready_  = false;
            break;
        default:  // 6 and 7 aside, the rest are unassigned (and the init DDR writes land here)
            cready_ = true;
            break;
    }
}

// Seek sets the current cylinder for a unit (reference section 5). The head does not
// actually move here in emulated time -- Read/Write Sector use cyl_[unit] directly.
void HdskBoard::doSeek(uint16_t w) {
    int unit = (w >> 10) & 0x03;
    int cyl  = w & 0x03FF;
    status_  = 0;
    if (cyl >= kCylinders) {
        status_ |= 0x02;  // illegal sector/cylinder (section 6, bit1)
        return;
    }
    cyl_[unit] = cyl;
    // Drive-not-ready if no platter of this unit is mounted (either slot). Guard the
    // slot index -- slotFor() returns -1 for a slot past `drives_`.
    bool ready = false;
    for (int pl = 0; pl < kPlattersPerUnit; ++pl) {
        int s = slotFor(unit, pl);
        if (s >= 0 && drive_[(size_t)s].img) { ready = true; break; }
    }
    if (!ready) status_ |= 0x01;
}

// Read/Write Sector: move one disk sector to/from an internal buffer. The low byte is
// platter(7:6) | side(5) | sector(4:0); the high byte carries unit(11:10) and
// buffer(9:8). This is the HDBL/BIOS wire layout (roms/HDBL/HDBL.ASM), which the
// bootable image is built for -- and which overrides the "head in bits 7:5" prose in
// reference/88-HDSK.md section 7 (see docs/boards/mits-88hdsk.md).
void HdskBoard::doSector(uint16_t w, bool write) {
    int unit    = (w >> 10) & 0x03;
    int buf     = (w >> 8) & 0x03;
    int low     = w & 0xFF;
    int platter = (low >> 6) & 0x03;
    int side    = (low >> 5) & 0x01;
    int sector  = low & 0x1F;
    sectorAccess(unit, platter, side, cyl_[unit], sector, buffers_[buf], write);
}

// Read/Write Buffer: stream a buffer to/from the Altair. The low byte is the transfer
// length, where 0 means 256 (section 5); the high byte's bits 9:8 pick the buffer.
void HdskBoard::beginBufferStream(uint16_t w, bool write) {
    int buf = (w >> 8) & 0x03;
    int len = w & 0xFF;
    xferBuf_   = buf;
    xferPos_   = 0;
    xferCount_ = len ? len : 256;
    status_    = 0;
    if (write) {
        phase_   = Phase::WriteStream;
        adReady_ = true;
    } else {
        phase_   = Phase::ReadStream;
        cdReady_ = true;
    }
    cready_ = true;
}

// (unit,platter) -> a mounted-drive slot, or -1. A logical drive is one platter; a
// physical unit carries up to two, so slot = unit*2 + platter (reference section 3).
int HdskBoard::slotFor(int unit, int platter) const {
    if (unit < 0 || unit > 3) return -1;
    if (platter < 0 || platter >= kPlattersPerUnit) return -1;
    int i = unit * kPlattersPerUnit + platter;
    return i < drives_ ? i : -1;
}

// The one place a sector command reaches the image. (cylinder,side,sector) maps 1:1
// onto DiskImage's CHS, because the geometry was init()'d that way in mount().
bool HdskBoard::sectorAccess(int unit, int platter, int side, int cyl, int sector, uint8_t* buf,
                             bool write) {
    status_ = 0;
    int i   = slotFor(unit, platter);
    if (i < 0 || !drive_[(size_t)i].img) {
        status_ |= 0x01;  // drive not ready (section 6, bit0)
        return false;
    }
    if (cyl >= kCylinders || sector >= kSectors) {
        status_ |= 0x02;  // illegal sector (section 6, bit1)
        return false;
    }

    DiskImage* img = drive_[(size_t)i].img.get();
    if (write && img->readOnly()) {
        // Write protect (section 6, bit7). The controller ignores the write and the
        // bytes go nowhere -- writeSector() below refuses a read-only medium too, so
        // this is the flag, not a second gate.
        status_ |= 0x80;
    }

    size_t n  = kSectorBytes;
    bool   ok = write ? img->writeSector(cyl, side, sector, buf, &n)
                      : img->readSector(cyl, side, sector, buf, &n);
    if (!ok && !(status_ & 0x80)) {
        char m[128];
        std::snprintf(m, sizeof m, "%s: %s failed at unit %d platter %d cyl %d side %d sector %d",
                      id.c_str(), write ? "write" : "read", unit, platter, cyl, side, sector);
        say(m);
        status_ |= 0x01;
        return false;
    }
    if (write && ok) img->sync();
    return ok;
}

// ---------------------------------------------------------------------------
// Lifecycle. Section 6: all error bits read 1 on the first read after power-on. A CPU
// reset does not move the heads or lose the controller's buffers; power does.
// ---------------------------------------------------------------------------
void HdskBoard::reset(Reset) {
    status_    = 0xFF;
    cready_    = true;
    ack_       = false;
    cdReady_   = false;
    adReady_   = true;
    phase_     = Phase::Idle;
    xferPos_   = 0;
    xferCount_ = 0;
    pendingLow_ = 0;
}

void HdskBoard::power() {
    reset(Reset::PowerOn);
    std::memset(buffers_, 0, sizeof buffers_);
    for (int& c : cyl_) c = 0;
}

// ---------------------------------------------------------------------------
// Properties: port, drives, interrupt strap.
// ---------------------------------------------------------------------------
std::vector<Property> HdskBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Base address. The board decodes eight ports: BASE+0 .. BASE+7";
        x.kind  = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xF8;  // eight ports must fit under 0xFF
        x.get   = [this] { return Value::ofInt(port_); };
        x.set   = [this](const Value& v, std::string&) {
            port_ = (uint16_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "drives";
        x.help  = "Logical drives (one platter each): slot = unit*2 + platter";
        x.kind  = Kind::Int;
        x.radix = 10;  // NEVER on the wire -> DECIMAL. A count of things.
        x.min   = 1;
        x.max   = kMaxDrives;
        x.get   = [this] { return Value::ofInt(drives_); };
        x.set   = [this](const Value& v, std::string& err) {
            int n = (int)v.i();
            for (int i = n; i < (int)drive_.size(); ++i) {
                if (drive_[(size_t)i].img) {
                    err = "drive" + std::to_string(i) + " still has a disk in it";
                    return false;
                }
            }
            drives_ = n;
            drive_.resize((size_t)n);
            return true;
        };
        p.push_back(std::move(x));
    }
    p.push_back(irqJumperProperty("interrupt", "Where the card's interrupt is soldered", irq_));
    return p;
}

// ---------------------------------------------------------------------------
// Units, MOUNT, UNMOUNT, and the [[board.drive]] sub-unit table. Same shape as the
// hard-sector cards (src/boards/mits-hardsector.cpp) -- a disk mounted by name.
// ---------------------------------------------------------------------------
static int hdskDriveIndex(const std::string& unit, int count) {
    if (unit.rfind("drive", 0) != 0) return -1;
    const std::string n = unit.substr(5);
    if (n.empty()) return -1;
    for (char ch : n)
        if (ch < '0' || ch > '9') return -1;
    int i = std::stoi(n);
    return (i >= 0 && i < count) ? i : -1;
}

std::vector<UnitDef> HdskBoard::units() const {
    std::vector<UnitDef> u;
    for (int i = 0; i < drives_; ++i) {
        UnitDef x;
        const auto& d = drive_[(size_t)i];
        x.name  = "drive" + std::to_string(i);
        x.kind  = UnitKind::Disk;
        x.state = d.img ? d.path : "(empty)";
        if (d.img) {
            x.readOnly       = d.img->readOnly();
            x.readOnlyForced = d.img->readOnlyForced();
        }
        u.push_back(std::move(x));
    }
    return u;
}

bool HdskBoard::mount(const std::string& unit, const std::string& path, bool ro, std::string& err) {
    int i = hdskDriveIndex(unit, drives_);
    if (i < 0) {
        err = "no unit `" + unit + "` on " + id + " (it has drive0.." +
              std::to_string(drives_ - 1) + ")";
        return false;
    }

    auto media = openMedia(resolvePath(path), ro, err);
    if (!media) { err += pathNote(path); return false; }

    // Probe into a FRESH image so a size mismatch does not half-replace the old disk.
    auto img = std::make_unique<DiskImage>(std::move(media));
    if (!sizeMatches(img->size(), kPlatterBytes)) {
        err = std::to_string(img->size()) + " bytes is not an 88-HDSK platter (expected " +
              std::to_string(kPlatterBytes) + " = 406 cyl x 2 sides x 24 sectors x 256)";
        return false;
    }
    // One platter: 406 cylinders, two sides, 24 sectors of 256 bytes, numbered from 0.
    // interleaved=true lays slots out as cyl*2+side, so readSector(cyl,side,sector)
    // lands at (cyl*48 + side*24 + sector)*256 -- the deramp image's linear order.
    img->init(kCylinders, kSides, /*interleaved=*/true);
    img->initFormat(0, kCylinders - 1, 0, kSides - 1, Density::SD, kSectors, kSectorBytes,
                    /*startSector=*/0);

    if (img->readOnlyForced()) {
        char m[192];
        std::snprintf(m, sizeof m,
                      "%s: drive%d mounted WRITE-PROTECTED -- the host will not let us write %s",
                      id.c_str(), i, path.c_str());
        say(m);
    }

    drive_[(size_t)i].img  = std::move(img);
    drive_[(size_t)i].path = path;
    return true;
}

bool HdskBoard::unmount(const std::string& unit, std::string& err) {
    int i = hdskDriveIndex(unit, drives_);
    if (i < 0) { err = "no unit `" + unit + "` on " + id; return false; }

    Drive& d = drive_[(size_t)i];
    if (!d.img) { err = id + ":" + unit + " is empty"; return false; }

    d.img->sync();
    d.img.reset();
    d.path.clear();
    return true;
}

std::vector<std::string> HdskBoard::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    return out;
}

std::vector<Property> HdskBoard::subUnitProperties(const std::string& table) const {
    if (table != "drive") return {};
    std::vector<Property> p;
    {
        Property x;
        x.name  = "unit";
        x.help  = "Which logical drive (slot = unit*2 + platter)";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.min   = 0;
        x.max   = drives_ - 1;
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "mount";
        x.help = "The disk image to put in it. Relative to THIS FILE.";
        x.kind = Kind::Str;
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "readonly";
        x.help    = "Write-protect the disk";
        x.kind    = Kind::Bool;
        x.aliases = {"writeprotect"};
        p.push_back(std::move(x));
    }
    return p;
}

bool HdskBoard::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
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
        else if (k == "readonly") ro = (v == "true" || v == "1" || v == "yes" || v == "on");
    }

    if (unit < 0) {
        err = "[[board.drive]] needs a `unit`";
        return false;
    }
    if (path.empty()) return true;
    return mount("drive" + std::to_string(unit), path, ro, err);
}

std::vector<Board::SubUnit> HdskBoard::subUnits() const {
    std::vector<SubUnit> out;
    for (int i = 0; i < drives_; ++i) {
        const Drive& d = drive_[(size_t)i];
        if (!d.img) continue;

        SubUnit su;
        su.table = "drive";
        su.fields.push_back({"unit", std::to_string(i), false});
        su.fields.push_back({"mount", d.path, true});
        if (d.img->readOnly()) su.fields.push_back({"readonly", "true", false});
        out.push_back(std::move(su));
    }
    return out;
}

// ---------------------------------------------------------------------------
// SNAPSHOT / RESTORE. The four buffers, the per-unit head positions, and the whole
// command/handshake state travel. The disk IMAGES are host-backed config and do not
// (DESIGN.md 13); nothing is scheduled on the Clock, so there is no deadline to re-arm.
// ---------------------------------------------------------------------------
void HdskBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.raw(&buffers_[0][0], sizeof buffers_);
    for (int c : cyl_) w.u32((uint32_t)(int32_t)c);
    w.u8(pendingLow_);
    w.u16(command_);
    w.u8((uint8_t)phase_);
    w.u32((uint32_t)(int32_t)xferBuf_);
    w.u32((uint32_t)(int32_t)xferPos_);
    w.u32((uint32_t)(int32_t)xferCount_);
    w.u8(ivAddr_);
    w.u8(statusByte_);
    w.u8(status_);
    w.boolean(cready_);
    w.boolean(ack_);
    w.boolean(cdReady_);
    w.boolean(adReady_);
}

void HdskBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    r.raw(&buffers_[0][0], sizeof buffers_);
    for (int& c : cyl_) c = (int)(int32_t)r.u32();
    pendingLow_ = r.u8();
    command_    = r.u16();
    phase_      = (Phase)r.u8();
    xferBuf_    = (int)(int32_t)r.u32();
    xferPos_    = (int)(int32_t)r.u32();
    xferCount_  = (int)(int32_t)r.u32();
    ivAddr_     = r.u8();
    statusByte_ = r.u8();
    status_     = r.u8();
    cready_     = r.boolean();
    ack_        = r.boolean();
    cdReady_    = r.boolean();
    adReady_    = r.boolean();
}

} // namespace altair
