#include "boards/tarbell.h"

#include "core/bus.h"
#include "core/clock.h"
#include "core/roms.h"
#include "core/statefile.h"
#include "core/value.h"
#include "host/media.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace altair {

// The raw bytes one revolution of an 8" single-density track holds: 250,000 bits/s FM at
// 360 RPM (166.67 ms/rev) is 250000/8 * 0.16667 = ~5208 bytes. This is the wait-synced Write
// Track budget (floppy-drive.h) and the length FORMAT.COM pads its ~4882 structured bytes out
// to. Must be >= what any Tarbell format program streams before its trailing gap. See
// reference/Tarbell_Floppy_Disk_Interface_Manual.md ("250,000 bits per second").
static constexpr int kSdTrackBytes = 5208;

// An unclocked card is a chip with no crystal: it cannot time a seek, so it reads
// dead rather than dereferencing a null Clock. (Same idiom as the VersaFloppy.)
static Clock& deadCard() {
    static Clock stopped;
    return stopped;
}

Clock& TarbellBoard::clk() const { return clock_ ? *clock_ : deadCard(); }

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
TarbellBoard::TarbellBoard() {
    drive_.resize((size_t)drives_);
    buildChip();
}

TarbellBoard::~TarbellBoard() {
    // The Clock is holding a lambda with `this` in it; a card can be pulled from a
    // running machine, and a deadline firing into a freed board is a use-after-free.
    if (clock_) clock_->cancel(wake_);
}

// THE PART IS THE GENERATION. The single-density card has an FD1771; the rest of the
// family (register file, command set) is shared in Wd17xx. Always wait-synced: the
// Tarbell's data port stalls the CPU on the wait-state generator rather than exposing
// a byte clock (reference §4), so every command completes on the access that would
// have waited (see wd17xx.h). DD overrides this to build a Wd1791.
void TarbellBoard::buildChip() {
    chip_ = std::make_unique<Wd1771>("fdc");
    chip_->setWaitSynced(true);
    if (clock_) chip_->powerOn(*clock_);
    applySelection();
}

// Point the chip at the selected drive and re-apply side and media data rate. Unlike
// the VersaFloppy's 63H latch, the Tarbell's drive select is PLAIN (no inversion): the
// SD function decoder and the DD bitmap latch both hand us a straight binary drive
// number (writeControl below).
void TarbellBoard::applySelection() {
    FloppyDrive* fd = nullptr;
    if (sel_ >= 0 && sel_ < (int)drive_.size()) {
        drive_[(size_t)sel_].drv.setSide(side_);
        fd = &drive_[(size_t)sel_].drv;
    }
    if (chip_) {
        chip_->attach(fd);
        chip_->setSide(side_);
        chip_->dataRateBits = dataRate_;
    }
}

// ---------------------------------------------------------------------------
// The bus. Two arms: the 8-port I/O block, and the boot PROM's memory reads while it
// is shadowing low RAM.
// ---------------------------------------------------------------------------
bool TarbellBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;

    if (c.type == Cycle::IoRead || c.type == Cycle::IoWrite) {
        uint8_t p = c.port();
        // `p - port_`, NOT `p < port_ + 8`: at the top of the range (port = F8) the
        // latter truncates F8+8 to 0 and the board decodes nothing. Since p >= port_,
        // the 8-bit difference is the true offset and the window test never overflows.
        return p >= port_ && (uint8_t)(p - port_) < 8;
    }

    // The boot PROM answers memory reads while it shadows. It does NOT decode a
    // MemWrite: assertsPhantom keeps the RAM off the READ but not the write, so the
    // bootstrap's sector lands in the RAM under the shadow.
    if (c.type == Cycle::MemRead) return assertsPhantom(c);
    return false;
}

uint8_t TarbellBoard::read(const BusCycle& c) {
    if (c.type == Cycle::MemRead) return prom_[c.addr & 0x1F];  // only reached while shadowing

    Clock&  k   = clk();
    uint8_t off = (uint8_t)(c.port() - port_);
    uint8_t v   = 0xFF;
    switch (off) {
        case 0: v = chip_->readStatus(k);   break;  // F8 status
        case 1: v = chip_->readTrackReg();  break;  // F9 track
        case 2: v = chip_->readSectorReg(); break;  // FA sector
        case 3: v = chip_->readData(k);     break;  // FB data (wait-synced)
        case 4:
            // FC WAIT: bit7 = DRQ (a byte is ready), or 0 = INTRQ (command done). The
            // PROM's RLOOP polls this: sign set -> read a byte, sign clear -> finished.
            // The low seven bits float; return them high (period code tests only bit7).
            chip_->poll(k);
            v = (uint8_t)((chip_->drq() ? 0x80 : 0x00) | 0x7F);
            break;
        default: v = readExtra(off); break;  // FD (DD only) / FE-FF
    }
    refresh();
    return v;
}

void TarbellBoard::write(const BusCycle& c) {
    if (c.type != Cycle::IoWrite) return;  // a MemWrite in the shadow falls through to RAM

    Clock&  k   = clk();
    uint8_t off = (uint8_t)(c.port() - port_);
    switch (off) {
        case 0: chip_->writeCommand(c.data, k);  break;  // F8 command
        case 1: chip_->writeTrackReg(c.data);    break;  // F9 track
        case 2: chip_->writeSectorReg(c.data);   break;  // FA sector
        case 3: chip_->writeData(c.data, k);     break;  // FB data (wait-synced)
        case 4: writeControl(c.data);            break;  // FC control
        default: writeExtra(off, c.data);        break;  // FD (DD only) / FE-FF
    }
    refresh();
}

// ---------------------------------------------------------------------------
// OUT FC -- the control port. THE SINGLE-DENSITY CARD IS A FUNCTION DECODER (74LS138),
// not a bitmap: D2:D0 select one function. `010` strobes the U40 drive latch from D4-D7;
// `001` is a fast step-out pulse; `000` pulses a drive RST* line (nothing to model).
//
// THE DRIVE NUMBER IS LOADED COMPLEMENTED. The standard Tarbell SELECT idiom -- and the
// CBIOS on both tracked disks -- does `CMA` before `OUT FC` (the byte sequence
// 2F 87 87 87 87 F6 02 D3 FC = CMA; ADD A x4; ORI 02; OUT FC), so the latch's D5:D4 hold
// ~drive: drive 0 is written as 0xF2 (D5:D4 = 11), drive 1 as 0xE2, and so on. The disk
// is the ground truth here (DESIGN.md 0.1); the interface manual's simplified SELECT
// example omits the complement. The default strap selects drive 0, so the boot PROM and
// the cold loader (which never write FC) run against drive 0.
// ---------------------------------------------------------------------------
void TarbellBoard::writeControl(uint8_t v) {
    switch (v & 0x07) {
        case 0x02: sel_ = (~(v >> 4)) & 0x03; applySelection(); break;  // U40 latch: ~drive
        case 0x01: /* fast step-out pulse -- nothing to model */        break;
        default:                                                        break;
    }
}

// ---------------------------------------------------------------------------
// Interrupts. The standard Tarbell software polls the WAIT port, and both tracked
// disks boot polled; the strap exists for completeness. Gate on the jumper.
// ---------------------------------------------------------------------------
bool TarbellBoard::assertsInt() const {
    return chip_ && irq_ == IrqJumper::Int && chip_->intrq();
}

uint8_t TarbellBoard::assertsVi() const {
    if (!chip_ || !chip_->intrq()) return 0;
    return viBit(irq_);
}

// ---------------------------------------------------------------------------
// PHANTOM* and the boot-PROM flip-flop.
//
// PHANTOM* is HELD while the PROM is armed, on reads AND writes -- the read/write
// distinction lives on the honoring RAM card (`honors_phantom = read`), which is why
// the bootstrap's sector lands in the RAM under the shadow. The RELEASE is
// combinational and off A5: the very cycle that reads an address with A5 high is
// already un-shadowed, so the PROM's `JZ 07DH` (0x7D has A5 set) drops straight into
// the loaded loader. `bootstrap = false` (the DIP off) disables the whole half.
// ---------------------------------------------------------------------------
bool TarbellBoard::assertsPhantom(const BusCycle& c) const {
    if (!armed_ || !bootstrap_) return false;
    if (c.type != Cycle::MemRead && c.type != Cycle::MemWrite) return false;
    if (c.type == Cycle::MemRead && (c.addr & 0x0020)) return false;  // A5 high -> released now
    return true;
}

// The LATCHED half: the first memory read with A5 high releases the shadow forever
// (until POC* re-arms it). The combinational half is in assertsPhantom() above; only
// the latch is news the backplane needs.
void TarbellBoard::snoop(const BusCycle& c) {
    if (armed_ && c.type == Cycle::MemRead && (c.addr & 0x0020)) {
        armed_ = false;
        decodeChanged();
    }
}

// ---------------------------------------------------------------------------
// The card's own clock discipline (the VersaFloppy idiom): advance the chip, re-drive
// the interrupt wire, re-arm the one deadline. Under wait-synced operation the chip
// has no autonomous edge, so no timer is armed -- everything happens on an access.
// ---------------------------------------------------------------------------
void TarbellBoard::refresh() {
    if (!clock_) return;
    chip_->poll(*clock_);
    intChanged();
    clock_->cancel(wake_);
    wake_ = Clock::kNone;
    uint64_t e = chip_->nextEdge(*clock_);
    if (e) wake_ = clock_->at(e, [this] { refresh(); });
}

void TarbellBoard::reset(Reset r) {
    // POC*/RESET* re-arms the boot PROM (the machine comes up with it shadowing 0000).
    // Independent of the clock -- do it first.
    if (bootstrap_ && !armed_) {
        armed_ = true;
        decodeChanged();
    }
    if (!clock_) return;
    if (r == Reset::PowerOn) chip_->powerOn(*clock_);
    // S-100 RESET* reaches the FD1771's MR pin, which auto-executes a Restore -- so the
    // head homes with no software help. The PROM's first `IN FC` ("WAIT FOR HOME")
    // relies on exactly this.
    chip_->masterReset(*clock_);
    refresh();
}

void TarbellBoard::power() {
    loadProm();
    reset(Reset::PowerOn);
}

void TarbellBoard::pump() { refresh(); }

void TarbellBoard::configChanged() {
    decodeChanged();  // `port` moved the card; `bootstrap` changed the shadow decode
    refresh();
}

// ---------------------------------------------------------------------------
// The boot PROM. `builtin:tarbell-sd` travels the same Intel HEX parser as a memory
// card's ROM region -- the same loader the SBC and Turnkey use. The 32 bytes land at
// 0000; anything the decode places elsewhere is ignored (there is nothing else).
// ---------------------------------------------------------------------------
void TarbellBoard::loadProm() {
    std::fill(std::begin(prom_), std::end(prom_), (uint8_t)0xFF);
    const BuiltinRom* rom = findRom("tarbell-sd");
    if (!rom) {
        log_.push_back(id + ": built-in ROM 'tarbell-sd' is missing. SHOW ROMS lists them.");
        return;
    }
    Image       img;
    std::string err;
    if (!decodeRom(*rom, 0, img, err)) {
        log_.push_back(id + ": tarbell-sd: " + err);
        return;
    }
    for (const auto& [a, b] : img.bytes)
        if (a < 32) prom_[a] = b;
}

// ---------------------------------------------------------------------------
// Geometry probes. The board probes (not DiskImage): the same byte count means
// different geometries on different controllers (DESIGN.md 7.3).
// ---------------------------------------------------------------------------
bool TarbellBoard::describeGeometry(uint64_t bytes, int& tracks, int& heads, bool& interleaved,
                                    std::vector<FmtRange>& ranges, std::string& err) const {
    const uint64_t full = 77ull * 26 * 128;  // 256,256 -- 8" SD, 77 x 26 x 128
    if (sizeMatches(bytes, full)) {
        tracks = 77;
        heads  = 1;
        interleaved = false;
        ranges = {{0, 76, 0, 0, Density::SD, 26, 128, 1}};  // soft-sector: sectors from 1
        return true;
    }
    // BLANK / SHORT -> an UNFORMATTED disk, not an error (matches SIMH tarbell_attach's rule:
    // anything that is not the recognized size is SSSD). Mount it at the SD track count with
    // EMPTY per-track geometry (no ranges): the drive is READY and steppable, but every access
    // RNFs until the guest's FORMAT writes a track (Write Track -> setTrackFormat). This is
    // what makes MOUNT ... CREATE (a 0-byte file) formattable. Empty-pending is preferred over
    // fabricating SD slots, since Write Track is the sole source of geometry (DESIGN.md 7.3).
    if (bytes < full) {
        tracks = 77;
        heads  = 1;
        interleaved = false;
        ranges.clear();
        return true;
    }
    // Oversized/garbage is still an error -- a real SD disk is never larger than one revolution
    // per track, and growing past that would manufacture tracks the controller cannot reach.
    err = std::to_string(bytes) + " bytes is too large for a Tarbell single-density disk (" +
          std::to_string(full) + " = 77 x 26 x 128 SD).";
    return false;
}

// The SD card formats at one 8" SD revolution. (The DD override returns 0 -- deferred.)
int TarbellBoard::formatTrackBytes() const { return kSdTrackBytes; }

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------
std::vector<Property> TarbellBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name = "bootstrap";
        x.help = "The boot-PROM enable DIP. On (default): the 32-byte PROM shadows 0000 over "
                 "PHANTOM* at reset and boots the disk. Off: a plain disk controller, no PROM";
        x.kind = Kind::Bool;
        x.get  = [this] { return Value::ofBool(bootstrap_); };
        x.set  = [this](const Value& v, std::string&) {
            bootstrap_ = v.b();
            decodeChanged();  // both decodes() and assertsPhantom() read bootstrap_
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "port";
        x.help  = "Base address. The board decodes eight ports: BASE+0 .. BASE+7 (default F8)";
        x.kind  = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xF8;
        x.get   = [this] { return Value::ofInt(port_); };
        x.set   = [this](const Value& v, std::string&) {
            port_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "drives";
        x.help  = "Drives on the controller (binary select 0-3)";
        x.kind  = Kind::Int;
        x.radix = 10;  // NEVER on the wire -> DECIMAL. It is a count.
        x.min   = 1;
        x.max   = 4;
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
            applySelection();  // the vector may have moved -- re-attach
            return true;
        };
        p.push_back(std::move(x));
    }
    p.push_back(irqJumperProperty("interrupt", "Where the card's interrupt is soldered", irq_));
    return p;
}

std::vector<MapEntry> TarbellBoard::ioMap() const {
    return {
        {(uint32_t)port_ + 0, (uint32_t)port_ + 0, "command/status", "FD177x"},
        {(uint32_t)port_ + 1, (uint32_t)port_ + 1, "track",          "FD177x"},
        {(uint32_t)port_ + 2, (uint32_t)port_ + 2, "sector",         "FD177x"},
        {(uint32_t)port_ + 3, (uint32_t)port_ + 3, "data",           "FD177x (wait-state synced)"},
        {(uint32_t)port_ + 4, (uint32_t)port_ + 4, "control/wait",   "drive select / DRQ/INTRQ (bit7)"},
    };
}

std::vector<MapEntry> TarbellBoard::memMap() const {
    if (!bootstrap_) return {};
    return {{0x0000, 0x001F, "boot PROM",
             "32-byte bootstrap; shadows RAM over PHANTOM* until an A5-high read releases it"}};
}

// ---------------------------------------------------------------------------
// Units, MOUNT, UNMOUNT, and the [[board.drive]] sub-unit table.
// ---------------------------------------------------------------------------
std::vector<UnitDef> TarbellBoard::units() const {
    std::vector<UnitDef> u;
    for (int i = 0; i < drives_; ++i) {
        const Drive& d = drive_[(size_t)i];
        UnitDef x;
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

static int driveIndex(const std::string& unit, int count) {
    if (unit.rfind("drive", 0) != 0) return -1;
    const std::string n = unit.substr(5);
    if (n.empty()) return -1;
    for (char ch : n)
        if (ch < '0' || ch > '9') return -1;
    int i = std::stoi(n);
    return (i >= 0 && i < count) ? i : -1;
}

bool TarbellBoard::mount(const std::string& unit, const std::string& path, bool ro,
                         std::string& err) {
    int i = driveIndex(unit, drives_);
    if (i < 0) {
        err = "no unit `" + unit + "` on " + id + " (it has drive0.." +
              std::to_string(drives_ - 1) + ")";
        return false;
    }

    auto media = openMedia(resolvePath(path), ro, err);
    if (!media) { err += pathNote(path); return false; }

    auto img = std::make_unique<DiskImage>(std::move(media));

    int  tracks = 0, heads = 0;
    bool interleaved = false;
    std::vector<FmtRange> ranges;
    if (!describeGeometry(img->size(), tracks, heads, interleaved, ranges, err))
        return false;  // a failed probe leaves the old disk in place

    img->init(tracks, heads, interleaved);
    for (const FmtRange& fr : ranges)
        img->initFormat(fr.trackLo, fr.trackHi, fr.headLo, fr.headHi, fr.density, fr.sectors,
                        fr.sectorSize, fr.startSector);

    const bool forcedRo = img->readOnlyForced();

    // EXTEND ON WRITE, always (mirror mits-hardsector.cpp). A recognized full disk never
    // grows -- its writes stay within the declared geometry -- but a blank/short one grows
    // as the guest's FORMAT streams each track (Write Track -> setTrackFormat), capped at the
    // dynamic geometryBytes_ (host/disk.h). The soft-sector "a short image is truncated" rule
    // is deliberately reversed here: Write Track is the source of geometry (DESIGN.md 7.3).
    img->setExtendsOnWrite(true);

    Drive& d = drive_[(size_t)i];
    d.img  = std::move(img);
    d.path = path;
    d.drv.mount(d.img.get(), ro);
    d.drv.setHeadTrack(0);
    // Grant the drive the card's format budget (0 on the DD card, deferred). Nonzero = "this
    // card formats" (Write Track fills exactly this many bytes; see floppy-drive.h).
    d.drv.setTrackCapacity(formatTrackBytes());
    if (sel_ == i) applySelection();  // re-point the chip at the new medium

    if (forcedRo) {
        char m[192];
        std::snprintf(m, sizeof m,
                      "drive%d mounted WRITE-PROTECTED -- the host will not let us write %s",
                      i, path.c_str());
        log_.push_back(m);
    }
    return true;
}

bool TarbellBoard::unmount(const std::string& unit, std::string& err) {
    int i = driveIndex(unit, drives_);
    if (i < 0) { err = "no unit `" + unit + "` on " + id; return false; }

    Drive& d = drive_[(size_t)i];
    if (!d.img) { err = id + ":" + unit + " is empty"; return false; }

    d.img->sync();
    d.img.reset();
    d.path.clear();
    d.drv.eject();
    if (sel_ == i) applySelection();  // the drive is empty now: NOT READY
    return true;
}

std::vector<Property> TarbellBoard::subUnitProperties(const std::string& table) const {
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
        x.help    = "Write-protect the disk. The drive senses it, so the guest is never told";
        x.kind    = Kind::Bool;
        x.aliases = {"writeprotect"};
        p.push_back(std::move(x));
    }
    return p;
}

bool TarbellBoard::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
    if (table != "drive") {
        err = type() + " has no [[board." + table + "]] table";
        return false;
    }

    int         unit = -1;
    std::string path;
    bool        ro = false;

    // loadSubUnit() has already refused an undeclared key and a `unit` outside range, and
    // has VALIDATED every value against the schema. It hands us the reader's original text,
    // so `readonly` must be parsed the SAME way the validator accepted it (parseValue,
    // case-insensitively: True/On/YES all mean true).
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

std::vector<Board::SubUnit> TarbellBoard::subUnits() const {
    std::vector<SubUnit> out;
    for (int i = 0; i < drives_; ++i) {
        const Drive& d = drive_[(size_t)i];
        if (!d.img) continue;

        SubUnit su;
        su.table = "drive";
        su.fields.push_back({"unit", std::to_string(i), false});  // DECIMAL: a count
        su.fields.push_back({"mount", d.path, true});
        if (d.img->readOnly()) su.fields.push_back({"readonly", "true", false});
        out.push_back(std::move(su));
    }
    return out;
}

std::vector<std::string> TarbellBoard::drainLog() {
    std::vector<std::string> out = std::move(log_);
    log_.clear();
    if (chip_)
        for (auto& s : chip_->drainLog()) out.push_back(std::move(s));
    for (auto& s : out) s = id + ":" + s;
    return out;
}

// ---------------------------------------------------------------------------
// SNAPSHOT/RESTORE (DESIGN.md 13). The controller state that is NOT host-backed: the
// chip's register file and any command in flight (chip_->serialize), the drive-select
// and side latches, each drive's head position, and the PROM flip-flop (armed_ -- a
// runtime latch that must travel). The disk IMAGES are host-backed; `bootstrap_` is a
// strap and is NOT serialized (a snapshot RESTOREs into a machine built from the same
// config, so it is already correct).
// ---------------------------------------------------------------------------
void TarbellBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u8((uint8_t)sel_);
    w.u8((uint8_t)side_);
    w.u32((uint32_t)dataRate_);
    w.boolean(armed_);
    w.u32((uint32_t)drive_.size());
    for (const Drive& d : drive_) w.u32((uint32_t)d.drv.headTrackRaw());
    chip_->serialize(w);
}

void TarbellBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    sel_      = (int)r.u8();
    side_     = (int)r.u8();
    dataRate_ = (long long)r.u32();
    armed_    = r.boolean();
    uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; ++i) {
        int head = (int)r.u32();
        if (i < drive_.size()) drive_[i].drv.setHeadTrack(head);
    }
    chip_->deserialize(r);
    applySelection();  // re-attach the selected drive and re-apply side/rate
    refresh();         // re-drive the wires from the restored chip state
    decodeChanged();   // armed_ may have changed the shadow decode
}

// ===========================================================================
// TarbellDdBoard -- the double-density #2022.
// ===========================================================================
void TarbellDdBoard::buildChip() {
    chip_ = std::make_unique<Wd1791>("fdc");
    chip_->setWaitSynced(true);
    if (clock_) chip_->powerOn(*clock_);
    applySelection();
}

// OUT FC on the DD card is a PLAIN BITMAP LATCH (not the SD function decoder):
//   bit3 = density (0 = SD, 1 = DD), bits4-5 = binary drive select, bit6 = side.
// Density is fidelity-only here -- the read path takes geometry entirely from the
// image's per-track TrackFormat -- but the strap is set so it is right for anyone who
// reads it (and matches the media bit rate).
void TarbellDdBoard::writeControl(uint8_t v) {
    dataRate_ = ((v >> 3) & 1) ? 500000 : 250000;
    sel_      = (v >> 4) & 0x03;
    side_     = (v >> 6) & 1;
    applySelection();
}

// Port FD IN: the DMA-busy check. We are a PIO model and never busy, so bit7 = 0
// ("transfer complete") -- period DMA code that polls it never hangs.
uint8_t TarbellDdBoard::readExtra(uint8_t off) const {
    if (off == 5) return 0x00;
    return 0xFF;
}

// Port FD OUT: the A16-A23 extended-address latch. Harmless to store in our PIO model.
void TarbellDdBoard::writeExtra(uint8_t off, uint8_t v) {
    if (off == 5) extAddr_ = v;
}

// The DD disk: track 0 is single density (the FD179x powers up density-clear and the
// IBM 3740 index convention keeps track 0 SD), tracks 1-76 double density. Two
// initFormat ranges, both single-sided.
bool TarbellDdBoard::describeGeometry(uint64_t bytes, int& tracks, int& heads, bool& interleaved,
                                      std::vector<FmtRange>& ranges, std::string& err) const {
    const uint64_t sd0 = 26ull * 128;             // track 0: 26 x 128       = 3,328
    const uint64_t dd  = 76ull * 51ull * 128;     // tracks 1-76: 51 x 128   = 496,128
    if (sizeMatches(bytes, sd0 + dd)) {           // 499,456
        tracks = 77;
        heads  = 1;
        interleaved = false;
        ranges = {{0, 0, 0, 0, Density::SD, 26, 128, 1},
                  {1, 76, 0, 0, Density::DD, 51, 128, 1}};
        return true;
    }
    err = std::to_string(bytes) + " bytes is not a Tarbell double-density disk (expected " +
          std::to_string(sd0 + dd) + " = SD track 0 + 76 x 51 x 128 DD).";
    return false;
}

void TarbellDdBoard::serialize(StateWriter& w) const {
    TarbellBoard::serialize(w);
    w.u8(extAddr_);
}

void TarbellDdBoard::deserialize(StateReader& r) {
    TarbellBoard::deserialize(r);
    extAddr_ = r.u8();
}

} // namespace altair
