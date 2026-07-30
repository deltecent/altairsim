#include "boards/sd-versafloppy.h"

#include "core/bus.h"
#include "core/clock.h"
#include "core/statefile.h"
#include "core/value.h"
#include "host/media.h"

#include <cstdio>
#include <string>

namespace altair {

// An unclocked card is a chip with no crystal: it cannot time a seek, so it reads dead
// rather than dereferencing a null Clock. (Same idiom as the 2SIO.)
static Clock& deadCard() {
    static Clock stopped;
    return stopped;
}

Clock& VersaFloppyBoard::clk() const { return clock_ ? *clock_ : deadCard(); }

// ---------------------------------------------------------------------------
// THE MEDIA THIS CARD KNOWS -- all ten SD Systems formats, the `Z`-command format codes 0-7,C,D
// (reference/SD Systems Monitor.md §3.4), cross-checked against the DDBIOS density tables
// (reference/SD Systems VersaFloppy.md §6). Five physical geometries x single/double sided; all
// IBM 3740 soft-sector, sectors numbering from 1.
//
// ONE SIZE COLLIDES: 8" SS-DD-256 (fC) and 8" DS-SD-128 (f1) are both 512,512 bytes. So `8dd256`
// is listed BEFORE `8sd-ds` and the size probe below takes the first hit -- 512,512 auto-probes
// as fC, the SDOS master; `media=8sd-ds` forces the FM double-sided reading. Every other size is
// distinct, so an unforced probe is unambiguous.
//
// RPM: the 8" formats spin at 360 RPM (6 rev/s), the 5.25" minis at 300 RPM (5 rev/s). This is
// the Write Track byte-budget denominator (DiskImageDrive::setRevsPerSecond); it is a property of
// the physical drive size, so it is set per mounted drive, not read from a control bit.
// ---------------------------------------------------------------------------
struct VfFormat {
    const char* name;
    int         tracks;
    int         heads;
    int         sectors;
    int         sectorSize;
    Density     density;
    int         revsPerSec;  // 6 = 360 RPM (8"), 5 = 300 RPM (5.25")
    uint64_t    bytes;       // tracks * heads * sectors * sectorSize
};

static const std::vector<VfFormat>& vfFormats() {
    static const std::vector<VfFormat> f = {
        {"8sd",       77, 1, 26, 128, Density::SD, 6, 77ull * 1 * 26 * 128},  // f0  8" SS-SD  256,256
        {"8dd",       77, 1, 50, 128, Density::DD, 6, 77ull * 1 * 50 * 128},  // f4  8" SS-DD  492,800
        {"8dd256",    77, 1, 26, 256, Density::DD, 6, 77ull * 1 * 26 * 256},  // fC  8" SS-DD-256  512,512 (SDOS master; before 8sd-ds)
        {"8sd-ds",    77, 2, 26, 128, Density::SD, 6, 77ull * 2 * 26 * 128},  // f1  8" DS-SD  512,512
        {"8dd-ds",    77, 2, 50, 128, Density::DD, 6, 77ull * 2 * 50 * 128},  // f5  8" DS-DD  985,600
        {"8dd256-ds", 77, 2, 26, 256, Density::DD, 6, 77ull * 2 * 26 * 256},  // fD  8" DS-DD-256  1,025,024
        {"5sd",       35, 1, 18, 128, Density::SD, 5, 35ull * 1 * 18 * 128},  // f2  5" SS-SD   80,640
        {"5sd-ds",    35, 2, 18, 128, Density::SD, 5, 35ull * 2 * 18 * 128},  // f3  5" DS-SD  161,280
        {"5dd",       35, 1, 29, 128, Density::DD, 5, 35ull * 1 * 29 * 128},  // f6  5" SS-DD  129,920
        {"5dd-ds",    35, 2, 29, 128, Density::DD, 5, 35ull * 2 * 29 * 128},  // f7  5" DS-DD  259,840
    };
    return f;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
VersaFloppyBoard::VersaFloppyBoard() {
    drive_.resize((size_t)drives_);
    buildChip();
}

VersaFloppyBoard::~VersaFloppyBoard() {
    // The Clock is holding a lambda with `this` in it; a card can be pulled from a running
    // machine, and a deadline firing into a freed board is a use-after-free.
    if (clock_) clock_->cancel(wake_);
}

// THE PART IS THE VARIANT. VF-I has an FD1771, VF-II an FD1791; the rest of the family
// (register file, command set) is shared in Wd17xx. Always wait-synced: the VersaFloppy
// stalls the CPU on PRDY rather than exposing DRQ, so every command completes on the access
// that would have waited (see the header and wd17xx.h).
void VersaFloppyBoard::buildChip() {
    if (variant_ == Variant::Vf1) chip_ = std::make_unique<Wd1771>("fdc");
    else                          chip_ = std::make_unique<Wd1791>("fdc");
    chip_->setWaitSynced(true);
    if (clock_) chip_->powerOn(*clock_);
    selectFromControl();  // re-attach whatever the control latch had selected
}

VersaFloppyBoard::Drive* VersaFloppyBoard::selected() {
    return (sel_ < 0 || sel_ >= (int)drive_.size()) ? nullptr : &drive_[(size_t)sel_];
}

// ---------------------------------------------------------------------------
// The 63H control latch -- decode drive select, side and (VF-II) density, and point the
// chip at the selected drive. THE BIT LAYOUT DIFFERS between the two boards; this is the
// single most software-visible difference between them (reference §3).
//
//   VF-I : D0-D3 drive, D4 side, D5 restore, D6 wait-enable, D7 int-enable
//   VF-II: D0-D3 drive, D4 side, D5 5"/8",   D6 density,      D7 wait-enable
//
// THE 63H LATCH IS NEGATIVE-TRUE (inverted), and the manual does not say so -- DDB200.ASM
// does: DRVSET does `CPL ;HRDWRE REG IS INVERTED` before `OUT (SELECT)`, and SWEB/DWAIT toggle
// the wait bit with `AND 7FH ;NEG TRUE ... SETS` / `OR 80H ... RESETS`. So the guest writes the
// COMPLEMENT of the control it wants. We keep `control_` in TRUE sense (a bit set = active);
// write() inverts on the way in and read() inverts on the way out, so a drive-0 select (the
// guest writes ~1) really points at drive 0. Miss this and every write picks the wrong drive,
// the disk reads NOT READY, and the boot hangs. (The 64-67 FDC data bus is a separate story --
// its inverting buffers cancel the 179x's negative-true DAL, so those registers are true-sense.)
//
// Drive select is ONE-HOT (one line per drive); we take the lowest set bit.
// ---------------------------------------------------------------------------
void VersaFloppyBoard::selectFromControl() {
    int newSel = -1;
    for (int i = 0; i < 4 && i < drives_; ++i)
        if (control_ & (1u << i)) { newSel = i; break; }
    sel_ = newSel;

    const int side = (control_ >> 4) & 1;

    // VF-II density (D6): double density doubles the media bit rate. Under the wait-synced
    // model the byte rate is not observable, but the strap is set so it is right for anyone
    // who ever reads it (and for a future DRQ-polling sibling).
    if (variant_ == Variant::Vf2 && chip_)
        chip_->dataRateBits = ((control_ >> 6) & 1) ? 500000 : 250000;

    FloppyDrive* fd = nullptr;
    if (Drive* d = selected()) {
        d->drv.setSide(side);
        fd = &d->drv;
    }
    if (chip_) {
        chip_->attach(fd);
        chip_->setSide(side);
    }
}

// ---------------------------------------------------------------------------
// The bus. Eight ports; 61/62 unused.
// ---------------------------------------------------------------------------
bool VersaFloppyBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    uint8_t p = c.port();
    // `p - port_`, NOT `p < port_ + 8`: at the top of the property's range (port = F8) the
    // latter truncates F8+8 to 0 and the board would decode nothing. Since p >= port_, the
    // 8-bit difference is the true offset and the window test never overflows.
    return p >= port_ && (uint8_t)(p - port_) < 8;
}

uint8_t VersaFloppyBoard::read(const BusCycle& c) {
    Clock&  k   = clk();
    uint8_t off = (uint8_t)(c.port() - port_);
    uint8_t v   = 0xFF;
    switch (off) {
        case 0: break;  // 60H reset strobe: nothing to read
        case 1:
        case 2: break;  // unused
        case 3:
            // 63H status readback -- the NEGATIVE-TRUE latch, so we invert control_ back to the
            // raw register the guest expects. VF-II reads all eight bits back; VF-I reads the
            // low five plus INTRQ at D7 (reference §3.3). VF-I's INTRQ readback polarity is not
            // boot-critical (the standard software polls the FDC status); we return it as-is.
            if (variant_ == Variant::Vf2) v = (uint8_t)~control_;
            else v = (uint8_t)((~control_ & 0x1F) | (chip_->intrq() ? 0x80 : 0x00));
            break;
        case 4: v = chip_->readStatus(k);   break;
        case 5: v = chip_->readTrackReg();  break;
        case 6: v = chip_->readSectorReg(); break;
        case 7: v = chip_->readData(k);     break;
    }
    refresh();
    return v;
}

void VersaFloppyBoard::write(const BusCycle& c) {
    Clock&  k   = clk();
    uint8_t off = (uint8_t)(c.port() - port_);
    switch (off) {
        case 0:
            // 60H controller reset -- the VF-II responds (DDB200.ASM's `RSET`); the VF-I
            // decoder does not gate 60H, so it is a no-op there (reference §2).
            if (variant_ == Variant::Vf2) chip_->masterReset(k);
            break;
        case 1:
        case 2: break;  // unused
        case 3: control_ = (uint8_t)~c.data; selectFromControl(); break;  // negative-true latch
        case 4: chip_->writeCommand(c.data, k);  break;
        case 5: chip_->writeTrackReg(c.data);    break;
        case 6: chip_->writeSectorReg(c.data);   break;
        case 7: chip_->writeData(c.data, k);     break;
    }
    refresh();
}

// ---------------------------------------------------------------------------
// Interrupts. The standard SD software polls, but the board can be strapped to a wire.
// Gate on the jumper and, on the VF-I, its int-enable control bit (D7).
// ---------------------------------------------------------------------------
bool VersaFloppyBoard::assertsInt() const {
    if (!chip_ || irq_ != IrqJumper::Int || !chip_->intrq()) return false;
    if (variant_ == Variant::Vf1 && !(control_ & 0x80)) return false;  // VF-I D7 int-enable
    return true;
}

uint8_t VersaFloppyBoard::assertsVi() const {
    if (!chip_ || !chip_->intrq()) return 0;
    if (variant_ == Variant::Vf1 && !(control_ & 0x80)) return 0;
    return viBit(irq_);
}

// ---------------------------------------------------------------------------
// The card's own clock discipline (the 2SIO idiom): advance the chip, re-drive pin 73,
// re-arm the one deadline. Under wait-synced operation the chip has no autonomous edge
// (nextEdge() returns 0), so no timer is armed -- everything happens on a register access.
// ---------------------------------------------------------------------------
void VersaFloppyBoard::refresh() {
    if (!clock_) return;
    chip_->poll(*clock_);
    intChanged();
    clock_->cancel(wake_);
    wake_ = Clock::kNone;
    uint64_t e = chip_->nextEdge(*clock_);
    if (e) wake_ = clock_->at(e, [this] { refresh(); });
}

void VersaFloppyBoard::reset(Reset r) {
    if (!clock_) return;
    if (r == Reset::PowerOn) chip_->powerOn(*clock_);
    refresh();
}

void VersaFloppyBoard::power() { reset(Reset::PowerOn); }

void VersaFloppyBoard::pump() { refresh(); }

void VersaFloppyBoard::configChanged() {
    decodeChanged();  // `port` may have moved the card
    refresh();
}

std::vector<std::string> VersaFloppyBoard::drainLog() {
    std::vector<std::string> out = std::move(log_);
    log_.clear();
    if (chip_)
        for (auto& s : chip_->drainLog()) out.push_back(std::move(s));
    for (auto& s : out) s = id + ":" + s;
    return out;
}

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------
std::vector<Property> VersaFloppyBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name    = "variant";
        x.help    = "Which board: vfi (FD1771, single density) or vfii (FD1791, single and "
                    "double density). vfii is the default -- it boots SDOS's DD-256 disks";
        x.kind    = Kind::Enum;
        x.choices = {"vfi", "vfii"};
        x.get     = [this] { return Value::ofStr(variant_ == Variant::Vf1 ? "vfi" : "vfii"); };
        x.set     = [this](const Value& v, std::string&) {
            Variant want = (v.s() == "vfi") ? Variant::Vf1 : Variant::Vf2;
            if (want != variant_) { variant_ = want; buildChip(); }
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "port";
        x.help  = "Base address. The board decodes eight ports: BASE+0 .. BASE+7 (60H)";
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
        x.help  = "Drives on the controller (one-hot select D0-D3)";
        x.kind  = Kind::Int;
        x.radix = 10;  // NEVER on the wire -> DECIMAL. It is a count.
        x.min   = 1;
        x.max   = 4;
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
            selectFromControl();  // the vector may have moved -- re-attach
            return true;
        };
        p.push_back(std::move(x));
    }
    p.push_back(irqJumperProperty("interrupt", "Where the card's interrupt is soldered", irq_));
    return p;
}

std::vector<MapEntry> VersaFloppyBoard::ioMap() const {
    return {
        {(uint32_t)port_ + 0, (uint32_t)port_ + 0, "reset",         "controller reset (VF-II)"},
        {(uint32_t)port_ + 3, (uint32_t)port_ + 3, "control/status","drive/side/density/wait / readback + INTRQ"},
        {(uint32_t)port_ + 4, (uint32_t)port_ + 4, "command/status","FD177x"},
        {(uint32_t)port_ + 5, (uint32_t)port_ + 5, "track",         "FD177x"},
        {(uint32_t)port_ + 6, (uint32_t)port_ + 6, "sector",        "FD177x"},
        {(uint32_t)port_ + 7, (uint32_t)port_ + 7, "data",          "FD177x (wait-state synced)"},
    };
}

// ---------------------------------------------------------------------------
// Units, MOUNT, UNMOUNT, and the [[board.drive]] sub-unit table.
// ---------------------------------------------------------------------------
std::vector<UnitDef> VersaFloppyBoard::units() const {
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

static const VfFormat* findFormat(const std::string& name) {
    for (const auto& f : vfFormats())
        if (name == f.name) return &f;
    return nullptr;
}

// Probe the mounted image's size against the format table (or a forced `media`), and report via
// `layFormat` whether the image is fully recorded (lay its per-track geometry down) or a blank /
// short one (use the shape, but leave the tracks unformatted so the guest's FORMAT writes them --
// Write Track -> setTrackFormat). The board probes, not DiskImage (DESIGN.md 7.3): the same byte
// count means different geometries on different controllers.
//
//   - forced `media=NAME`: the name IS the geometry. Matching size -> recorded; short -> a blank
//     of that shape (the MOUNT ... CREATE case); larger -> too big for that format (error).
//   - no `media`: match by size (8dd256 before 8sd-ds resolves the 512,512 collision). A blank /
//     short image with no `media` cannot be shaped -> "specify media=NAME"; an unrecognized
//     larger size -> error.
static const VfFormat* probe(uint64_t got, const std::string& forced, const std::string& who,
                             bool& layFormat, std::string& err) {
    if (!forced.empty()) {
        const VfFormat* f = findFormat(forced);
        if (!f) { err = "unknown media `" + forced + "`"; return nullptr; }  // schema-validated
        if (sizeMatches(got, f->bytes)) { layFormat = true;  return f; }  // pad tolerance
        if (got < f->bytes)             { layFormat = false; return f; }  // blank / short -> format it
        err = std::to_string(got) + " bytes is too large for media=" + forced + " (" +
              std::to_string(f->bytes) + ").";
        return nullptr;
    }

    for (const auto& f : vfFormats())
        if (sizeMatches(got, f.bytes)) { layFormat = true; return &f; }  // first hit wins (collision)

    uint64_t smallest = UINT64_MAX;
    for (const auto& f : vfFormats()) smallest = f.bytes < smallest ? f.bytes : smallest;
    if (got < smallest) {
        err = std::to_string(got) + " bytes: too small to identify a " + who +
              " format -- set `media=NAME` to format a blank.";
        return nullptr;
    }

    std::string sizes;
    for (const auto& f : vfFormats()) {
        if (!sizes.empty()) sizes += ", ";
        sizes += std::string(f.name) + "=" + std::to_string(f.bytes);
    }
    err = std::to_string(got) + " bytes matches no " + who + " format (" + sizes +
          "). Set `media` to force one.";
    return nullptr;
}

bool VersaFloppyBoard::mount(const std::string& unit, const std::string& path, bool ro,
                             std::string& err) {
    int i = driveIndex(unit, drives_);
    if (i < 0) {
        err = "no unit `" + unit + "` on " + id + " (it has drive0.." +
              std::to_string(drives_ - 1) + ")";
        return false;
    }

    // WHERE WE LOOK is resolvePath(); WHAT WE REMEMBER is `path`, as written (core/board.h),
    // so SHOW and CONFIG SAVE round-trip and the file still loads from its own directory.
    auto media = openMedia(resolvePath(path), ro, err);
    if (!media) { err += pathNote(path); return false; }

    auto img = std::make_unique<DiskImage>(std::move(media));
    bool layFormat = false;
    const VfFormat* fmt = probe(img->size(), drive_[(size_t)i].forced, type(), layFormat, err);
    if (!fmt) return false;  // a failed probe leaves the old disk in place

    img->init(fmt->tracks, fmt->heads, /*interleaved=*/false);
    if (layFormat)  // a recognized full disk; a blank stays unformatted until Write Track fills it
        img->initFormat(0, fmt->tracks - 1, 0, fmt->heads - 1, fmt->density, fmt->sectors,
                        fmt->sectorSize, /*startSector=*/1);  // soft-sector: sectors number from 1

    // EXTEND ON WRITE, always (mirror the Tarbell). A recognized full disk never grows -- its
    // writes stay within the declared geometry -- but a blank/short one grows as the guest's
    // FORMAT streams each track (Write Track -> setTrackFormat), capped at the dynamic
    // geometryBytes_. init(...) laid the slot order out from the named media, so a double-sided
    // blank grows in ascending order (all of side 0, then side 1) as required (host/disk.h).
    img->setExtendsOnWrite(true);

    const bool forcedRo = img->readOnlyForced();

    Drive& d = drive_[(size_t)i];
    d.img    = std::move(img);
    d.path   = path;
    d.roSaid = false;
    d.drv.mount(d.img.get(), ro);
    d.drv.setHeadTrack(0);
    // Both VersaFloppy generations format via Write Track (VF-I the four FM codes, VF-II all ten).
    // The recorded density and the per-track byte budget come from the chip's data rate and the
    // drive's RPM at Write Track time, not from the card here -- so the guest's `Z` command, which
    // sets the density bit (D6 -> dataRateBits) per format code, drives the geometry.
    d.drv.setFormatting(true);
    // The drive's rotation speed is fixed by its physical size (8" = 360 RPM, 5.25" = 300 RPM):
    // the Write Track budget denominator. Set from the named/probed media -- a diskette's RPM is
    // a property of the drive, not a control bit, so mount time is where it is known.
    d.drv.setRevsPerSecond(fmt->revsPerSec);
    if (sel_ == i) selectFromControl();  // re-point the chip at the new medium

    if (forcedRo) {
        char m[192];
        std::snprintf(m, sizeof m,
                      "drive%d mounted WRITE-PROTECTED -- the host will not let us write %s",
                      i, path.c_str());
        log_.push_back(m);
    }
    return true;
}

bool VersaFloppyBoard::unmount(const std::string& unit, std::string& err) {
    int i = driveIndex(unit, drives_);
    if (i < 0) { err = "no unit `" + unit + "` on " + id; return false; }

    Drive& d = drive_[(size_t)i];
    if (!d.img) { err = id + ":" + unit + " is empty"; return false; }

    d.img->sync();
    d.img.reset();
    d.path.clear();
    d.drv.eject();
    if (sel_ == i) selectFromControl();  // the drive is empty now: NOT READY
    return true;
}

std::vector<Property> VersaFloppyBoard::subUnitProperties(const std::string& table) const {
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
    {
        Property x;
        x.name = "media";
        x.help = "Force the format instead of probing the image's size";
        x.kind = Kind::Enum;
        for (const auto& f : vfFormats()) x.choices.push_back(f.name);
        p.push_back(std::move(x));
    }
    return p;
}

bool VersaFloppyBoard::addSubUnit(const std::string& table, const KeyValues& kv,
                                  std::string& err) {
    if (table != "drive") {
        err = type() + " has no [[board." + table + "]] table";
        return false;
    }

    int         unit = -1;
    std::string path, media;
    bool        ro = false;

    // loadSubUnit() has already refused an undeclared key, a `media` not in vfFormats(), and
    // a `unit` outside 0..drives-1, and it has VALIDATED every value against the schema. It
    // hands us the reader's original text, though -- so `readonly` must be parsed the SAME way
    // the validator accepted it (parseValue, case-insensitively: True/On/YES all mean true).
    // A hand-rolled `v == "on"` compare would silently mount a disk the operator protected.
    for (const auto& [k, v] : kv) {
        if (k == "unit") unit = std::stoi(v);
        else if (k == "mount") path = v;
        else if (k == "readonly") {
            Value bv;
            std::string e;
            if (parseValue(v, Kind::Bool, bv, e)) ro = bv.b();
        }
        else if (k == "media") media = v;
    }
    if (unit < 0) {
        err = "[[board.drive]] needs a `unit`";
        return false;
    }
    if (unit >= drives_) {
        err = "[[board.drive]] unit " + std::to_string(unit) + " but the card has " +
              std::to_string(drives_) + " drives";
        return false;
    }

    drive_[(size_t)unit].forced = media;
    if (path.empty()) return true;
    return mount("drive" + std::to_string(unit), path, ro, err);
}

std::vector<Board::SubUnit> VersaFloppyBoard::subUnits() const {
    std::vector<SubUnit> out;
    for (int i = 0; i < drives_; ++i) {
        const Drive& d = drive_[(size_t)i];
        if (!d.img && d.forced.empty()) continue;  // an empty, unforced drive says nothing

        SubUnit su;
        su.table = "drive";
        su.fields.push_back({"unit", std::to_string(i), false});  // DECIMAL: a count
        if (!d.forced.empty()) su.fields.push_back({"media", d.forced, true});
        if (d.img) {
            su.fields.push_back({"mount", d.path, true});
            if (d.img->readOnly()) su.fields.push_back({"readonly", "true", false});
        }
        out.push_back(std::move(su));
    }
    return out;
}

// ---------------------------------------------------------------------------
// SNAPSHOT/RESTORE (DESIGN.md 13). The controller state that is NOT host-backed: the chip's
// whole register file and any command in flight (chip_->serialize), the control latch, and
// each drive's head position. The disk IMAGES are host-backed and do not travel; the format
// straps (waitSynced, side) are the card's and are re-applied here.
// ---------------------------------------------------------------------------
void VersaFloppyBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u8(control_);
    w.u32((uint32_t)drive_.size());
    for (const Drive& d : drive_) w.u32((uint32_t)d.drv.headTrackRaw());
    chip_->serialize(w);
}

void VersaFloppyBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    control_ = r.u8();
    uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; ++i) {
        int head = (int)r.u32();
        if (i < drive_.size()) drive_[i].drv.setHeadTrack(head);
    }
    chip_->deserialize(r);
    selectFromControl();  // re-attach the selected drive and re-apply side/density
    refresh();            // re-drive pin 73 from the restored chip state
}

} // namespace altair

