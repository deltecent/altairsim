#include "boards/icom-fd3712.h"

#include "core/bus.h"
#include "core/roms.h"
#include "core/statefile.h"
#include "core/value.h"
#include "host/media.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace altair {

IcomFdBoard::IcomFdBoard() { drive_.resize((size_t)drives_); }

// ---------------------------------------------------------------------------
// The command/handshake engine (reference sections 2-5). See the header for the
// unified read/write model that serves both PROM generations.
// ---------------------------------------------------------------------------

// OUT C0 -- the command word. Bit 0 is the strobe (the value executes), bit 6 selects
// what a following IN C0 returns. The two cWRTBUF forms (0x30 stream, 0x31 strobe) are
// the ONLY commands that leave the write buffer "armed"; every other command clears it,
// which is exactly the trailing `xra a / out C0` the 3712 loops issue between bytes.
void IcomFdBoard::outCmd(uint8_t v) {
    lastCmd_ = v;
    switch (v) {
        case 0x00: break;                                // cSTATUS: examine-status mode, no action
        case 0x03: doRead();  break;                     // cREAD
        case 0x07: doRead();  break;                     // cRDCRC: re-read to validate CRC
        case 0x05: doWrite(); break;                     // cWRITE
        case 0x0F: doWrite(); break;                     // cWRITE (deleted address mark -- see doWrite)
        case 0x09: break;                                // cSEEK: synchronous, head is already there
        case 0x0B: crcErr_ = false; ddam_ = false; break;  // cCLRERR
        case 0x0D: track_ = 0; break;                    // cRESTOR: seek to track 0
        case 0x11: track_ = lastDataOut_; break;         // cSETTRK
        case 0x15: config_ = lastDataOut_; break;        // cLDCFG: density (bit4) + format-mode (bit5)
        case 0x21:                                       // cDRVSEC: unit 7:6, sector in the low bits
            unit_   = (lastDataOut_ >> 6) & 0x03;
            sector_ = lastDataOut_ & 0x3F;
            break;
        case 0x30: break;                                // cWRTBUF (3812): stream mode, no immediate push
        case 0x31: pushWrite(lastDataOut_); break;       // cWRTBUF (3712): push the latched C1 byte now
        case 0x40: break;                                // cRDBUF: enable buffer output (no pointer reset)
        case 0x41: break;                                // cSHIFT: same -- IN C0 advances the pointer
        case 0x81: doClear(); break;                     // cCLEAR (reference "Clear")
        default:   break;                                // unassigned opcode -- a harmless no-op
    }
    // The write buffer stays armed only across a cWRTBUF (either form); the very next command
    // -- the examine-status the 3712 issues between bytes, or the cWRITE that consumes it --
    // disarms it, so a stray OUT C1 never pushes.
    wbufMode_ = (v == 0x30 || v == 0x31);
}

// OUT C1 -- the data-out byte a following command consumes (track, unit+sector, config).
// On the FD3812 the one-shot pulses the command strobe on every OUT C1, so while a cWRTBUF
// is armed each write here also pushes the byte into the write buffer (reference section 8).
void IcomFdBoard::outData(uint8_t v) {
    lastDataOut_ = v;
    if (wbufMode_) pushWrite(v);
}

// IN C0 -- read-buffer byte when the last OUT C0 selected it (bit 6 set), otherwise status.
// The read advances the buffer pointer, which is what makes one IN-per-byte serve both the
// 3712 (explicit cSHIFT between bytes) and the 3812 (back-to-back INs).
uint8_t IcomFdBoard::inC0() {
    if (lastCmd_ & 0x40) {
        uint8_t b = (readPtr_ < readLen_) ? readBuf_[readPtr_] : 0xFF;
        if (readPtr_ < readLen_) ++readPtr_;
        return b;
    }
    return statusByte();
}

// The status byte (reference section 4). BUSY reads 0 -- the emulated controller completes
// every command synchronously, so a PROM that polls "wait for not busy" falls straight through.
uint8_t IcomFdBoard::statusByte() const {
    uint8_t s = 0;
    if (crcErr_)                              s |= 0x08;  // bit3 CRC error
    if (const DiskImage* img = currentImg()) {
        if (img->readOnly())                  s |= 0x10;  // bit4 write protected
    } else {
        s |= 0x20;                                        // bit5 drive not ready (empty unit)
    }
    if (ddam_)                                s |= 0x80;  // bit7 deleted address mark
    return s;
}

// cREAD / cRDCRC: pull the whole physical sector into the read buffer and rewind the pointer.
// The sector's real length (128 on an SD track, 256 on a DD track) comes back in n, and that
// is exactly how many bytes the pointer will hand out.
void IcomFdBoard::doRead() {
    readPtr_ = 0;
    readLen_ = 0;
    crcErr_  = false;
    ddam_    = false;
    DiskImage* img = currentImg();
    if (!img) return;  // drive-fail shows through statusByte bit5
    size_t n = kBufBytes;
    if (img->readSector(track_, 0, sector_, readBuf_, &n))
        readLen_ = n;
    else
        crcErr_ = true;  // a slot the medium does not have -> CRC/read error
}

// cWRITE: commit the write buffer to the addressed physical sector. The guest has already
// streamed exactly one sector's worth of bytes (128 or 256) before issuing this; writeSector
// moves the sector's geometry length and reports it in n. The buffer rewinds afterward, the
// recirculating-buffer behavior the next sector's fill relies on.
void IcomFdBoard::doWrite() {
    crcErr_ = false;
    ddam_   = false;
    DiskImage* img = currentImg();
    if (!img) { writePtr_ = 0; return; }         // drive-fail -> statusByte bit5
    if (img->readOnly()) { writePtr_ = 0; return; }  // write-protect -> statusByte bit4; drop the write
    size_t n = kBufBytes;
    if (img->writeSector(track_, 0, sector_, writeBuf_, &n))
        img->sync();
    else
        crcErr_ = true;
    writePtr_ = 0;
}

// Push one byte into the write buffer, clamped at a whole DD sector. The guest never streams
// past the sector, so the clamp is a backstop, not a path.
void IcomFdBoard::pushWrite(uint8_t b) {
    if (writePtr_ < kBufBytes) writeBuf_[writePtr_++] = b;
}

// cCLEAR (command 0x81, reference section 3 "Clear"): halt any operation, clear BUSY, pulse
// DONE, unload the head, and clear the CRC / deleted-data-mark status latches. It is NOT a
// controller reset: on real hardware the read/write "shift register" buffers keep their pointers
// and the track/unit/sector/config registers are untouched, so a partly loaded write buffer
// survives a Clear (a bug found in the FDC+ and AltairZ80 FD3712 -- do not reset writePtr_ here).
void IcomFdBoard::doClear() {
    crcErr_ = false;
    ddam_   = false;
}

// A true controller reset: power-on and CPU RESET only (never the Clear command). Buffers,
// pointers and address registers all return to their power-up state.
void IcomFdBoard::resetController() {
    lastCmd_  = 0;
    readPtr_  = 0;
    readLen_  = 0;
    writePtr_ = 0;
    wbufMode_ = false;
    crcErr_   = false;
    ddam_     = false;
    track_    = 0;
    unit_     = 0;
    sector_   = 1;
    config_   = 0;
}

DiskImage* IcomFdBoard::currentImg() const {
    if (unit_ < 0 || unit_ >= (int)drive_.size()) return nullptr;
    return drive_[(size_t)unit_].img.get();
}

// ---------------------------------------------------------------------------
// The bus. Two I/O ports (C0/C1) plus the boot PROM and 6810 scratch-RAM windows, both
// above the 48K main RAM (nothing else sits there, so no PHANTOM* is needed -- the plain
// owned-decode idiom, like the Cromemco RDOS PROM).
// ---------------------------------------------------------------------------
bool IcomFdBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type == Cycle::IoRead || c.type == Cycle::IoWrite) {
        uint8_t p = c.port();
        return p == (uint8_t)port_ || p == (uint8_t)(port_ + 1);
    }
    if (c.type == Cycle::MemRead) return inRom(c.addr) || inRam(c.addr);
    if (c.type == Cycle::MemWrite) return inRam(c.addr);  // the 6810 is read/write; the PROM ignores writes
    return false;
}

uint8_t IcomFdBoard::read(const BusCycle& c) {
    if (c.type == Cycle::MemRead) {
        if (inRom(c.addr)) return rom_[c.addr - romBase_];
        if (inRam(c.addr)) return ram_[c.addr - ramBase_];
        return 0xFF;
    }
    // IoRead: DATAIN is C0; C1 is output-only, so an IN there floats.
    if (c.port() == (uint8_t)port_) return inC0();
    return 0xFF;
}

void IcomFdBoard::write(const BusCycle& c) {
    if (c.type == Cycle::MemWrite) {
        if (inRam(c.addr)) ram_[c.addr - ramBase_] = c.data;
        return;  // a MemWrite into the PROM window is ignored (an EPROM socket)
    }
    if (c.type != Cycle::IoWrite) return;
    if (c.port() == (uint8_t)port_)            outCmd(c.data);
    else if (c.port() == (uint8_t)(port_ + 1)) outData(c.data);
}

// LOOK WITHOUT TOUCHING: DISASM/TRACE over the PROM and scratch RAM. The I/O ports are NOT
// peekable -- an IN C0 advances the read buffer -- so they are left to float 0xFF.
bool IcomFdBoard::peek(uint16_t addr, uint8_t& out) const {
    if (inRom(addr)) { out = rom_[addr - romBase_]; return true; }
    if (inRam(addr)) { out = ram_[addr - ramBase_]; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Lifecycle. Power reloads the PROM and clears the scratch RAM; a CPU reset re-homes the
// controller but leaves the 6810 (static RAM) intact, exactly as the hardware does.
// ---------------------------------------------------------------------------
void IcomFdBoard::reset(Reset) { resetController(); }

void IcomFdBoard::power() {
    loadRom();
    std::memset(ram_, 0, sizeof ram_);
    resetController();
}

void IcomFdBoard::configChanged() {
    // `rom` changed the window base/contents, `port` moved the decode.
    loadRom();
    decodeChanged();
}

// ---------------------------------------------------------------------------
// The boot PROM. `builtin:icom-fd3712-cpm` / `-fdos` / `icom-fd3812-cpm` travel the same
// Intel HEX parser as a memory card's ROM region. The window base is DERIVED from the decoded
// image (the CP/M PROMs place themselves at F000, FDOS at C000), so nothing here is hardcoded.
// The 6810 scratch RAM sits one kilobyte above the PROM base (F400 / C400).
// ---------------------------------------------------------------------------
void IcomFdBoard::loadRom() {
    rom_.assign((size_t)kRomSize, (uint8_t)0xFF);
    romLoaded_ = false;

    // `builtin:<name>` is a SCHEME, not a path (core/paths.cpp): strip it exactly as the
    // memory card does before the registry lookup.
    std::string name = romName_;
    if (name.rfind("builtin:", 0) == 0) name = name.substr(8);

    const BuiltinRom* rom = findRom(name);
    if (!rom) {
        say("built-in ROM '" + romName_ + "' is missing. SHOW ROMS lists them.");
        return;
    }
    Image       img;
    std::string err;
    if (!decodeRom(*rom, 0, img, err)) {
        say(romName_ + ": " + err);
        return;
    }
    if (img.empty()) {
        say(romName_ + ": decoded to no bytes");
        return;
    }

    romBase_ = img.lo() & 0xFF00u;               // page-align the window down to the PROM base
    ramBase_ = romBase_ + kRamOffset;
    for (const auto& [a, b] : img.bytes)
        if (a >= romBase_ && a < romBase_ + kRomSize)
            rom_[a - romBase_] = b;
    romLoaded_ = true;
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> IcomFdBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Base address. The board decodes two ports: BASE (command/status) and BASE+1 (data)";
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
    {
        Property x;
        x.name = "rom";
        x.help = "The boot PROM this interface board carries: builtin:icom-fd3712-cpm (CP/M 2.2, "
                 "single density, F000), builtin:icom-fd3712-fdos (FDOS, C000), or "
                 "builtin:icom-fd3812-cpm (CP/M 2.2, double density, F000). The window base and the "
                 "6810 scratch RAM follow the image";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(romName_); };
        x.set  = [this](const Value& v, std::string&) {
            romName_ = v.s();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "drives";
        x.help  = "Drives on the controller (unit 0..3, selected by cDRVSEC bits 7:6)";
        x.kind  = Kind::Int;
        x.radix = 10;  // a count, never on the wire -> decimal
        x.min   = 1;
        x.max   = kMaxDrives;
        x.get   = [this] { return Value::ofInt(drives_); };
        x.set   = [this](const Value& v, std::string& err) {
            int n = (int)v.i();
            for (int i = n; i < (int)drive_.size(); ++i)
                if (drive_[(size_t)i].img) {
                    err = "drive" + std::to_string(i) + " still has a disk in it";
                    return false;
                }
            drives_ = n;
            drive_.resize((size_t)n);
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<MapEntry> IcomFdBoard::ioMap() const {
    uint8_t b = (uint8_t)port_;
    return {
        {(uint32_t)(b + 0), (uint32_t)(b + 0), "read/write", "CMDOUT (OUT) / DATAIN status or read buffer (IN)"},
        {(uint32_t)(b + 1), (uint32_t)(b + 1), "write",      "DATAOUT: track / unit+sector / write buffer / config"},
    };
}

std::vector<MapEntry> IcomFdBoard::memMap() const {
    if (!romLoaded_) return {};
    return {
        {romBase_, romBase_ + kRomSize - 1, "read",       "iCOM boot PROM"},
        {ramBase_, ramBase_ + kRamSize - 1, "read/write", "6810 scratch RAM (BIOS vectors + variables)"},
    };
}

// ---------------------------------------------------------------------------
// Units, MOUNT, UNMOUNT, and the [[board.drive]] sub-unit table (the 88-HDSK shape).
// ---------------------------------------------------------------------------
static int icomDriveIndex(const std::string& unit, int count) {
    if (unit.rfind("drive", 0) != 0) return -1;
    const std::string n = unit.substr(5);
    if (n.empty()) return -1;
    for (char ch : n)
        if (ch < '0' || ch > '9') return -1;
    int i = std::stoi(n);
    return (i >= 0 && i < count) ? i : -1;
}

std::vector<UnitDef> IcomFdBoard::units() const {
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

bool IcomFdBoard::mount(const std::string& unit, const std::string& path, bool ro, std::string& err) {
    int i = icomDriveIndex(unit, drives_);
    if (i < 0) {
        err = "no unit `" + unit + "` on " + id + " (it has drive0.." +
              std::to_string(drives_ - 1) + ")";
        return false;
    }

    auto media = openMedia(resolvePath(path), ro, err);
    if (!media) { err += pathNote(path); return false; }

    // Probe into a FRESH image so a size mismatch does not half-replace the old disk. The
    // byte count is the geometry: an SD image is one 128-byte range, a DD image keeps track 0
    // single-density (128) with the rest double (256), which is the FD3812's mandatory layout.
    auto img = std::make_unique<DiskImage>(std::move(media));
    if (sizeMatches(img->size(), kSdBytes)) {
        img->init(kTracks, 1, /*interleaved=*/false);
        img->initFormat(0, kTracks - 1, 0, 0, Density::SD, kSectors, 128, 1);
    } else if (sizeMatches(img->size(), kDdBytes)) {
        img->init(kTracks, 1, /*interleaved=*/false);
        img->initFormat(0, 0, 0, 0, Density::SD, kSectors, 128, 1);
        img->initFormat(1, kTracks - 1, 0, 0, Density::DD, kSectors, 256, 1);
    } else {
        err = std::to_string(img->size()) + " bytes is not an iCOM disk (expected " +
              std::to_string(kSdBytes) + " single density or " + std::to_string(kDdBytes) +
              " double density)";
        return false;
    }

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

bool IcomFdBoard::unmount(const std::string& unit, std::string& err) {
    int i = icomDriveIndex(unit, drives_);
    if (i < 0) { err = "no unit `" + unit + "` on " + id; return false; }

    Drive& d = drive_[(size_t)i];
    if (!d.img) { err = id + ":" + unit + " is empty"; return false; }

    d.img->sync();
    d.img.reset();
    d.path.clear();
    return true;
}

std::vector<std::string> IcomFdBoard::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    for (auto& s : out) s = id + ":" + s;
    return out;
}

std::vector<Property> IcomFdBoard::subUnitProperties(const std::string& table) const {
    if (table != "drive") return {};
    std::vector<Property> p;
    {
        Property x;
        x.name  = "unit";
        x.help  = "Which drive (0..3)";
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

bool IcomFdBoard::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
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
            Value bv;
            std::string e;
            if (parseValue(v, Kind::Bool, bv, e)) ro = bv.b();
        }
    }

    if (unit < 0) { err = "[[board.drive]] needs a `unit`"; return false; }
    if (unit >= drives_) {
        err = "[[board.drive]] unit " + std::to_string(unit) + " but the card has " +
              std::to_string(drives_) + " drives";
        return false;
    }
    if (path.empty()) return true;
    return mount("drive" + std::to_string(unit), path, ro, err);
}

std::vector<Board::SubUnit> IcomFdBoard::subUnits() const {
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
// SNAPSHOT / RESTORE (DESIGN.md 13). The controller's registers and both sector buffers
// travel, and so does the 6810 scratch RAM -- it holds the running BIOS's vectors and
// variables, which is genuine runtime state. The PROM is host-backed config (reloaded from
// `rom` in deserialize); the disk IMAGES are host-backed too. Nothing is on the Clock.
// ---------------------------------------------------------------------------
void IcomFdBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.raw(ram_, sizeof ram_);
    w.u8(lastCmd_);
    w.u8(lastDataOut_);
    w.u32((uint32_t)(int32_t)track_);
    w.u32((uint32_t)(int32_t)unit_);
    w.u32((uint32_t)(int32_t)sector_);
    w.u8(config_);
    w.raw(readBuf_, sizeof readBuf_);
    w.u32((uint32_t)readPtr_);
    w.u32((uint32_t)readLen_);
    w.raw(writeBuf_, sizeof writeBuf_);
    w.u32((uint32_t)writePtr_);
    w.boolean(wbufMode_);
    w.boolean(crcErr_);
    w.boolean(ddam_);
}

void IcomFdBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    r.raw(ram_, sizeof ram_);
    lastCmd_     = r.u8();
    lastDataOut_ = r.u8();
    track_       = (int)(int32_t)r.u32();
    unit_        = (int)(int32_t)r.u32();
    sector_      = (int)(int32_t)r.u32();
    config_      = r.u8();
    r.raw(readBuf_, sizeof readBuf_);
    readPtr_     = r.u32();
    readLen_     = r.u32();
    r.raw(writeBuf_, sizeof writeBuf_);
    writePtr_    = r.u32();
    wbufMode_    = r.boolean();
    crcErr_      = r.boolean();
    ddam_        = r.boolean();
}

} // namespace altair
