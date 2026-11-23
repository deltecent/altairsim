#include "boards/compupro-disk.h"

#include "core/debuglog.h"

#include "core/bus.h"
#include "core/clock.h"
#include "core/hex.h"
#include "core/roms.h"
#include "core/statefile.h"
#include "core/value.h"
#include "host/media.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace altair {

namespace {

// A DMA byte is one memory cycle the board drives: three bus states, no wait states.
constexpr uint32_t kDmaCycle = 3;

int driveIndex(const std::string& unit) {
    if (unit.size() != 6 || unit.rfind("drive", 0) != 0) return -1;
    const char ch = unit[5];
    return (ch >= '0' && ch < '0' + CompuProDiskBoard::kDrives) ? ch - '0' : -1;
}

// ---------------------------------------------------------------------------
// THE SIZE PROBE. A raw image carries no geometry, so its byte count decides it, from the
// formats CompuPro's own BIOS knows (HMXBIOS MAKEDPB, ACTIVE.LIB TRK5 = 80):
//
//   8"    77 cylinders. Cylinder 0 head 0 is always SD 26 x 128 (the IBM 3740 index
//         track, and what the boot ROM reads). Every other track is the disk's format:
//         SD 26 x 128, or DD 26 x 256, 15 x 512, 8 x 1024.
//   5.25" 80 cylinders, one format throughout: SD 16 x 128, or DD 16 x 256, 8 x 512,
//         5 x 1024. (The BIOS blocks every 5.25" track; there is no SD track 0.)
//
// Either size, one or two sides. TWO SIDES ARE CYLINDER-INTERLEAVED (C0H0, C0H1, C1H0...):
// the BIOS turns a logical track into cylinder t/2, head t&1, and the CP/M directory of
// the double-sided CCPM22Q disk is at slot 4 of the file, which is cylinder 2 head 0 only
// in that order.
//
// Three 5.25" formats share 327,680 bytes and two share 655,360, so those sizes are
// refused rather than guessed. A smaller image of no known size is a BLANK disk: READY,
// steppable, and formattable track by track with Format a Track. A blank has no size to probe,
// so it needs `media` to give it its size and sides, and without one it is refused, as on the
// VersaFloppy (#777). A slot that is never formatted takes no bytes (DiskImage::rebuild), so a
// format that writes fewer tracks still lays out end to end, and the size probe reads it back.
//
// A board takes the sizes its drives are: the Disk 1A both, a Disk 1 the one it was built
// for (takes8()/takes525()). An image the size of a disk it does not take is refused, not
// mounted as a blank.
//
// `media` IS THE DISKETTE. A soft-sector disk's density and sector size are whatever FORMAT
// wrote, so only the size and the sides belong to the physical media: `8ds` is a blank 8"
// two-sided diskette, and TS is high from the mount, as on a real one (#755). A full format
// (`5ds-dd512`) also names the tracks, for the sizes that more than one format shares.
// ---------------------------------------------------------------------------
struct SizeFormat {
    bool    eightInch;
    Density density;
    int     sectors, size;
};

constexpr SizeFormat kFormats[] = {
    {true,  Density::SD, 26, 128},  {true,  Density::DD, 26, 256},
    {true,  Density::DD, 15, 512},  {true,  Density::DD,  8, 1024},
    {false, Density::SD, 16, 128},  {false, Density::DD, 16, 256},
    {false, Density::DD,  8, 512},  {false, Density::DD,  5, 1024},
};

uint64_t formatBytes(const SizeFormat& f, int heads) {
    const uint64_t track = (uint64_t)f.sectors * (uint64_t)f.size;
    if (!f.eightInch) return 80ull * (uint64_t)heads * track;
    return 26ull * 128 + (77ull * (uint64_t)heads - 1) * track;
}

std::string describe(const SizeFormat& f, int heads) {
    return std::string(f.eightInch ? "8\" " : "5.25\" ") + (heads == 2 ? "DS" : "SS") +
           (f.density == Density::SD ? "SD " : "DD ") + std::to_string(f.sectors) + " x " +
           std::to_string(f.size);
}

// A `media` value: a shape (size and sides), and for a full format one kFormats row too.
struct Media {
    std::string       name;
    bool              eightInch;
    int               heads;
    const SizeFormat* fmt;  // null: the shape only
};

std::string mediaName(bool eightInch, int heads, const SizeFormat* f) {
    std::string n = std::string(eightInch ? "8" : "5") + (heads == 2 ? "ds" : "ss");
    if (f) n += f->density == Density::SD ? "-sd" : "-dd" + std::to_string(f->size);
    return n;
}

const std::vector<Media>& allMedia() {
    static const std::vector<Media> m = [] {
        std::vector<Media> v;
        for (bool eight : {true, false})
            for (int h = 1; h <= 2; ++h) {
                v.push_back({mediaName(eight, h, nullptr), eight, h, nullptr});
                for (const SizeFormat& f : kFormats)
                    if (f.eightInch == eight) v.push_back({mediaName(eight, h, &f), eight, h, &f});
            }
        return v;
    }();
    return m;
}

// Case-insensitive, as the schema accepted it.
const Media* findMedia(const std::string& name) {
    auto same = [&name](const std::string& n) {
        return std::equal(name.begin(), name.end(), n.begin(), n.end(), [](char a, char b) {
            return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
        });
    };
    for (const Media& m : allMedia())
        if (same(m.name)) return &m;
    return nullptr;
}

} // namespace

// Set up `img` from its size; false (and `err`) when the size is no disk this board reads.
// A `forced` media limits the probe to its size and sides (and its format, if it names one).
bool CompuProDiskBoard::probe(DiskImage& img, const std::string& forced, bool& eightInch,
                              std::string& err) const {
    const Media* m = forced.empty() ? nullptr : findMedia(forced);
    if (!forced.empty() && !m) {
        err = "unknown media `" + forced + "`";
        return false;
    }
    auto takes = [this, m](const SizeFormat& f) {
        if (m) return f.eightInch == m->eightInch && (!m->fmt || m->fmt == &f);
        return f.eightInch ? takes8() : takes525();
    };
    auto heads = [m](int h) { return !m || m->heads == h; };

    const uint64_t bytes = img.size();
    const SizeFormat*  hit   = nullptr;
    const SizeFormat*  big   = nullptr;
    int            hitHeads = 0;
    std::string    all, names;
    int            count = 0;
    for (const SizeFormat& f : kFormats) {
        if (!takes(f)) continue;
        if (!big || formatBytes(f, 2) > formatBytes(*big, 2)) big = &f;
        for (int h = 1; h <= 2; ++h)
            if (heads(h) && sizeMatches(bytes, formatBytes(f, h))) {
                if (!hit) { hit = &f; hitHeads = h; }
                all += (count ? ", " : "") + describe(f, h);
                names += (count ? ", " : "") + mediaName(f.eightInch, h, &f);
                ++count;
            }
    }
    if (count > 1) {
        err = std::to_string(bytes) + " bytes fits more than one 5.25\" format (" + all +
              "), and a raw image cannot say which. Set `media` to one of: " + names + ".";
        return false;
    }
    if (hit) {
        eightInch = hit->eightInch;
        const int cyls = eightInch ? 77 : 80;
        img.init(cyls, hitHeads, true);
        if (eightInch) {
            img.initFormat(0, 0, 0, 0, Density::SD, 26, 128, 1);
            if (hitHeads == 2)
                img.initFormat(0, 0, 1, 1, hit->density, hit->sectors, hit->size, 1);
            img.initFormat(1, cyls - 1, 0, hitHeads - 1, hit->density, hit->sectors, hit->size, 1);
        } else {
            img.initFormat(0, cyls - 1, 0, hitHeads - 1, hit->density, hit->sectors, hit->size, 1);
        }
        return true;
    }
    // A disk this board reads, but not the forced media or not a size the board takes, is
    // refused, not taken for a blank.
    for (const SizeFormat& f : kFormats)
        for (int h = 1; h <= 2; ++h) {
            if (!sizeMatches(bytes, formatBytes(f, h))) continue;
            if (m && (f.eightInch ? takes8() : takes525())) {
                err = std::to_string(bytes) + " bytes is a " + describe(f, h) +
                      " disk, not media = " + m->name + ".";
                return false;
            }
            if (!m && !takes(f)) {
                err = std::to_string(bytes) + " bytes is a " + describe(f, h) + " disk. This " +
                      traits_.name + " is built for " + (takes8() ? "8\"" : "5.25\"") + " drives.";
                return false;
            }
        }
    const int      sides   = m ? m->heads : 2;
    const uint64_t largest = formatBytes(*big, sides);
    if (bytes < largest) {
        // A BLANK. Its size and sides are the diskette's, and only `media` can say what they
        // are: a guess here was TS read from whatever FORMAT wrote, and an 8" drive for a
        // 5.25" diskette (#777).
        if (!m) {
            err = std::to_string(bytes) + " bytes: too small to identify a " + traits_.name +
                  " format -- set `media` to give a blank its size and sides (" +
                  (takes8() ? std::string("8ss, 8ds") : std::string()) +
                  (takes8() && takes525() ? ", " : "") + (takes525() ? "5ss, 5ds" : "") + ").";
            return false;
        }
        eightInch = m->eightInch;
        img.init(eightInch ? 77 : 80, m->heads, true);  // blank: no tracks formatted yet
        return true;
    }
    if (m) {
        err = std::to_string(bytes) + " bytes is larger than media = " + m->name + " (" +
              std::to_string(largest) + ").";
        return false;
    }
    err = std::to_string(bytes) + " bytes is larger than any disk the " + traits_.name +
          " reads (" + std::to_string(largest) + " = " + describe(*big, 2) + ").";
    return false;
}

// ---------------------------------------------------------------------------
// Construction: what the 765's pins reach (Upd765::Wiring), through cableUnit().
// ---------------------------------------------------------------------------
CompuProDiskBoard::CompuProDiskBoard(const Traits& traits)
    : traits_(traits), romSpec_(traits.romDefault), routine_(traits.routineDefault) {
    mover_.d = this;
    chip_.wiring.drive = [this](int us) -> FloppyDrive* { return &drive_[(size_t)cableUnit(us)].drv; };
    chip_.wiring.head  = [this](int us, int hd) { drive_[(size_t)cableUnit(us)].drv.setSide(hd); };
    // TS is the diskette's sides: from its size, or from `media` for a blank. Formatting
    // does not change it, as it does not change a real diskette.
    chip_.wiring.twoSided = [this](int us) {
        const DiskImage* img = drive_[(size_t)cableUnit(us)].img.get();
        return forceTwoSided() || (img && img->heads() > 1);
    };
    rom_.assign(traits_.romBytes, 0xFF);
}

CompuProDiskBoard::~CompuProDiskBoard() {
    // The Clock holds lambdas with `this` in them; the board can be pulled from a live machine.
    if (clock_) clock_->cancel(wake_);
}

void CompuProDiskBoard::clockAttached() {
    chip_.attachClock(clock_);
    for (Drive& d : drive_) d.drv.setClock(clock_);
}

// ---------------------------------------------------------------------------
// The bus.
// ---------------------------------------------------------------------------
bool CompuProDiskBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type == Cycle::IoRead || c.type == Cycle::IoWrite) {
        const uint8_t p = c.port();
        if (p < port_) return false;
        const uint8_t off   = (uint8_t)(p - port_);
        const bool    write = c.type == Cycle::IoWrite;
        if (off == 1 || (off == 0 && !write) || (off == 2 && write)) return true;
        if (off == 0 || off == 2 || off == 3) return decodesPort(off, write);
        return false;
    }
    // The ROM answers reads only; a write under it lands in the RAM (honors_phantom = read).
    if (c.type == Cycle::MemRead) return assertsPhantom(c);
    return false;
}

// PHANTOM* is a level over the ROM's window while the ROM is on (board.h).
bool CompuProDiskBoard::assertsPhantom(const BusCycle& c) const {
    if (!romOn_ || !boot_) return false;
    if (c.type != Cycle::MemRead && c.type != Cycle::MemWrite) return false;
    return c.addr < traits_.romWindow;
}

// DISASM and EXAMINE see the ROM while it is on. The ports are never peeked: an IN from the
// data register takes a result byte.
bool CompuProDiskBoard::peek(uint16_t addr, uint8_t& out) const {
    if (!enabled_ || !romOn_ || !boot_ || addr >= traits_.romWindow) return false;
    out = rom_[(size_t)routine_ * traits_.romWindow + addr];
    return true;
}

uint8_t CompuProDiskBoard::read(const BusCycle& c) {
    if (c.type == Cycle::MemRead)
        return rom_[(size_t)routine_ * traits_.romWindow + (c.addr & (traits_.romWindow - 1))];

    chip_.poll();
    uint8_t v = 0xFF;
    switch (const uint8_t off = (uint8_t)(c.port() - port_)) {
        case 0: v = chip_.readMsr(); break;
        case 1: v = chip_.readData(); break;
        default: v = readPort(off); break;
    }
    touched();
    refresh();
    return v;
}

void CompuProDiskBoard::write(const BusCycle& c) {
    if (c.type != Cycle::IoWrite) return;
    chip_.poll();
    switch (const uint8_t off = (uint8_t)(c.port() - port_)) {
        case 1: chip_.writeData(c.data); break;
        case 2:
            // A three-deep push-down stack, most significant byte first (§2).
            dma_ = ((dma_ << 8) | c.data) & 0xFFFFFFu;
            break;
        case 3:
            // D0 = 0 switches the boot ROM off, on both boards (§3).
            if (!(c.data & 0x01) && romOn_) {
                romOn_ = false;
                decodeChanged();
            }
            writePort(off, c.data);
            break;
        default: writePort(off, c.data); break;
    }
    touched();
    refresh();
}

// ---------------------------------------------------------------------------
// The chip's clock discipline: advance it, re-drive INT and pHOLD, re-arm the one deadline.
// ---------------------------------------------------------------------------
void CompuProDiskBoard::refresh() {
    chip_.poll();
    intChanged();
    holdChanged();
    if (!clock_) return;
    clock_->cancel(wake_);
    wake_ = Clock::kNone;
    if (uint64_t e = chip_.nextEdge()) wake_ = clock_->at(e, [this] { refresh(); });
}

bool CompuProDiskBoard::assertsInt() const {
    return irq_ == IrqJumper::Int && chip_.intPin();
}

uint8_t CompuProDiskBoard::assertsVi() const {
    return chip_.intPin() ? viBit(irq_) : 0;
}

// ---------------------------------------------------------------------------
// DMA. DRQ asks for the bus (in DMA mode only: the chip never raises DRQ with ND set), and
// each granted step moves one byte at the counter, which then counts up. The command ends
// at its EOT, DRQ drops, and the run loop gives the bus back.
// ---------------------------------------------------------------------------
bool CompuProDiskBoard::requestsBus() const {
    return chip_.drq();
}

StepResult CompuProDiskBoard::transferOne(Bus& bus) {
    if (!chip_.drq()) return {0, RunStatus::Ok};
    const uint16_t addr = (uint16_t)(dma_ & 0xFFFFu);  // A16-A23 are not on this bus
    if (chip_.toMemory()) {
        uint8_t v = 0;
        if (!chip_.dackRead(v)) {
            refresh();
            return {0, RunStatus::Ok};
        }
        bus.memWrite(addr, v);
    } else {
        chip_.dackWrite(bus.memRead(addr));
    }
    dma_ = (dma_ + 1) & 0xFFFFFFu;
    refresh();
    return {kDmaCycle, RunStatus::Ok};
}

// ---------------------------------------------------------------------------
// Reset and power. RESET* reaches the 765's RST pin and turns the boot ROM back on (§3).
// ---------------------------------------------------------------------------
void CompuProDiskBoard::reset(Reset r) {
    if (!romOn_) {
        romOn_ = true;
        decodeChanged();
    }
    resetRegisters();
    applyDataRate();
    if (r == Reset::PowerOn) chip_.powerOn();
    else                     chip_.reset();
    refresh();
}

void CompuProDiskBoard::power() {
    std::string err;
    if (!loadRom(romSpec_, err)) log_.push_back(err);
    reset(Reset::PowerOn);
}

void CompuProDiskBoard::pump() { refresh(); }

void CompuProDiskBoard::configChanged() {
    applyDataRate();
    decodeChanged();
    refresh();
}

// ---------------------------------------------------------------------------
// The EPROM. `builtin:<name>` travels the ROM registry; a file is Intel HEX, S-records or a
// flat binary, placed at 0. The whole EPROM is kept and `routine` picks the page, so moving
// the switch needs no reload.
// ---------------------------------------------------------------------------
bool CompuProDiskBoard::loadRom(const std::string& spec, std::string& err) {
    Image img;
    if (spec.rfind("builtin:", 0) == 0) {
        const BuiltinRom* rom = findRom(spec.substr(8));
        if (!rom) {
            err = "no built-in ROM named '" + spec.substr(8) + "'. SHOW ROMS lists them.";
            return false;
        }
        if (!decodeRom(*rom, 0, img, err)) return false;
    } else {
        std::ifstream f(resolvePath(spec), std::ios::binary);
        if (!f) {
            err = "cannot open '" + spec + "'" + pathNote(spec);
            return false;
        }
        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (looksLikeHex(raw)) {
            if (!loadHex(raw, img, err)) { err = spec + ": " + err; return false; }
        } else if (looksLikeSrec(raw)) {
            if (!loadSrec(raw, img, err)) { err = spec + ": " + err; return false; }
        } else {
            loadBin(raw, 0, img);
        }
    }
    if (img.empty()) {
        err = spec + ": no bytes";
        return false;
    }
    if (img.bytes.rbegin()->first >= traits_.romBytes) {
        char last[8];
        std::snprintf(last, sizeof last, "%04X", (unsigned)(traits_.romBytes - 1));
        err = spec + ": runs past " + last + ". The " + traits_.name + "'s EPROM is " +
              traits_.romPart + ".";
        return false;
    }
    rom_.assign(traits_.romBytes, 0xFF);
    for (const auto& [a, b] : img.bytes) rom_[a] = b;
    return true;
}

// ---------------------------------------------------------------------------
// Properties: the straps both boards have, the leaf's own, then the interrupt jumper.
// ---------------------------------------------------------------------------
std::vector<Property> CompuProDiskBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = std::string("Base address (") + traits_.baseSwitch +
                  "). The board decodes four ports: BASE+0 .. BASE+3 (default C0)";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xFC;
        x.get   = [this] { return Value::ofInt(port_); };
        x.set   = [this](const Value& v, std::string& err) {
            if (v.i() % 4) {
                err = "the base is a multiple of 4: the switches set A7-A2 only";
                return false;
            }
            port_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "rom";
        x.help = traits_.romHelp;
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(romSpec_); };
        x.set  = [this](const Value& v, std::string& err) {
            if (!loadRom(v.s(), err)) return false;
            romSpec_ = v.s();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "routine";
        x.help  = traits_.routineHelp;
        x.kind  = Kind::Int;
        x.radix = 10;
        x.min   = 0;
        x.max   = (long long)(traits_.romBytes / traits_.romWindow) - 1;
        x.get   = [this] { return Value::ofInt(routine_); };
        x.set   = [this](const Value& v, std::string&) {
            routine_ = (int)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "boot";
        x.help = traits_.bootHelp;
        x.kind = Kind::Bool;
        x.get  = [this] { return Value::ofBool(boot_); };
        x.set  = [this](const Value& v, std::string&) {
            boot_ = v.b();
            return true;
        };
        p.push_back(std::move(x));
    }
    boardProperties(p);
    return p;
}

std::vector<MapEntry> CompuProDiskBoard::memMap() const {
    if (!boot_) return {};
    return {{0x0000, traits_.romWindow - 1, "boot ROM",
             "one " + std::to_string(traits_.romWindow) +
                 "-byte routine of the EPROM, over PHANTOM* until OUT BASE+3 with D0 = 0"}};
}

// ---------------------------------------------------------------------------
// Units, MOUNT, UNMOUNT, and [[board.drive]].
// ---------------------------------------------------------------------------
std::vector<UnitDef> CompuProDiskBoard::units() const {
    std::vector<UnitDef> u;
    for (int i = 0; i < kDrives; ++i) {
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

bool CompuProDiskBoard::mount(const std::string& unit, const std::string& path, bool ro,
                              std::string& err) {
    const int i = driveIndex(unit);
    if (i < 0) {
        err = "no unit `" + unit + "` on " + id + " (it has drive0..drive3)";
        return false;
    }
    auto media = openMedia(resolvePath(path), ro, err);
    if (!media) {
        err += pathNote(path);
        return false;
    }
    auto img = std::make_unique<DiskImage>(std::move(media));
    bool eight = true;
    if (!probe(*img, drive_[(size_t)i].forced, eight, err)) return false;  // the old disk stays in
    img->setExtendsOnWrite(true);  // a blank grows as the guest formats it

    Drive& d = drive_[(size_t)i];
    d.img       = std::move(img);
    d.path      = path;
    d.eightInch = eight;
    d.drv.mount(d.img.get(), ro);
    d.drv.setDebug([this] { return (const dbg::Channel*)debugChannel(); }, kFaultDebugBit,
                   "drive" + std::to_string(i));
    d.drv.setHeadTrack(0);
    d.drv.setFormatting(true);
    d.drv.setRevsPerSecond(eight ? 6 : 5);
    if (d.img->readOnlyForced()) {
        char m[192];
        std::snprintf(m, sizeof m, "drive%d mounted WRITE-PROTECTED -- the host will not let us write %s",
                      i, path.c_str());
        log_.push_back(m);
    }
    return true;
}

bool CompuProDiskBoard::unmount(const std::string& unit, std::string& err) {
    const int i = driveIndex(unit);
    if (i < 0) {
        err = "no unit `" + unit + "` on " + id;
        return false;
    }
    Drive& d = drive_[(size_t)i];
    if (!d.img) {
        err = id + ":" + unit + " is empty";
        return false;
    }
    d.img->sync();
    d.drv.eject();
    d.img.reset();
    d.path.clear();
    d.eightInch = true;
    return true;
}

std::vector<Property> CompuProDiskBoard::subUnitProperties(const std::string& table) const {
    if (table != "drive") return {};
    std::vector<Property> p;
    {
        Property x;
        x.name  = "unit";
        x.help  = "Which drive (0..3)";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.min   = 0;
        x.max   = kDrives - 1;
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
    p.push_back(mediaKey());
    {
        Property x;
        x.name = "create";
        x.help = "Make an empty disk file if it is not there, then mount it as a blank disk. "
                 "A blank needs `media`";
        x.kind = Kind::Bool;
        p.push_back(std::move(x));
    }
    p.push_back(driveFaultsKey());
    return p;
}

// `media`, the one key that shapes a disk before it goes in: a [[board.drive]] key, and a
// MOUNT option (mountKeys).
Property CompuProDiskBoard::mediaKey() const {
    Property x;
    x.name = "media";
    x.help = "The diskette: its size and sides, such as 8ds, or a whole format, such as "
             "8ds-dd1024. A blank disk needs it. Without it, the size of the image decides";
    x.kind = Kind::Enum;
    for (const Media& m : allMedia())
        if (m.eightInch ? takes8() : takes525()) x.choices.push_back(m.name);
    return x;
}

std::vector<Property> CompuProDiskBoard::mountKeys(const std::string& unit) const {
    if (driveIndex(unit) < 0) return {};
    return {mediaKey()};
}

// The media goes with the disk: a mount that is refused leaves the old disk and its media.
bool CompuProDiskBoard::mountWith(const std::string& unit, const std::string& path, bool ro,
                                  const KeyValues& keys, std::string& err) {
    const int i = driveIndex(unit);
    if (i < 0) return mount(unit, path, ro, err);  // mount() names the units
    std::string& forced = drive_[(size_t)i].forced;
    const std::string was = forced;
    for (const auto& [k, v] : keys)
        if (k == "media") {
            const Media* m = findMedia(v);  // the schema has checked it
            forced = m ? m->name : v;
        }
    if (mount(unit, path, ro, err)) return true;
    forced = was;
    return false;
}

bool CompuProDiskBoard::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
    if (table != "drive") {
        err = type() + " has no [[board." + table + "]] table";
        return false;
    }
    int         unit = -1;
    std::string path, media;
    std::string faults;
    bool        ro = false, create = false;
    auto flag = [](const std::string& v) {
        Value       bv;
        std::string e;
        return parseValue(v, Kind::Bool, bv, e) && bv.b();
    };
    for (const auto& [k, v] : kv) {
        if (k == "unit") unit = std::stoi(v);
        else if (k == "mount") path = v;
        else if (k == "faults") faults = v;
        else if (k == "readonly") ro = flag(v);
        else if (k == "create") create = flag(v);
        else if (k == "media") media = v;
    }
    if (unit < 0) {
        err = "[[board.drive]] needs a `unit`";
        return false;
    }
    if (path.empty() && !faults.empty()) {
        err = "`faults` needs a disk: give `mount` in the same table";
        return false;
    }
    if (path.empty()) {
        const Media* m = findMedia(media);
        drive_[(size_t)unit].forced = m ? m->name : media;
        return true;
    }
    // `create`: an empty file at the place mount() opens, never over a file that is there.
    if (create) {
        const std::string rp = resolvePath(path);
        std::error_code   ec;
        if (!std::filesystem::exists(rp, ec)) {
            std::string cerr;
            if (!writeHostFile(rp, {}, cerr)) {
                err = cerr + pathNote(path);
                return false;
            }
        }
    }
    // Always passed, empty too: a table with no `media` is a disk the size decides.
    if (!mountWith("drive" + std::to_string(unit), path, ro, {{"media", media}}, err)) return false;
    return applyDriveFaults(drive_[(size_t)unit].drv, faults, err);
}

std::vector<Board::SubUnit> CompuProDiskBoard::subUnits() const {
    std::vector<SubUnit> out;
    for (int i = 0; i < kDrives; ++i) {
        const Drive& d = drive_[(size_t)i];
        if (!d.img && d.forced.empty()) continue;
        SubUnit su;
        su.table = "drive";
        su.fields.push_back({"unit", std::to_string(i), false});
        if (!d.forced.empty()) su.fields.push_back({"media", d.forced, true});
        if (d.img) {
            su.fields.push_back({"mount", d.path, true});
            if (d.img->readOnly()) su.fields.push_back({"readonly", "true", false});
            if (!d.drv.faults().empty()) su.fields.push_back({"faults", d.drv.faults().toText(), true});
        }
        out.push_back(std::move(su));
    }
    return out;
}

std::vector<std::string> CompuProDiskBoard::drainLog() {
    std::vector<std::string> out = std::move(log_);
    log_.clear();
    for (auto& s : out) s = id + ": " + s;
    return out;
}

void CompuProDiskBoard::serializeHeads(StateWriter& w) const {
    for (const Drive& d : drive_) {
        w.u32((uint32_t)d.drv.headTrackRaw());
        w.u8((uint8_t)d.drv.side());
        d.drv.faults().serialize(w);  // the bad sectors and their counts (#966)
    }
}

void CompuProDiskBoard::deserializeHeads(StateReader& r) {
    for (Drive& d : drive_) {
        d.drv.setHeadTrack((int)r.u32());
        d.drv.setSide(r.u8() & 1);
        d.drv.faults().deserialize(r);
    }
}

bool CompuProDiskBoard::takesFaults(const std::string& unit) const { return driveIndex(unit) >= 0; }

DiskFaults* CompuProDiskBoard::faults(const std::string& unit) {
    const int i = driveIndex(unit);
    return (i >= 0 && drive_[(size_t)i].img) ? &drive_[(size_t)i].drv.faults() : nullptr;
}


} // namespace altair
