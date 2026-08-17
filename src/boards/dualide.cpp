#include "boards/dualide.h"

#include "core/bus.h"
#include "core/statefile.h"
#include "core/value.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace altair {

DualIdeBoard::DualIdeBoard() { drive_.resize((size_t)kDrives); }

// ---------------------------------------------------------------------------
// The 8255 -> ATA/IDE register engine -- a faithful model of HIDE3.ASM's programmed-I/O
// strobes (IDEwr8D / IDErd8D / the 16-bit sector loops). See dualide.h for the port model.
// ---------------------------------------------------------------------------

MediaFile* DualIdeBoard::curMedia() const {
    if (curDrive_ < 0 || curDrive_ >= (int)drive_.size()) return nullptr;
    return drive_[(size_t)curDrive_].media.get();
}

uint32_t DualIdeBoard::computeLba() const {
    return (uint32_t)regSector_ | ((uint32_t)regCylLSB_ << 8) |
           ((uint32_t)regCylMSB_ << 16) | ((uint32_t)(regShd_ & 0x0F) << 24);
}

// The REGstatus byte the BIOS polls: BSY always 0 (instant controller), RDY set while a card is
// present, DRQ during a transfer phase, ERR from the last command. An empty socket floats 0xFF,
// which is handled in readReg8() -- this is only ever reached with a card present.
uint8_t DualIdeBoard::statusByte() const {
    uint8_t s = kStRdy;
    if (phase_ != Phase::Idle) s |= kStDrq;
    if (err_) s |= kStErr;
    return s;
}

// An OUT to port C. The BIOS drives the whole control byte at once and pulses WR/RD high then
// low, so the engine acts on the RISING edge of each strobe.
void DualIdeBoard::writeCtrl(uint8_t v) {
    uint8_t rising = (uint8_t)(v & ~prevCtrl_);
    prevCtrl_ = v;

    if (rising & kRst) resetTransfer();     // RST line asserted: drop any transfer in flight

    if (!(v & kCs0)) return;                 // CS0 must be low-true-asserted to reach a register
    uint8_t reg = (uint8_t)(v & 0x0F);

    if (rising & kWr) {
        if (reg == rData) pushWriteWord();   // 16-bit data-out word
        else              writeReg8(reg, portAOut_);
    } else if (rising & kRd) {
        if (reg == rData) presentReadWord(); // 16-bit data-in word -> A/B latches
        else              portAIn_ = readReg8(reg);
    }
}

// An 8-bit register write (IDEwr8D). Address/geometry registers just store; REGcmd dispatches.
void DualIdeBoard::writeReg8(uint8_t reg, uint8_t val) {
    switch (reg) {
        case rErr:    regErr_    = val; return;   // feature register (unused)
        case rSecCnt: regSecCnt_ = val; return;
        case rSector: regSector_ = val; return;
        case rCylLSB: regCylLSB_ = val; return;
        case rCylMSB: regCylMSB_ = val; return;
        case rShd:    regShd_    = val; return;
        case rCmdSt:  doCommand(val);   return;
        default:                        return;
    }
}

// An 8-bit register read (IDErd8D). An empty socket floats the whole IDE bus to 0xFF.
uint8_t DualIdeBoard::readReg8(uint8_t reg) {
    if (!curMedia()) return kFloat;
    switch (reg) {
        case rCmdSt:  return statusByte();
        case rErr:    return regErr_;
        case rSecCnt: return regSecCnt_;
        case rSector: return regSector_;
        case rCylLSB: return regCylLSB_;
        case rCylMSB: return regCylMSB_;
        case rShd:    return regShd_;
        default:      return kFloat;
    }
}

// A write to REGcmd. Latch the LBA from the geometry registers and set up the transfer phase.
void DualIdeBoard::doCommand(uint8_t cmd) {
    err_     = false;
    wordIdx_ = 0;
    lba_     = computeLba();
    MediaFile* m = curMedia();

    switch (cmd) {
        case cRead:
            // Load the sector now. A CardImage serves 0xFF for an in-range but never-written
            // sector (readAt true) -- that is not an error; reading past the card's declared
            // size fails, so the BIOS's IDEwaitdrq times out and reports the error.
            if (!m || !m->readAt((uint64_t)lba_ * kSectorSize, buf_, kSectorSize)) {
                err_   = true;
                phase_ = Phase::Idle;   // DRQ never sets -> BIOS aborts the read
                return;
            }
            phase_ = Phase::ReadData;
            return;

        case cWrite:
            if (!m) { err_ = true; phase_ = Phase::Idle; return; }
            phase_ = Phase::WriteData;   // gather 256 words, then commitWrite()
            return;

        case cRecal:
        case cInit:
        case cSpinDown:
        case cSpinUp:
            phase_ = Phase::Idle;        // housekeeping: report ready, no data
            return;

        case cIdentify:
            std::memset(buf_, 0, sizeof buf_);   // identity not modeled -- 256 words of zero
            phase_ = m ? Phase::ReadData : Phase::Idle;
            if (!m) err_ = true;
            return;

        default:
            err_   = true;
            phase_ = Phase::Idle;
            return;
    }
}

// A REGdata WR strobe during a WRITE: buffer the 16-bit word {portB high, portA low}. The 256th
// word commits the sector.
void DualIdeBoard::pushWriteWord() {
    if (phase_ != Phase::WriteData) return;   // stray data write outside a transfer
    if (wordIdx_ < kWords) {
        buf_[wordIdx_ * 2]     = portAOut_;   // low byte first (HIDE3.ASM WRSEC1)
        buf_[wordIdx_ * 2 + 1] = portBOut_;   // then high byte
    }
    if (++wordIdx_ >= kWords) {
        commitWrite();
        phase_ = Phase::Idle;
    }
}

// A REGdata RD strobe during a READ: present the next 16-bit word on the A/B input latches.
void DualIdeBoard::presentReadWord() {
    if (phase_ != Phase::ReadData) { portAIn_ = kFloat; portBIn_ = kFloat; return; }
    if (wordIdx_ < kWords) {
        portAIn_ = buf_[wordIdx_ * 2];        // low byte -> A
        portBIn_ = buf_[wordIdx_ * 2 + 1];    // high byte -> B
    }
    if (++wordIdx_ >= kWords) phase_ = Phase::Idle;
}

// The 256 words are in; commit the sector and sync it (per-sector durability, as the SD half
// does). A write-protected or over-the-end write fails with ERR and is reported via drainLog().
void DualIdeBoard::commitWrite() {
    MediaFile* m = curMedia();
    if (!m) { err_ = true; return; }
    if (m->readOnly()) {
        say("write to a write-protected IDE card ignored");
        err_ = true;
        return;
    }
    if (m->writeAt((uint64_t)lba_ * kSectorSize, buf_, kSectorSize)) {
        m->sync();
        return;
    }
    say("write past the end of the IDE card ignored (LBA " + std::to_string(lba_) + ")");
    err_ = true;
}

void DualIdeBoard::resetTransfer() {
    phase_   = Phase::Idle;
    wordIdx_ = 0;
    err_     = false;
}

void DualIdeBoard::resetEngine() {
    resetTransfer();
    curDrive_  = 0;
    portAOut_  = portBOut_ = portAIn_ = portBIn_ = 0;
    ctrlCfg_   = 0;
    prevCtrl_  = 0;
    regErr_    = regSecCnt_ = regSector_ = regCylLSB_ = regCylMSB_ = 0;
    regShd_    = 0xE0;
    lba_       = 0;
    std::memset(buf_, 0, sizeof buf_);
}

// ---------------------------------------------------------------------------
// The bus. Five contiguous I/O ports, no memory window (the board carries no boot PROM -- CP/M
// is loaded by the CPU board's MASTER monitor; reference section 4).
// ---------------------------------------------------------------------------
bool DualIdeBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type == Cycle::IoRead || c.type == Cycle::IoWrite) {
        uint8_t p = c.port();
        return p >= (uint8_t)port_ && p <= (uint8_t)(port_ + 4);
    }
    return false;
}

uint8_t DualIdeBoard::read(const BusCycle& c) {
    if (c.type != Cycle::IoRead) return 0xFF;
    switch ((uint8_t)(c.port() - (uint8_t)port_)) {
        case 0:  return portAIn_;    // portA -- data low / register read-back
        case 1:  return portBIn_;    // portB -- data high
        case 2:  return prevCtrl_;   // portC is an output; 8255 read-back of the last value
        case 3:  return ctrlCfg_;    // 8255 mode config read-back
        case 4:  return (uint8_t)curDrive_;
        default: return 0xFF;
    }
}

void DualIdeBoard::write(const BusCycle& c) {
    if (c.type != Cycle::IoWrite) return;
    switch ((uint8_t)(c.port() - (uint8_t)port_)) {
        case 0:  portAOut_ = c.data; return;           // portA
        case 1:  portBOut_ = c.data; return;           // portB
        case 2:  writeCtrl(c.data);  return;           // portC -- the strobe engine
        case 3:  ctrlCfg_  = c.data; return;           // portCtrl -- 8255 mode (inert)
        case 4:  curDrive_ = c.data & 0x01; return;    // drive select: 0 -> A:, 1 -> B:
        default:                     return;
    }
}

// ---------------------------------------------------------------------------
// Lifecycle.
// ---------------------------------------------------------------------------
void DualIdeBoard::reset(Reset) { resetEngine(); }

void DualIdeBoard::power() { resetEngine(); }

void DualIdeBoard::configChanged() { decodeChanged(); }  // `port` moved the decode

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> DualIdeBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Base address. The board decodes five 8255 ports: BASE..BASE+4 (A,B,C,cfg,drive)";
        x.kind  = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xFB;  // five ports must fit under 0xFF
        x.get   = [this] { return Value::ofInt(port_); };
        x.set   = [this](const Value& v, std::string&) {
            port_ = (uint16_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<MapEntry> DualIdeBoard::ioMap() const {
    uint8_t b = (uint8_t)port_;
    return {
        {(uint32_t)(b + 0), (uint32_t)(b + 0), "read/write", "8255 port A (IDE data low / reg read-back)"},
        {(uint32_t)(b + 1), (uint32_t)(b + 1), "read/write", "8255 port B (IDE data high)"},
        {(uint32_t)(b + 2), (uint32_t)(b + 2), "read/write", "8255 port C (IDE control lines)"},
        {(uint32_t)(b + 3), (uint32_t)(b + 3), "read/write", "8255 mode config"},
        {(uint32_t)(b + 4), (uint32_t)(b + 4), "read/write", "drive select (bit0: 0=A:, 1=B:)"},
    };
}

// ---------------------------------------------------------------------------
// Units, MOUNT, UNMOUNT, and the [[board.drive]] sub-unit table (the two CF sockets).
// ---------------------------------------------------------------------------
static int dualideDriveIndex(const std::string& unit, int count) {
    if (unit.rfind("drive", 0) != 0) return -1;
    const std::string n = unit.substr(5);
    if (n.empty()) return -1;
    for (char ch : n)
        if (ch < '0' || ch > '9') return -1;
    int i = std::stoi(n);
    return (i >= 0 && i < count) ? i : -1;
}

std::vector<UnitDef> DualIdeBoard::units() const {
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

bool DualIdeBoard::mount(const std::string& unit, const std::string& path, bool ro, std::string& err) {
    int i = dualideDriveIndex(unit, kDrives);
    if (i < 0) {
        err = "no unit `" + unit + "` on " + id + " (it has drive0.." +
              std::to_string(kDrives - 1) + ")";
        return false;
    }

    // The card is addressed directly by byte offset, so the medium is held raw -- no DiskImage
    // geometry probe. A CardImage card owns its own geometry; any MediaFile works the same way.
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

bool DualIdeBoard::unmount(const std::string& unit, std::string& err) {
    int i = dualideDriveIndex(unit, kDrives);
    if (i < 0) { err = "no unit `" + unit + "` on " + id; return false; }

    Drive& d = drive_[(size_t)i];
    if (!d.media) { err = id + ":" + unit + " is empty"; return false; }

    d.media->sync();
    d.media.reset();
    d.path.clear();
    return true;
}

std::vector<std::string> DualIdeBoard::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    for (auto& s : out) s = id + ":" + s;
    return out;
}

std::vector<Property> DualIdeBoard::subUnitProperties(const std::string& table) const {
    if (table != "drive") return {};
    std::vector<Property> p;
    {
        Property x;
        x.name  = "unit";
        x.help  = "Which CF socket (0 = drive A:, 1 = drive B:)";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.min   = 0;
        x.max   = kDrives - 1;
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "mount";
        x.help = "The card image (a .img with a sibling .geo geometry) to put in it. Relative to THIS FILE.";
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

bool DualIdeBoard::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
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

std::vector<Board::SubUnit> DualIdeBoard::subUnits() const {
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
// SNAPSHOT / RESTORE (DESIGN.md 13). The engine state and the sector buffer travel; the mounted
// media are host-backed (reloaded from `mount` in the config on restore) and the port strap is
// config. Nothing is on the Clock.
// ---------------------------------------------------------------------------
void DualIdeBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u8(portAOut_);
    w.u8(portBOut_);
    w.u8(portAIn_);
    w.u8(portBIn_);
    w.u8(ctrlCfg_);
    w.u8(prevCtrl_);
    w.u8(regErr_);
    w.u8(regSecCnt_);
    w.u8(regSector_);
    w.u8(regCylLSB_);
    w.u8(regCylMSB_);
    w.u8(regShd_);
    w.u8((uint8_t)phase_);
    w.u32(wordIdx_);
    w.u32(lba_);
    w.u8(err_ ? 1 : 0);
    w.raw(buf_, sizeof buf_);
    w.u32((uint32_t)(int32_t)curDrive_);
}

void DualIdeBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    portAOut_  = r.u8();
    portBOut_  = r.u8();
    portAIn_   = r.u8();
    portBIn_   = r.u8();
    ctrlCfg_   = r.u8();
    prevCtrl_  = r.u8();
    regErr_    = r.u8();
    regSecCnt_ = r.u8();
    regSector_ = r.u8();
    regCylLSB_ = r.u8();
    regCylMSB_ = r.u8();
    regShd_    = r.u8();
    phase_     = (Phase)r.u8();
    wordIdx_   = r.u32();
    lba_       = r.u32();
    err_       = r.u8() != 0;
    r.raw(buf_, sizeof buf_);
    curDrive_  = (int)(int32_t)r.u32();
}

} // namespace altair
