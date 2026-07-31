#include "boards/cromemco-fdc.h"

#include "core/bus.h"
#include "core/clock.h"
#include "core/roms.h"
#include "core/statefile.h"
#include "core/value.h"
#include "host/endpoint.h"
#include "host/media.h"
#include "host/stream.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace altair {

namespace {

EndpointResolver g_resolver;

// An unclocked card is a chip with no crystal: it cannot time a seek or pace a byte, so it
// reads dead rather than dereferencing a null Clock. (Same idiom as the Tarbell/SBC.)
Clock& deadCard() {
    static Clock stopped;
    return stopped;
}

int driveIndex(const std::string& unit, int count) {
    if (unit.rfind("drive", 0) != 0) return -1;
    const std::string n = unit.substr(5);
    if (n.empty()) return -1;
    for (char ch : n)
        if (ch < '0' || ch > '9') return -1;
    int i = std::stoi(n);
    return (i >= 0 && i < count) ? i : -1;
}

} // namespace

void CromemcoFdcBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

Clock& CromemcoFdcBoard::clk() const { return clock_ ? *clock_ : deadCard(); }

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
CromemcoFdcBoard::CromemcoFdcBoard() {
    drive_.resize((size_t)drives_);
    uart_.disconnect();  // -> NullStream: a card with nothing plugged in has a DEAD line
    buildFdc();          // virtual, but the base answer (FD1793) is what 16/64 want
}

CromemcoFdcBoard::~CromemcoFdcBoard() {
    // The Clock is holding a lambda with `this` in it; a card can be pulled from a running
    // machine, and a deadline firing into a freed board is a use-after-free.
    if (clock_) clock_->cancel(wake_);
}

// THE PART IS THE GENERATION. 16FDC/64FDC carry an FD1793 (single + double density). NOT
// wait-synced: the Cromemco data port (33) never wait-states and DRQ is a readable bit
// (reference §6) -- Auto Wait is armed per-access on port 34 only (see readPort34).
void CromemcoFdcBoard::buildFdc() {
    chip_ = std::make_unique<Wd1791>("fdc");
    chip_->setWaitSynced(false);
    if (clock_) chip_->powerOn(*clock_);
    applySelection();
}

// Point the chip at the selected drive and re-apply side and media data rate. The
// drive-select latch (port-34 D3-D0) hands us a straight drive index (writePort34).
void CromemcoFdcBoard::applySelection() {
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
// The bus. Three I/O windows (00-09 serial, 30-34 disk, 40 bank-select) and the RDOS
// PROM's memory reads while it is armed. The ports are HARD-DECODED -- unlike the TU-ART
// or VersaFloppy, all three FDC boards fix these addresses (reference §2).
// ---------------------------------------------------------------------------
bool CromemcoFdcBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;

    if (c.type == Cycle::IoRead || c.type == Cycle::IoWrite) {
        uint8_t p = c.port();
        if (p <= 0x09) return true;                       // 00-09 TMS 5501
        if (p >= 0x30 && p <= 0x34) return true;          // 30-34 disk
        if (p == 0x40 && hasBankSelect()) return true;    // 40 bank-select (16/64 only)
        return false;
    }

    // The RDOS PROM answers memory reads in its window while armed. It is a PLAIN memory
    // decode the board owns outright -- NOT the Tarbell's low-memory PHANTOM* shadow -- so
    // there is no assertsPhantom/snoop here. A write falls through to RAM (an EPROM socket
    // ignores writes), so we do not decode MemWrite.
    if (c.type == Cycle::MemRead) return inRomWindow(c.addr);
    return false;
}

uint8_t CromemcoFdcBoard::read(const BusCycle& c) {
    if (c.type == Cycle::MemRead) return rom_[c.addr - kRomBase];  // only reached in-window

    Clock&  k = clk();
    uint8_t p = c.port();
    uint8_t v = 0xFF;
    switch (p) {
        // ---- TMS 5501 (00-09) ----
        case 0x00: v = uart_.readStatus(k);   break;
        case 0x01: v = uart_.readData(k);     break;
        case 0x02: v = 0xFF;                  break;  // IN 02 not assigned
        case 0x03: v = uart_.readIntAddr();   break;  // inert (0xFF)
        case 0x04: v = uart_.readParallel();  break;  // parallel in / aux disk status (inert)
        // ---- FD1793 (30-33) + Cromemco disk flags (34) ----
        case 0x30: v = chip_->readStatus(k);    break;
        case 0x31: v = chip_->readTrackReg();   break;
        case 0x32: v = chip_->readSectorReg();  break;
        case 0x33: v = chip_->readData(k);      break;  // NEVER wait-synced (reference §6)
        case 0x34: v = readPort34();            break;
        default:   v = 0xFF;                    break;  // 05-09 not connected, 40 IN not assigned
    }
    refresh();
    return v;
}

void CromemcoFdcBoard::write(const BusCycle& c) {
    if (c.type != Cycle::IoWrite) return;  // a MemWrite in the ROM window falls through to RAM

    Clock&  k = clk();
    uint8_t p = c.port();
    uint8_t v = c.data;
    switch (p) {
        // ---- TMS 5501 (00-09) ----
        case 0x00: uart_.writeBaud(v);       break;
        case 0x01: uart_.writeData(v, k);    break;
        case 0x02: uart_.writeCommand(v, k); break;
        case 0x03: uart_.writeMask(v);       break;  // inert
        case 0x04: uart_.writeParallel(v);   break;  // parallel out / aux disk command (inert)
        case 0x05: case 0x06: case 0x07:
        case 0x08: case 0x09: uart_.writeTimer(p - 0x05, v); break;  // inert
        // ---- FD1793 (30-33) + Cromemco disk control (34) ----
        case 0x30: chip_->writeCommand(v, k);  break;
        case 0x31: chip_->writeTrackReg(v);    break;
        case 0x32: chip_->writeSectorReg(v);   break;
        case 0x33: chip_->writeData(v, k);     break;
        case 0x34: writePort34(v);             break;
        // ---- bank-select (40) ----
        case 0x40:
            // OUT 40H (any byte) banks the RDOS ROM out until the next RESET. Only the 16FDC/
            // 64FDC decode this port; on the 4FDC hasBankSelect() is false and decodes() never
            // routes a write here.
            if (hasBankSelect() && armed_) {
                armed_ = false;
                decodeChanged();
            }
            break;
        default: break;
    }
    refresh();
}

// ---------------------------------------------------------------------------
// Port 34 -- disk flags (IN) and disk control (OUT). The 16/64 layout (reference §4).
// ---------------------------------------------------------------------------

// IN 34: DRQ (D7), ¬BOOT (D6, low when jumpered to BOOT), EOJ (D0). The motor/timeout/
// select-request bits are inert here and read 0. AUTO WAIT: if the guest armed it (the OUT
// 34 latch's D7), this read is the CPU-stall path -- flip the chip wait-synced for the
// length of the read so the in-flight command resolves to its next DRQ (or completion),
// exactly as a CPU stalled on the wait-state generator would see. Port 33 stays DRQ-polled.
uint8_t CromemcoFdcBoard::readPort34() {
    Clock&     k        = clk();
    const bool autoWait = (control_ & 0x80) != 0;
    if (autoWait) chip_->setWaitSynced(true);
    chip_->poll(k);
    uint8_t v = (uint8_t)((chip_->drq() ? 0x80 : 0x00) |
                          (bootstrap_ ? 0x00 : 0x40) |
                          (chip_->intrq() ? 0x01 : 0x00));
    if (autoWait) chip_->setWaitSynced(false);
    return v;
}

// OUT 34: AUTO WAIT (D7), DOUBLE DENSITY (D6), MOTOR ON (D5), MAXI (D4), DS4-DS1 (D3-D0).
void CromemcoFdcBoard::writePort34(uint8_t v) {
    control_ = v;
    maxi_ = (v & 0x10) != 0;             // D4: MAXI -> 8" (true) / 5.25" (false)
    const bool dden = (v & 0x40) != 0;   // D6: double density

    // DATA RATE IS MAXI x DDEN, NOT DDEN ALONE (reference §1, the RCLK table keyed on
    // MAXI/DDEN): ONLY 8" double density is 500 kbit/s; 8" SD, 5.25" SD and 5.25" DD are
    // all 250 kbit/s. A naive "D6 -> 250/500" mis-clocks every 5.25" DD disk -- the likely
    // real-world break behind Joe's double-density failures, and the diagnostic value here.
    dataRate_ = (maxi_ && dden) ? 500000 : 250000;

    // D3-D0: one-hot drive select DS4-DS1. The lowest set bit picks the drive; no bit set
    // leaves the selection unchanged, so the boot loader (which never writes port 34) runs
    // against the default drive 0.
    for (int i = 0; i < 4; ++i)
        if (v & (1 << i)) { sel_ = i; break; }

    applySelection();
}

// ---------------------------------------------------------------------------
// The card's own clock discipline (the SBC/Tarbell idiom): advance both chips, re-drive the
// interrupt wire, re-arm the one deadline for the next moment a chip changes on its own.
// ---------------------------------------------------------------------------
void CromemcoFdcBoard::refresh() {
    if (!clock_) return;
    chip_->poll(*clock_);
    uart_.poll(*clock_);
    intChanged();
    clock_->cancel(wake_);
    wake_ = Clock::kNone;
    if (uint64_t e = nextEdge()) wake_ = clock_->at(e, [this] { refresh(); });
}

uint64_t CromemcoFdcBoard::nextEdge() const {
    const Clock& k = clk();
    uint64_t best = 0;
    auto consider = [&](uint64_t w) {
        if (!w || w <= k.now()) return;
        if (!best || w < best) best = w;
    };
    if (chip_) consider(chip_->nextEdge(k));
    consider(uart_.nextEdge(k));  // 0 in Phase 1 (no timer, no receive interrupt)
    return best;
}

void CromemcoFdcBoard::reset(Reset r) {
    // RESET re-arms the boot ROM (the machine comes up with it mapped). Independent of the
    // clock -- do it first.
    if (bootstrap_ && !armed_) {
        armed_ = true;
        decodeChanged();
    }
    if (!clock_) return;
    if (r == Reset::PowerOn) {
        chip_->powerOn(*clock_);
        uart_.powerOn(*clock_);
    }
    // S-100 RESET* reaches the FD1793's MR (an auto-Restore that homes the head) and the
    // 5501's RESET* (clears the receiver, sets TBE) -- so the head homes and the console
    // comes up with no software help, which is what RDOS's cold start relies on.
    chip_->masterReset(*clock_);
    uart_.reset(*clock_);
    refresh();
}

void CromemcoFdcBoard::power() {
    loadRom();
    reset(Reset::PowerOn);
}

void CromemcoFdcBoard::pump() {
    uart_.pump();
    refresh();
}

void CromemcoFdcBoard::configChanged() {
    decodeChanged();      // `bootstrap` changed the ROM decode
    uart_.programLine();  // a `baud` restrap moved the console's character time
    refresh();
}

// ---------------------------------------------------------------------------
// The RDOS boot PROM. `builtin:rdos252` / `rdos312` travel the same Intel HEX/BIN parser as
// a memory card's ROM region -- the same loader the SBC and Turnkey use. The bytes land in
// the 4K window at C000; anything the decode places outside it is ignored.
// ---------------------------------------------------------------------------
void CromemcoFdcBoard::loadRom() {
    rom_.assign((size_t)romBytes(), (uint8_t)0xFF);
    const BuiltinRom* rom = findRom(romName());
    if (!rom) {
        // drainLog() prepends the board id, so the message here carries none (the same
        // convention the chips' drainLog messages follow).
        log_.push_back("built-in ROM '" + romName() + "' is missing. SHOW ROMS lists them.");
        return;
    }
    Image       img;
    std::string err;
    if (!decodeRom(*rom, kRomBase, img, err)) {
        log_.push_back(romName() + ": " + err);
        return;
    }
    for (const auto& [a, b] : img.bytes)
        if (a >= kRomBase && (uint32_t)a < (uint32_t)kRomBase + rom_.size())
            rom_[a - kRomBase] = b;
}

// ---------------------------------------------------------------------------
// Geometry probes. The BOARD probes (not DiskImage): the same byte count means different
// geometries on different controllers (DESIGN.md 7.3). The 16/64 read both densities, so
// the probe is a superset expressed as a per-track/per-side FmtRange list.
// ---------------------------------------------------------------------------
bool CromemcoFdcBoard::describeGeometry(uint64_t bytes, int& tracks, int& heads,
                                        bool& interleaved, int& revsPerSec,
                                        std::vector<FmtRange>& ranges, std::string& err) const {
    // 8" mixed-density CDOS 2.58 (the showcase, the DD path Joe's failures point at): a DD
    // track 0 side 0, an SD boot side (track 0 side 1, the side RDOS reads first), then DD
    // everywhere else. 77 tracks, double-sided, 8"/360 RPM.
    const uint64_t t0    = 16ull * 512 + 26ull * 128;       // 11,520
    const uint64_t cdos8 = t0 + 76ull * 2 * 16 * 512;       // 1,256,704
    if (sizeMatches(bytes, cdos8)) {
        tracks      = 77;
        heads       = 2;
        interleaved = false;
        revsPerSec  = 6;  // 8" / 360 RPM
        ranges = {{0, 0, 0, 0, Density::DD, 16, 512, 1},
                  {0, 0, 1, 1, Density::SD, 26, 128, 1},
                  {1, 76, 0, 1, Density::DD, 16, 512, 1}};
        return true;
    }

    // A plain 8" single-density disk: all 77 tracks SD, 26 x 128. The FD1793 reads SD media
    // too, so recognize the exact size before the blank fallback.
    const uint64_t sd8 = 77ull * 26 * 128;  // 256,256
    if (sizeMatches(bytes, sd8)) {
        tracks      = 77;
        heads       = 1;
        interleaved = false;
        revsPerSec  = 6;
        ranges = {{0, 76, 0, 0, Density::SD, 26, 128, 1}};
        return true;
    }

    // BLANK / SHORT -> an UNFORMATTED disk, not an error (the Tarbell rule): mount it at the
    // 8" track count with EMPTY geometry. The drive is READY and steppable, but every access
    // RNFs until the guest's DFORMAT streams a track (Write Track -> setTrackFormat, density
    // from the OUT-34 DD bit). This is what makes MOUNT ... CREATE (a 0-byte file) formattable.
    //
    // The 5.25" mixed/DSDD CDOS geometries (and the swapped-sides 8" variant) land here as a
    // forced media= choice once their exact per-track split is confirmed against the real
    // images -- a task-5 follow-up. For now the 8" showcase + a plain SD disk + a formattable
    // blank prove the mixed-density Write-Track path; the rest is added with the media in hand.
    if (bytes < cdos8) {
        tracks      = 77;
        heads       = 2;
        interleaved = false;
        revsPerSec  = 6;
        ranges.clear();
        return true;
    }

    err = std::to_string(bytes) + " bytes is too large for a Cromemco 8\" disk (" +
          std::to_string(cdos8) + " = the mixed-density CDOS image).";
    return false;
}

// ---------------------------------------------------------------------------
// Reflection
// ---------------------------------------------------------------------------
std::vector<Property> CromemcoFdcBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name = "bootstrap";
        x.help = "The BOOT/MON strap. On (default): the RDOS ROM is mapped at C000 and ¬BOOT "
                 "reads low, so RDOS boots the disk. Off: the ROM still answers but ¬BOOT reads "
                 "high (the monitor prompt instead of an auto-boot)";
        x.kind = Kind::Bool;
        x.get  = [this] { return Value::ofBool(bootstrap_); };
        x.set  = [this](const Value& v, std::string&) {
            bootstrap_ = v.b();
            decodeChanged();  // inRomWindow() reads bootstrap_
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "drives";
        x.help  = "Drives on the controller (A-D, one-hot select DS4-DS1)";
        x.kind  = Kind::Int;
        x.radix = 10;  // a count, never on the wire -> decimal
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
    return p;
}

std::vector<MapEntry> CromemcoFdcBoard::ioMap() const {
    std::vector<MapEntry> m = {
        {0x00, 0x00, "read/write", "TMS 5501 -- status / baud"},
        {0x01, 0x01, "read/write", "TMS 5501 -- receive / transmit"},
        {0x02, 0x02, "write",      "TMS 5501 -- command"},
        {0x03, 0x09, "write",      "TMS 5501 -- interrupt mask / timers 1-5 (inert in Phase 1)"},
        {0x30, 0x30, "read/write", "FD1793 -- status / command"},
        {0x31, 0x31, "read/write", "FD1793 -- track"},
        {0x32, 0x32, "read/write", "FD1793 -- sector"},
        {0x33, 0x33, "read/write", "FD1793 -- data (never wait-state synced)"},
        {0x34, 0x34, "read/write", "disk flags (DRQ/¬BOOT/EOJ) / control (Auto-Wait/density/MAXI/select)"},
    };
    if (hasBankSelect())
        m.push_back({0x40, 0x40, "write", "bank-select: OUT 40H banks the RDOS ROM out until RESET"});
    return m;
}

std::vector<MapEntry> CromemcoFdcBoard::memMap() const {
    if (!bootstrap_) return {};
    return {{(uint32_t)kRomBase, (uint32_t)kRomBase + (uint32_t)romBytes() - 1, "read",
             "RDOS boot PROM (banked out by OUT 40H, restored by RESET)"}};
}

// ---------------------------------------------------------------------------
// The console serial unit ('tty'), delegated to the embedded TMS 5501 (the SBC idiom).
// ---------------------------------------------------------------------------
std::vector<UnitDef> CromemcoFdcBoard::units() const {
    std::vector<UnitDef> u;
    u.push_back({"tty", UnitKind::Serial, uart_.endpoint()});
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

std::vector<Property> CromemcoFdcBoard::unitProperties(const std::string& unit) {
    if (unit != "tty") return {};
    // The 5501's own `connect` property setter opens the endpoint, so it must rebase a
    // relative in:/out: PATH from a machine file, exactly as the card's connect() does.
    return uart_.properties(
        rebasingResolver(g_resolver, [this](const std::string& p) { return resolvePath(p); }));
}

bool CromemcoFdcBoard::connect(const std::string& unit, const std::string& ep, std::string& err) {
    if (unit != "tty") {
        err = type() + " has no serial unit '" + unit + "' -- the console is 'tty' (the disks "
              "are drive0..drive" + std::to_string(drives_ - 1) + ", via MOUNT)";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    std::vector<std::string> paths;
    std::string              spec = rebaseEndpointPaths(ep, [&](const std::string& p) {
        paths.push_back(p);
        return resolvePath(p);
    });
    auto s = g_resolver(spec, err);
    if (!s) {
        for (const std::string& p : paths) err += pathNote(p);
        return false;
    }
    uart_.connect(std::move(s));
    refresh();  // a new line, and it may already have something waiting on it
    return true;
}

bool CromemcoFdcBoard::disconnect(const std::string& unit, std::string& err) {
    if (unit != "tty") {
        err = type() + " has no serial unit '" + unit + "'";
        return false;
    }
    uart_.disconnect();
    refresh();
    return true;
}

// ---------------------------------------------------------------------------
// Units, MOUNT, UNMOUNT, and the [[board.drive]] sub-unit table (the Tarbell shape).
// ---------------------------------------------------------------------------
bool CromemcoFdcBoard::mount(const std::string& unit, const std::string& path, bool ro,
                             std::string& err) {
    int i = driveIndex(unit, drives_);
    if (i < 0) {
        err = "no unit `" + unit + "` on " + id + " (it has drive0.." +
              std::to_string(drives_ - 1) + ", and the serial console 'tty')";
        return false;
    }

    auto media = openMedia(resolvePath(path), ro, err);
    if (!media) { err += pathNote(path); return false; }

    auto img = std::make_unique<DiskImage>(std::move(media));

    int  tracks = 0, heads = 0, revsPerSec = 6;
    bool interleaved = false;
    std::vector<FmtRange> ranges;
    if (!describeGeometry(img->size(), tracks, heads, interleaved, revsPerSec, ranges, err))
        return false;  // a failed probe leaves the old disk in place

    img->init(tracks, heads, interleaved);
    for (const FmtRange& fr : ranges)
        img->initFormat(fr.trackLo, fr.trackHi, fr.headLo, fr.headHi, fr.density, fr.sectors,
                        fr.sectorSize, fr.startSector);

    const bool forcedRo = img->readOnlyForced();
    img->setExtendsOnWrite(true);  // a blank grows as the guest's DFORMAT streams each track

    Drive& d = drive_[(size_t)i];
    d.img  = std::move(img);
    d.path = path;
    d.drv.mount(d.img.get(), ro);
    d.drv.setHeadTrack(0);
    d.drv.setFormatting(true);        // both densities format via Write Track
    d.drv.setRevsPerSecond(revsPerSec);  // 8" = 6 rev/s, 5.25" = 5 -- the Write-Track budget
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

bool CromemcoFdcBoard::unmount(const std::string& unit, std::string& err) {
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

std::vector<Property> CromemcoFdcBoard::subUnitProperties(const std::string& table) const {
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

bool CromemcoFdcBoard::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
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

std::vector<Board::SubUnit> CromemcoFdcBoard::subUnits() const {
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

std::vector<std::string> CromemcoFdcBoard::drainLog() {
    std::vector<std::string> out = std::move(log_);
    log_.clear();
    for (auto& s : uart_.drainLog()) out.push_back(std::move(s));
    if (chip_)
        for (auto& s : chip_->drainLog()) out.push_back(std::move(s));
    for (auto& s : out) s = id + ":" + s;
    return out;
}

// ---------------------------------------------------------------------------
// SNAPSHOT / RESTORE (DESIGN.md 13). The controller state that is NOT host-backed: both
// chips' register files and any command in flight, the drive-select/side/rate/control
// latches, each drive's head position, and the ROM flip-flop (armed_ -- a runtime latch
// that must travel). The disk IMAGES are host-backed; `bootstrap_` is a strap and is not
// serialized (a snapshot restores into a machine built from the same config).
// ---------------------------------------------------------------------------
void CromemcoFdcBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u8((uint8_t)sel_);
    w.u8((uint8_t)side_);
    w.boolean(maxi_);
    w.u32((uint32_t)dataRate_);
    w.u8(control_);
    w.boolean(armed_);
    w.u32((uint32_t)drive_.size());
    for (const Drive& d : drive_) w.u32((uint32_t)d.drv.headTrackRaw());
    chip_->serialize(w);
    uart_.serialize(w);
}

void CromemcoFdcBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    sel_      = (int)r.u8();
    side_     = (int)r.u8();
    maxi_     = r.boolean();
    dataRate_ = (long long)r.u32();
    control_  = r.u8();
    armed_    = r.boolean();
    uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; ++i) {
        int head = (int)r.u32();
        if (i < drive_.size()) drive_[i].drv.setHeadTrack(head);
    }
    chip_->deserialize(r);
    uart_.deserialize(r);
    applySelection();  // re-attach the selected drive and re-apply side/rate
    refresh();         // re-drive the wires from the restored chip state
    decodeChanged();   // armed_ may have changed the ROM decode
}

} // namespace altair
