#include "boards/northstar-mds.h"

#include "boards/floppy-drive.h"
#include "core/bus.h"
#include "core/clock.h"
#include "core/hex.h"
#include "core/roms.h"
#include "core/statefile.h"
#include "host/media.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace altair {
namespace {

// THE WINDOW. "A read or write command must be issued to the controller within a 96
// microsecond window after the sector pulse" (MDS-A-D manual, Theory of Operation 6).
// On the MDS-A it is a counter preset: "sets WINDOW width to 12 bit times" (schematic
// page 3), and a bit time is 8 us -- 96 again.
constexpr uint64_t kWindowUs = 96;

// BODY. Writing starts at the end of the window, and what is written is 16 bytes of
// zeros and one sync character at 64 us a byte (single density) or 32 bytes and two at
// 32 us a byte (double density). Both come to 1088 us, so the sync character has gone
// under the head 96 + 1088 us after the sector hole, on either board, at either density.
constexpr uint64_t kBodyUs = 96 + 1088;

// The MDS-A-D's read path, from its checkout table (manual, step C3): "RE ... goes HIGH
// at 480us" and "DDI ... goes HIGH at 512us (32us after RE)".
constexpr uint64_t kReadEnableUs = 480;
constexpr uint64_t kDdiUs        = 512;

// With no sector hole to see, the board's own counter chain supplies the pulse: 2 MHz
// divided by 2^16. The MDS-A names it HOLE NOT FOUND (schematic page 3); the MDS-A-D
// checkout table lists it as "CC15 ... 32ms square wave".
constexpr uint64_t kFreeRunUs = 32768;

// The check character: "setting it to zero and then exclusive ORing each successive data
// byte value with the current value of the check character and left cycling the result."
// The PROM does it as `xra b / rlc / mov b,a`. It is not a CRC.
uint8_t checkChar(const uint8_t* p, int n) {
    uint8_t c = 0;
    for (int i = 0; i < n; ++i) {
        c ^= p[i];
        c = (uint8_t)((c << 1) | (c >> 7));
    }
    return c;
}

// Side 2 is recorded from the inside out: North Star DOS numbers the tracks of a
// double-sided diskette 0-34 on the first side and 35-69 on the second, and track 35 is
// the INNERMOST track of side 2, so the head does not move when the side changes. The
// file is in that order.
int imageTrack(int track, int side) { return side ? (kNsTracks - 1 - track) : track; }

// "drive3" -> 3, or -1.
int driveIndex(const std::string& unit, int count) {
    if (unit.rfind("drive", 0) != 0) return -1;
    const std::string n = unit.substr(5);
    if (n.empty()) return -1;
    for (char ch : n)
        if (ch < '0' || ch > '9') return -1;
    int i = std::stoi(n);
    return (i >= 0 && i < count) ? i : -1;
}

bool truthy(const std::string& v) { return v == "true" || v == "1" || v == "yes" || v == "on"; }

} // namespace

NorthStarFdc::NorthStarFdc(const char* romName, uint16_t romOrigin, int drives)
    : romName_(romName), romOrigin_(romOrigin), drives_(drives) {
    drive_.resize((size_t)drives_);
    for (Drive& d : drive_)
        d.faults.refuse(FaultKind::Id,
                        "a North Star sector has no ID field. Give data, missing or write");
    spindle_.configure(300, kNsSectors);  // an SA-400: 200 ms a revolution, 20 ms a sector
    loadProm();
}

// The interrupt wake holds a lambda with `this` in it.
NorthStarFdc::~NorthStarFdc() {
    if (clock_) clock_->cancel(wake_);
}

// ---------------------------------------------------------------------------
// The bootstrap PROM. The built-in image carries its own addresses (E900 for the MDS-A's,
// E800 for the MDS-A-D's); the board holds it as 256 bytes addressed by A7..A0, which is
// all the PROM ever sees.
// ---------------------------------------------------------------------------
void NorthStarFdc::loadProm() {
    std::memset(prom_, 0xFF, sizeof prom_);
    const BuiltinRom* rom = findRom(romName_);
    if (!rom) {
        say(std::string("built-in ROM '") + romName_ + "' is missing. SHOW ROMS lists them.");
        return;
    }
    Image       img;
    std::string err;
    if (!decodeRom(*rom, 0, img, err)) {
        say(std::string(romName_) + ": " + err);
        return;
    }
    for (const auto& [a, b] : img.bytes)
        if (a >= romOrigin_ && a < (uint32_t)romOrigin_ + 256) prom_[a - romOrigin_] = b;
}

// ---------------------------------------------------------------------------
// Decode. A memory READ in the 1 K block, and nothing else: the board gates itself with
// sMEMR, so a write to the block goes to whatever else is there.
// ---------------------------------------------------------------------------
bool NorthStarFdc::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    return c.type == Cycle::MemRead && c.addr >= base_ && c.addr <= base_ + 0x3FFu;
}

uint8_t NorthStarFdc::read(const BusCycle& c) {
    const uint16_t off = (uint16_t)(c.addr - base_);
    tick();
    return access(off >> 8, (uint8_t)off);
}

// Only the PROM can be looked at without touching. Every other address in the block IS a
// command, so the honest answer for those is "not during a cycle" -- and DISASM, HISTORY
// and the breakpoint display, which are built on this, show FF there and move nothing.
bool NorthStarFdc::peek(uint16_t addr, uint8_t& out) const {
    if (!enabled_ || addr < base_ || addr > base_ + 0x3FFu) return false;
    const uint16_t off = (uint16_t)(addr - base_);
    if (!promCase(off >> 8)) return false;
    out = prom_[off & 0xFF];
    return true;
}

std::vector<MapEntry> NorthStarFdc::memMap() const {
    std::vector<MapEntry> m;
    for (uint32_t k = 0; k < 4; ++k)
        m.push_back({base_ + k * 0x100u, base_ + k * 0x100u + 0xFFu, promCase((int)k) ? "rom" : "read",
                     caseNote((int)k)});
    return m;
}

NorthStarFdc::Drive*       NorthStarFdc::selected()       { return sel_ < 0 ? nullptr : &drive_[(size_t)sel_]; }
const NorthStarFdc::Drive* NorthStarFdc::selected() const { return sel_ < 0 ? nullptr : &drive_[(size_t)sel_]; }

// A microsecond of WALL time, in T-states of whatever crystal the CPU has. The board has
// its own 4 MHz crystal, so its windows do not stretch for a faster processor.
uint64_t NorthStarFdc::tFromUs(uint64_t us) const {
    if (!clock_) return 1;
    uint64_t t = (uint64_t)clock_->hz() * us / 1000000;  // multiply FIRST
    return t ? t : 1;
}

uint64_t NorthStarFdc::now() const {
    if (!clock_) return ahead_;
    return clock_->now() > ahead_ ? clock_->now() : ahead_;
}

void NorthStarFdc::holdUntil(uint64_t t) {
    const uint64_t n = now();
    if (t <= n) return;  // the byte is already here: a late access waits for nothing
    const uint64_t d = t - n;
    holdReady(d > UINT32_MAX ? UINT32_MAX : (uint32_t)d);
    ahead_ = t;
}

uint64_t NorthStarFdc::byteT(bool dd) const { return tFromUs(dd ? 32 : 64); }

// ---------------------------------------------------------------------------
// WHERE THE DISK IS -- the one place rotation is computed, as a reading off the Clock.
// Nothing here advances anything: a counter that moved when the guest read a status byte
// would turn the disk at the speed of the loop that watches it.
//
// A diskette is under a selected head with the motors on: ten sector holes a revolution,
// 20 ms apart, and the index hole puts the counter at sector 0. Anything else, and the
// board is counting its own pulses -- which is why the PROM's "wait 50 sector times for
// the motor" works before any drive is selected.
// ---------------------------------------------------------------------------
NorthStarFdc::Rotation NorthStarFdc::where() const {
    Rotation r;
    if (!clock_) return r;
    const Drive* d = selected();
    r.real = motorOn_ && d && d->img;

    uint64_t per = r.real ? spindle_.tPerSector(*clock_) : tFromUs(kFreeRunUs);
    if (!per) per = 1;
    const uint64_t t = now();
    r.abs    = t / per;
    r.into   = t % per;
    r.sector = (int)(r.abs % (uint64_t)(r.real ? kNsSectors : freeRunModulo()));
    return r;
}

// Bring the lazily-kept state up to date: the motor's deadline, and the sector flag.
void NorthStarFdc::tick() {
    if (!clock_) return;

    if (motorOn_ && motorReal_ && now() >= motorOffAt_) {
        motorOn_ = false;
        onMotorOff();
    }

    const Rotation r = where();
    if (r.real != lastReal_) {
        // The pulses changed source (the motor stopped, a drive was selected). That is
        // not a sector hole going by, so the flag does not move; the count starts again.
        lastReal_ = r.real;
        lastAbs_  = r.abs;
        endSector();
    } else if (r.abs != lastAbs_) {
        lastAbs_ = r.abs;
        endSector();
        if (!sf_) {
            sf_ = true;
            intChanged();
        }
    }
}

// A sector pulse (the window) clears the write gate and body mode. A write that did not
// reach the end of its data is dropped: on the medium it would be a sector with no valid
// check character, and a payload-only image has no way to hold one.
void NorthStarFdc::endSector() {
    if (wr_ == Wr::Data && wrPos_ > 0) {
        if (auto* dc = debugChannel(); dc && dc->on(SECTOR))
            dbg::line(*dc) << "write cut short at " << wrPos_ << " bytes: track=" << wrTrack_
                           << " sector=" << wrSector_ << " -- dropped\n";
    }
    wr_         = Wr::Idle;
    wrPos_      = 0;
    rdAbs_      = ~0ull;
    rdValid_    = false;
    rdPos_      = 0;
    rdFault_    = -1;
    bodyForced_ = false;
}

// ---------------------------------------------------------------------------
// THE INTERRUPT. "An interrupt will be generated at every sector pulse while the
// interrupt is armed in the controller." The line is SECTOR FLAG and INT ARM (MDS-A
// schematic, page 1), so it stays down until software resets the sector flag.
//
// The sector flag is otherwise worked out when the guest looks. An interrupt cannot wait
// to be looked at, so while it is armed AND strapped the board is present at each pulse.
// With either missing there is no deadline at all, and an idle machine can stand down.
// ---------------------------------------------------------------------------
void NorthStarFdc::scheduleWake() {
    if (!clock_) return;
    clock_->cancel(wake_);
    wake_ = Clock::kNone;
    if (!armed_ || irq_ == IrqJumper::None) return;

    const Rotation r = where();
    uint64_t per = r.real ? spindle_.tPerSector(*clock_) : tFromUs(kFreeRunUs);
    if (!per) per = 1;
    wake_ = clock_->at(now() - r.into + per, [this] {
        wake_ = Clock::kNone;
        tick();
        scheduleWake();
    });
}

void NorthStarFdc::configChanged() {
    Board::configChanged();
    scheduleWake();
}

void NorthStarFdc::armInterrupt(bool on) {
    if (armed_ == on) return;
    armed_ = on;
    intChanged();
    scheduleWake();
}

void NorthStarFdc::resetSectorFlag() {
    if (!sf_) return;
    sf_ = false;
    intChanged();
}

// ---------------------------------------------------------------------------
// The motor. One line runs every drive's motor (MOTOR ON, pin 16).
// ---------------------------------------------------------------------------
void NorthStarFdc::motorSet() {
    motorOn_ = true;
    if (clock_) motorOffAt_ = now() + tFromUs(motorOffUs());
}

void NorthStarFdc::motorStop() { motorOn_ = false; }

void NorthStarFdc::selectDrive(int n) {
    if (n >= drives_) {
        char m[96];
        std::snprintf(m, sizeof m, "%s: no drive %d (this board has %d)", id.c_str(), n + 1, drives_);
        say(m);
        n = -1;
    }
    if (n == sel_) return;
    endSector();  // the head that was reading or writing is not this one
    sel_ = n;
}

void NorthStarFdc::step(bool in) {
    Drive* d = selected();
    if (!d) return;
    endSector();

    const int was = d->track;
    if (in) {
        if (d->track < kNsTracks - 1) d->track++;
    } else {
        if (d->track > 0) d->track--;  // the drive stops at track 0
    }
    if (auto* dc = debugChannel(); dc && dc->on(SEEK) && d->track != was)
        dbg::line(*dc) << "seek drive=" << sel_ + 1 << " track=" << was << " -> " << d->track << "\n";
}

void NorthStarFdc::controllerReset() {
    motorStop();
    selectDrive(-1);
    armInterrupt(false);
    endSector();
}

// POC always clears the controller. Whether the front-panel RESET does is the board's.
// The images stay mounted and the heads stay where they are.
void NorthStarFdc::reset(Reset r) {
    if (r == Reset::PowerOn) ahead_ = 0;  // power restarts the clock at zero
    if (resetsOn(r)) controllerReset();
    scheduleWake();
}

bool NorthStarFdc::track0() const {
    const Drive* d = selected();
    return d && d->track == 0;
}

bool NorthStarFdc::writeProtected() const {
    const Drive* d = selected();
    return d && d->img && d->img->readOnly();
}

bool NorthStarFdc::mediumDD() const {
    const Drive* d = selected();
    return d && d->img && d->fmt && d->fmt->dd;
}

bool NorthStarFdc::inWindow(const Rotation& r) const { return r.into < tFromUs(kWindowUs); }

bool NorthStarFdc::readEnabled(const Rotation& r) const {
    return wr_ == Wr::Idle && pastUs(r, kReadEnableUs);
}

bool NorthStarFdc::ddDetected(const Rotation& r) const {
    return wr_ == Wr::Idle && r.real && mediumDD() && pastUs(r, kDdiUs);
}

// "Writing data begins at the end of the window ... Writing stops at the next sector
// pulse."
bool NorthStarFdc::writeActive(const Rotation& r) const { return wr_ != Wr::Idle && !inWindow(r); }

// ---------------------------------------------------------------------------
// THE READ SIDE.
//
// The sector under the head is fetched once per pass. A sector that is not in the file --
// a blank diskette, a track never written -- has no sync character, so body mode never
// comes and the guest's own timeout reports it. That is what unformatted media does.
// ---------------------------------------------------------------------------
bool NorthStarFdc::loadSector(const Rotation& r) {
    if (rdAbs_ == r.abs) return rdValid_;
    rdAbs_   = r.abs;
    rdPos_   = 0;
    rdValid_ = false;

    Drive* d = selected();
    if (!r.real || !d || !d->img || !d->fmt || side() >= d->fmt->sides) return false;

    size_t n = sizeof rdBuf_;
    if (!d->img->readSector(imageTrack(d->track, side()), side(), r.sector, rdBuf_, &n) ||
        (int)n != d->fmt->sectorBytes)
        return false;
    rdLen_   = (int)n;

    // BAD SECTORS (#970). A missing sector has no sync character: body mode never comes, as
    // on a sector that is not in the file. A data fault spoils the check character only.
    rdFault_ = -1;
    if (!d->faults.empty()) {
        if (DiskFault* f = d->faults.find(d->track, side(), r.sector, FaultKind::Missing)) {
            useFault(*f, d->track, side(), r.sector);
            return false;
        }
        if (d->faults.find(d->track, side(), r.sector, FaultKind::Data))
            rdFault_ = (int)FaultKind::Data;
    }
    rdValid_ = true;

    if (auto* dc = debugChannel(); dc && dc->on(SECTOR))
        dbg::line(*dc) << "read drive=" << sel_ + 1 << " track=" << d->track << " side=" << side()
                       << " sector=" << r.sector << "\n";
    return true;
}

void NorthStarFdc::useFault(DiskFault& f, int track, int side, int sector) {
    DiskFaults::use(f);
    if (auto* dc = debugChannel(); dc && dc->on(FAULT)) {
        dbg::line(*dc) << "fault drive" << sel_ << " " << track << "/" << side << "/" << sector
                       << " " << faultKindName(f.kind) << " hit " << f.hits;
        if (!f.hard()) dbg::out() << ", " << f.left << " left";
        dbg::out() << "\n";
    }
}

bool NorthStarFdc::body(const Rotation& r) {
    if (wr_ != Wr::Idle) return false;
    if (bodyForced_) return true;
    if (!r.real || !pastUs(r, kBodyUs)) return false;
    return loadSector(r);
}

// Read Data. On the board this access stalls the CPU until the read shift register is
// full. Under `timing = full` the byte is simply the next one; under `timing = real` the
// access holds READY until it has come under the head -- byte p of the body, p+1 byte times
// after the sync character. After the data comes the check character.
uint8_t NorthStarFdc::readData(const Rotation& r) {
    if (!body(r) || !loadSector(r)) return 0;
    if (timingReal_ && rdPos_ <= rdLen_)
        holdUntil(now() - r.into + tFromUs(kBodyUs) + (uint64_t)(rdPos_ + 1) * byteT(mediumDD()));
    uint8_t v = 0;
    if (rdPos_ < rdLen_)       v = rdBuf_[rdPos_];
    else if (rdPos_ == rdLen_) {
        v = checkChar(rdBuf_, rdLen_);
        // #970: the guest's compare fails. The fault is looked up again here, so a FAULT
        // CLEAR while the data went by leaves the sector clean.
        Drive* d = selected();
        if (rdFault_ == (int)FaultKind::Data && d)
            if (DiskFault* f = d->faults.find(d->track, side(), r.sector, FaultKind::Data)) {
                v ^= 0xFF;
                useFault(*f, d->track, side(), r.sector);
            }
    }
    if (rdPos_ <= rdLen_) ++rdPos_;
    return v;
}

// ---------------------------------------------------------------------------
// THE WRITE SIDE.
//
// Begin Write is taken only inside the window: on the MDS-A the WRITE REQ flip-flop's J
// input is WINDOW-SAMP (schematic page 2). The controller then writes the first byte of
// zeros itself, and software sends the rest of the preamble, the sync character(s), the
// data and the check character, one memory read each.
//
// So the stream is parsed, not counted: zeros until the first byte that is not zero (the
// sync), one more sync byte at double density, then exactly one sector of data. The
// sector goes to the image, and to the host, when its last data byte arrives. The check
// character that follows is not kept -- the file has nowhere to put it, and a read
// computes it again.
// ---------------------------------------------------------------------------
void NorthStarFdc::beginWrite(const Rotation& r) {
    if (!inWindow(r)) return;
    const Drive* d = selected();
    wr_       = Wr::Preamble;
    wrDD_     = writeDD();
    wrTrack_  = d ? d->track : 0;
    wrSide_   = side();
    wrSector_ = r.sector;
    wrPos_    = 0;
    rdAbs_    = ~0ull;  // whatever was read from here is about to be stale
    rdValid_  = false;
    rdFault_  = -1;
}

void NorthStarFdc::writeData(uint8_t v) {
    // `timing = real`: the access holds READY until the write shift register takes the byte.
    // It shifts one out every byte time from the end of the window, and the board writes
    // the first zero itself -- so the guest's bytes are taken at window end + 1, 2, 3 ...
    // byte times, each at the next such slot after the access. That needs no count of the
    // bytes written: where the disk is says which slot comes next.
    if (timingReal_ && wr_ != Wr::Idle && wr_ != Wr::Done && clock_) {
        const Rotation r  = where();
        const uint64_t t  = now();
        const uint64_t bt = byteT(wrDD_);
        const uint64_t w0 = t - r.into + tFromUs(kWindowUs);  // the window ends; writing starts
        holdUntil(t < w0 ? w0 + bt : w0 + ((t - w0) / bt + 1) * bt);
    }
    switch (wr_) {
        case Wr::Idle:
        case Wr::Done:
            return;  // the write gate is shut: the byte goes nowhere
        case Wr::Preamble:
            if (v != 0) wr_ = wrDD_ ? Wr::Sync2 : Wr::Data;
            return;
        case Wr::Sync2:
            wr_ = Wr::Data;
            return;
        case Wr::Data:
            wrBuf_[wrPos_++] = v;
            if (wrPos_ == (wrDD_ ? 512 : 256)) {
                commitWrite();
                wr_ = Wr::Done;
            }
            return;
    }
}

void NorthStarFdc::commitWrite() {
    Drive* d = selected();
    if (!lastReal_ || !d || !d->img) return;  // nothing under the head

    char m[200];

    // The drive senses the notch and inhibits its own write head. The board cannot
    // refuse -- but unlike the MITS boards it CAN say so, in the WP status bit, and the
    // period software reads that bit before it writes.
    if (d->img->readOnly()) {
        if (!d->roSaid) {
            d->roSaid = true;
            std::snprintf(m, sizeof m,
                          "%s: drive%d is write-protected -- the guest's writes are being discarded",
                          id.c_str(), sel_);
            say(m);
        }
        return;
    }

    // A BLANK DISKETTE TAKES THE DENSITY OF ITS FIRST WRITE. Nothing on a real blank
    // says what it is; the format program decides. The file is given the board's largest
    // medium at that density and grows as the format proceeds.
    if (!d->fmt) {
        const NsFormat* pick = nullptr;
        for (const auto& f : formats())
            if (f.dd == wrDD_ && (!pick || f.bytes > pick->bytes)) pick = &f;
        if (!pick) return;
        applyFormat(*d, *pick);
    }

    if (d->fmt->dd != wrDD_) {
        if (!d->roSaid) {
            d->roSaid = true;
            std::snprintf(m, sizeof m,
                          "%s: drive%d holds a %s-density image and the guest is writing %s density "
                          "-- an image file has one sector size, so the write is discarded",
                          id.c_str(), sel_, d->fmt->dd ? "double" : "single",
                          wrDD_ ? "double" : "single");
            say(m);
        }
        return;
    }
    if (wrSide_ >= d->fmt->sides) {
        if (!d->roSaid) {
            d->roSaid = true;
            std::snprintf(m, sizeof m,
                          "%s: drive%d holds a single-sided image and the guest is writing side 2 "
                          "-- the write is discarded",
                          id.c_str(), sel_);
            say(m);
        }
        return;
    }

    // A BAD SPOT THE DRIVE WILL NOT WRITE (#970): the bytes go nowhere and the old ones stay.
    // The guest is not told, as on the real board. Its verify, or the next read, finds it.
    if (DiskFault* f = d->faults.find(wrTrack_, wrSide_, wrSector_, FaultKind::Write)) {
        useFault(*f, wrTrack_, wrSide_, wrSector_);
        return;
    }

    size_t n = (size_t)d->fmt->sectorBytes;
    if (!d->img->writeSector(imageTrack(wrTrack_, wrSide_), wrSide_, wrSector_, wrBuf_, &n)) {
        std::snprintf(m, sizeof m, "%s: write failed at track %d sector %d", id.c_str(), wrTrack_,
                      wrSector_);
        say(m);
        return;
    }
    d->img->sync();

    if (auto* dc = debugChannel(); dc && dc->on(SECTOR))
        dbg::line(*dc) << "write drive=" << sel_ + 1 << " track=" << wrTrack_ << " side=" << wrSide_
                       << " sector=" << wrSector_ << "\n";
}

// ---------------------------------------------------------------------------
// The probe. THE BOARD DOES THIS, not DiskImage (DESIGN.md 7.3): 179,200 bytes is a
// double-density North Star diskette only because this is a North Star controller.
// ---------------------------------------------------------------------------
void NorthStarFdc::applyFormat(Drive& d, const NsFormat& f) const {
    d.fmt = &f;
    d.img->init(kNsTracks, f.sides, /*interleaved=*/false);
    d.img->initFormat(0, kNsTracks - 1, 0, f.sides - 1, f.dd ? Density::DD : Density::SD,
                      kNsSectors, f.sectorBytes, /*startSector=*/0);
    // A matched file never grows (its writes stay in bounds). A blank one grows as the
    // guest's format program fills it, up to this format's size.
    d.img->setExtendsOnWrite(true);
}

bool NorthStarFdc::probe(Drive& d, std::string& err) const {
    const uint64_t got = d.img->size();
    const auto&    fs  = formats();

    if (!d.forced.empty()) {
        for (const auto& f : fs)
            if (d.forced == f.name) { applyFormat(d, f); return true; }
    }
    for (const auto& f : fs)
        if (sizeMatches(got, f.bytes)) { applyFormat(d, f); return true; }

    // A board with one medium has nothing to choose: a file of any other size is a
    // diskette that is not formatted yet, or not all of it.
    if (fs.size() == 1) { applyFormat(d, fs.front()); return true; }

    // An EMPTY file is a blank diskette. Its density is set by the first write.
    if (got == 0) { d.fmt = nullptr; return true; }

    err = "a " + std::to_string(got) + "-byte image is not a North Star diskette this board knows (";
    for (size_t i = 0; i < fs.size(); ++i) {
        if (i) err += ", ";
        err += std::string(fs[i].name) + " = " + std::to_string(fs[i].bytes);
    }
    err += " bytes). Set `media` on the drive to say which it is";
    return false;
}

// ---------------------------------------------------------------------------
// Units, MOUNT, UNMOUNT.
//
// THE UNITS COUNT FROM ZERO AND NORTH STAR COUNTS FROM ONE. `drive0` is the drive that
// North Star DOS and the PROM call drive 1 -- the same convention every other controller
// here uses for its first drive.
// ---------------------------------------------------------------------------
std::vector<UnitDef> NorthStarFdc::units() const {
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

bool NorthStarFdc::mount(const std::string& unit, const std::string& path, bool ro,
                         std::string& err) {
    int i = driveIndex(unit, drives_);
    if (i < 0) {
        err = "no unit `" + unit + "` on " + id + " (it has drive0.." +
              std::to_string(drives_ - 1) + ")";
        return false;
    }

    // WHERE WE LOOK is resolvePath(); WHAT WE REMEMBER is `path`, as it was written
    // (core/board.h), so SHOW and CONFIG SAVE give back what the file said.
    auto media = openMedia(resolvePath(path), ro, err);
    if (!media) { err += pathNote(path); return false; }

    Drive& d = drive_[(size_t)i];
    Drive  fresh;  // probe into a FRESH drive: a refused image must not half-replace the old one
    fresh.forced = d.forced;
    fresh.img    = std::make_unique<DiskImage>(std::move(media));
    fresh.path   = path;

    if (!probe(fresh, err)) return false;

    if (fresh.img->readOnlyForced()) {
        char m[192];
        std::snprintf(m, sizeof m,
                      "%s: drive%d mounted WRITE-PROTECTED -- the host will not let us write %s",
                      id.c_str(), i, path.c_str());
        say(m);
    }

    if (sel_ == i) endSector();
    d.img    = std::move(fresh.img);
    d.path   = std::move(fresh.path);
    d.fmt    = fresh.fmt;
    d.roSaid = false;
    d.faults.clearAll();  // the faults were the old diskette's
    return true;
}

bool NorthStarFdc::unmount(const std::string& unit, std::string& err) {
    int i = driveIndex(unit, drives_);
    if (i < 0) { err = "no unit `" + unit + "` on " + id; return false; }

    Drive& d = drive_[(size_t)i];
    if (!d.img) { err = id + ":" + unit + " is empty"; return false; }

    if (sel_ == i) endSector();
    d.img->sync();
    d.img.reset();
    d.path.clear();
    d.fmt = nullptr;
    d.faults.clearAll();
    return true;
}

std::vector<std::string> NorthStarFdc::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    return out;
}

// ---------------------------------------------------------------------------
// Properties, and the [[board.drive]] sub-unit table.
// ---------------------------------------------------------------------------
std::vector<Property> NorthStarFdc::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "base";
        x.help  = "Origin of the 1 K block. The built-in PROM is the standard part and runs only at E800";
        x.kind  = Kind::Int;
        x.radix = 16;  // an address on the wire -> hex
        x.min   = 0;
        x.max   = 0xFC00;
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string& err) {
            if (v.i() & 0x3FF) {
                err = "the board selects on the high six address bits -- base must be a "
                      "multiple of 0x400";
                return false;
            }
            base_ = (uint16_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "drives";
        x.help  = "Drives on the cable";
        x.kind  = Kind::Int;
        x.radix = 10;  // a count of things
        x.min   = 1;
        x.max   = maxDrives();
        x.get   = [this] { return Value::ofInt(drives_); };
        x.set   = [this](const Value& v, std::string& err) {
            int n = (int)v.i();
            for (int i = n; i < (int)drive_.size(); ++i) {
                if (drive_[(size_t)i].img) {
                    err = "drive" + std::to_string(i) + " still has a disk in it";
                    return false;
                }
            }
            if (sel_ >= n) selectDrive(-1);
            drives_ = n;
            drive_.resize((size_t)n);
            return true;
        };
        p.push_back(std::move(x));
    }
    p.push_back(irqJumperProperty("interrupt",
                                  "Where the sector-pulse interrupt is jumpered (lower left of the board)",
                                  irq_));
    {
        // The same policy as the 88-MDS, and for the same reason: flat out is the default.
        // What is NOT behind this switch is the 300 RPM diskette, the 96 us window and the
        // sector flag -- those are the board, and the guest's own delay loops wait on them.
        Property x;
        x.name    = "motor";
        x.help    = "free: the motors run until they are stopped (default). real: they stop "
                    "by themselves after the board's count of revolutions";
        x.kind    = Kind::Enum;
        x.choices = {"free", "real"};
        x.get     = [this] { return Value::ofStr(motorReal_ ? "real" : "free"); };
        x.set     = [this](const Value& v, std::string& err) {
            if (v.s() == "free")      motorReal_ = false;
            else if (v.s() == "real") motorReal_ = true;
            else { err = "motor is `free` or `real`"; return false; }
            // A motor that was already running starts its count from now.
            if (motorOn_) motorSet();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "timing";
        x.help    = "Disk timing. full: a read-data or write-data access completes at once. "
                    "real: it holds READY until the byte is under the head, so a sector transfer "
                    "takes emulated time, as on the board";
        x.kind    = Kind::Enum;
        x.choices = {"full", "real"};
        x.get     = [this] { return Value::ofStr(timingReal_ ? "real" : "full"); };
        x.set     = [this](const Value& v, std::string& err) {
            if (v.s() != "full" && v.s() != "real") {
                err = "timing is full or real";
                return false;
            }
            timingReal_ = v.s() == "real";
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<Property> NorthStarFdc::subUnitProperties(const std::string& table) const {
    if (table != "drive") return {};
    std::vector<Property> p;
    {
        Property x;
        x.name  = "unit";
        x.help  = "Which drive. Unit 0 is the drive that North Star calls drive 1";
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
        x.help    = "Write-protect the diskette. The guest sees it in the WP status bit";
        x.kind    = Kind::Bool;
        x.aliases = {"writeprotect"};  // the operator's word for it; `readonly` is written back
        p.push_back(std::move(x));
    }
    p.push_back(mediaKey());
    {
        Property x;
        x.name = "create";
        x.help = "Make the disk file (empty) if it is not there, then mount it -- a blank "
                 "diskette for the guest to format";
        x.kind = Kind::Bool;
        p.push_back(std::move(x));
    }
    p.push_back(driveFaultsKey());
    return p;
}

// `media`: a [[board.drive]] key, and a MOUNT option that reaches the probe (mountKeys).
Property NorthStarFdc::mediaKey() const {
    Property x;
    x.name = "media";
    x.help = "Force the format instead of probing the image's size";
    x.kind = Kind::Enum;
    for (const auto& f : formats()) x.choices.push_back(f.name);
    return x;
}

std::vector<Property> NorthStarFdc::mountKeys(const std::string& unit) const {
    if (driveIndex(unit, drives_) < 0) return {};
    return {mediaKey()};
}

// The media goes with the disk: a mount that is refused leaves the old disk and its media.
bool NorthStarFdc::mountWith(const std::string& unit, const std::string& path, bool ro,
                      const KeyValues& keys, std::string& err) {
    const int i = driveIndex(unit, drives_);
    if (i < 0) return mount(unit, path, ro, err);  // mount() names the units
    std::string& forced = drive_[(size_t)i].forced;
    const std::string was = forced;
    for (const auto& [k, v] : keys)
        if (k == "media") forced = v;
    if (mount(unit, path, ro, err)) return true;
    forced = was;
    return false;
}

bool NorthStarFdc::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
    if (table != "drive") {
        err = type() + " has no [[board." + table + "]] table";
        return false;
    }

    int         unit = -1;
    std::string path, media, faultText;
    bool        ro     = false;
    bool        create = false;

    // loadSubUnit() has already refused a key we did not declare, a `media` that is not
    // in formats() and a `unit` outside 0..drives-1.
    for (const auto& [k, v] : kv) {
        if (k == "unit") unit = std::stoi(v);
        else if (k == "mount") path = v;
        else if (k == "readonly") ro = truthy(v);
        else if (k == "media") media = v;
        else if (k == "create") create = truthy(v);
        else if (k == "faults") faultText = v;
    }

    if (unit < 0) {
        err = "[[board.drive]] needs a `unit`";
        return false;
    }

    if (path.empty()) {
        drive_[(size_t)unit].forced = media;
        return applyDriveFaults(drive_[(size_t)unit].faults, false, faultText, err);
    }

    // `create = true`: make the file (empty) where mount() will open it. Never replace a
    // file that is already there.
    if (create) {
        std::string     rp = resolvePath(path);
        std::error_code ec;
        if (!std::filesystem::exists(rp, ec)) {
            std::string cerr;
            if (!writeHostFile(rp, {}, cerr)) { err = cerr + pathNote(path); return false; }
        }
    }
    // Always passed, empty too: a table with no `media` is a disk the size decides.
    if (!mountWith("drive" + std::to_string(unit), path, ro, {{"media", media}}, err)) return false;
    return applyDriveFaults(drive_[(size_t)unit].faults, true, faultText, err);
}

bool NorthStarFdc::takesFaults(const std::string& unit) const {
    return driveIndex(unit, drives_) >= 0;
}

DiskFaults* NorthStarFdc::faults(const std::string& unit) {
    const int i = driveIndex(unit, drives_);
    return (i >= 0 && drive_[(size_t)i].img) ? &drive_[(size_t)i].faults : nullptr;
}

// An EMPTY drive with no forced media writes no table (DESIGN.md 5).
std::vector<Board::SubUnit> NorthStarFdc::subUnits() const {
    std::vector<SubUnit> out;
    for (int i = 0; i < drives_; ++i) {
        const Drive& d = drive_[(size_t)i];
        if (!d.img && d.forced.empty()) continue;

        SubUnit su;
        su.table = "drive";
        su.fields.push_back({"unit", std::to_string(i), false});
        if (!d.forced.empty()) su.fields.push_back({"media", d.forced, true});
        if (d.img) {
            su.fields.push_back({"mount", d.path, true});
            if (d.img->readOnly()) su.fields.push_back({"readonly", "true", false});
            if (!d.faults.empty()) su.fields.push_back({"faults", d.faults.toText(), true});
        }
        out.push_back(std::move(su));
    }
    return out;
}

// ---------------------------------------------------------------------------
// SNAPSHOT/RESTORE.
// ---------------------------------------------------------------------------
void NorthStarFdc::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u32((uint32_t)(int32_t)sel_);
    w.boolean(sf_);
    w.boolean(armed_);
    w.boolean(motorOn_);
    w.u64(motorOffAt_);
    w.boolean(lastReal_);
    w.u64(lastAbs_);

    w.u32((uint32_t)drive_.size());
    for (const auto& d : drive_) {
        w.u32((uint32_t)(int32_t)d.track);
        w.boolean(d.roSaid);
    }

    w.raw(rdBuf_, sizeof rdBuf_);
    w.u64(rdAbs_);
    w.boolean(rdValid_);
    w.u32((uint32_t)(int32_t)rdLen_);
    w.u32((uint32_t)(int32_t)rdPos_);
    w.boolean(bodyForced_);

    w.u8((uint8_t)wr_);
    w.boolean(wrDD_);
    w.u32((uint32_t)(int32_t)wrTrack_);
    w.u32((uint32_t)(int32_t)wrSide_);
    w.u32((uint32_t)(int32_t)wrSector_);
    w.u32((uint32_t)(int32_t)wrPos_);
    w.raw(wrBuf_, sizeof wrBuf_);

    // The bad sectors and their counts, and the fault on the sector being read (#970).
    for (const auto& d : drive_) d.faults.serialize(w);
    w.u32((uint32_t)(int32_t)rdFault_);
}

void NorthStarFdc::deserialize(StateReader& r) {
    Board::deserialize(r);
    ahead_ = 0;  // never serialized: see holdUntil()
    sel_        = (int)(int32_t)r.u32();
    sf_         = r.boolean();
    armed_      = r.boolean();
    motorOn_    = r.boolean();
    motorOffAt_ = r.u64();
    lastReal_   = r.boolean();
    lastAbs_    = r.u64();
    if (sel_ >= drives_) sel_ = -1;

    uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; ++i) {
        int  track  = (int)(int32_t)r.u32();
        bool roSaid = r.boolean();
        if (i < drive_.size()) {
            drive_[i].track  = track;
            drive_[i].roSaid = roSaid;
        }
    }

    r.raw(rdBuf_, sizeof rdBuf_);
    rdAbs_      = r.u64();
    rdValid_    = r.boolean();
    rdLen_      = (int)(int32_t)r.u32();
    rdPos_      = (int)(int32_t)r.u32();
    bodyForced_ = r.boolean();

    wr_       = (Wr)r.u8();
    wrDD_     = r.boolean();
    wrTrack_  = (int)(int32_t)r.u32();
    wrSide_   = (int)(int32_t)r.u32();
    wrSector_ = (int)(int32_t)r.u32();
    wrPos_    = (int)(int32_t)r.u32();
    r.raw(wrBuf_, sizeof wrBuf_);

    // Into each drive's own table, so the drive's refusals (an id fault) stay.
    for (auto& d : drive_) d.faults.deserialize(r);
    rdFault_ = (int)(int32_t)r.u32();

    intChanged();
    scheduleWake();
}

// ===========================================================================
// MDS-A -- single density.
// ===========================================================================
const std::vector<NsFormat>& MdsABoard::formats() const {
    static const std::vector<NsFormat> f = {
        {"sd", false, 1, 256, 35ull * 10 * 256},  // 89,600
    };
    return f;
}

MdsABoard::MdsABoard() : NorthStarFdc("nsboot-sd", 0xE900, 3) {}

const char* MdsABoard::caseNote(int kase) const {
    switch (kase) {
        case 0:  return "MDS-A boot PROM (the same 256 bytes as the next page)";
        case 1:  return "MDS-A boot PROM -- RUN here";
        case 2:  return "MDS-A write data: the low address byte goes to the disk";
        default: return "MDS-A command: MO RD BST CC(3) M1 M0 -- reads status or disk data";
    }
}

void MdsABoard::controllerReset() {
    NorthStarFdc::controllerReset();
    stepFf_ = false;  // RST clears the step flip-flop; the direction flip-flop has no reset
}

// ---------------------------------------------------------------------------
// One memory read in the block.
//
//   case 0, 1   the PROM. The errata sheet (July 31 1978) makes case 0 identical to case 1.
//   case 2      write a byte of data
//   case 3      a command:   MO RD BST CC CC CC M1 M0
//
// THE STATUS IS SAMPLED BEFORE THE COMMAND ACTS, and the PROM depends on it. Its first
// act is `LDA CTLMO+CTLNOP / ANI SAMO` -- start the motors AND ask whether they were
// already running -- and it takes a different path on each answer. The schematic agrees:
// MOTOR-SAMP and WINDOW-SAMP are flip-flops clocked by the leading edge of the read,
// before the command pulse.
// ---------------------------------------------------------------------------
uint8_t MdsABoard::access(int kase, uint8_t low) {
    if (kase < 2) return promByte(low);
    if (kase == 2) { writeData(low); return 0; }

    const Rotation r = where();

    uint8_t v;
    if (low & 0x40) {  // RD: the read shift register, not a status byte
        v = readData(r);
    } else {
        // A-status:  SF WN 0 MO | WRT BDY WP TR0
        // B-status:  SF WN 0 MO | sector position
        // Bit 5 is a pad for a diagnostic jumper (schematic page 1) and reads 0.
        v = 0;
        if (sectorFlag()) v |= 0x80;
        if (inWindow(r))  v |= 0x40;
        if (motorOn())    v |= 0x10;
        if (low & 0x20) {
            v |= (uint8_t)(r.sector & 0x0F);
        } else {
            if (writeActive(r))   v |= 0x08;
            if (body(r))          v |= 0x04;
            if (writeProtected()) v |= 0x02;
            if (track0())         v |= 0x01;
        }
    }

    if (low & 0x80) motorSet();  // MO, on any command

    const bool m0 = (low & 0x01) != 0;
    switch ((low >> 2) & 0x07) {
        case 0:
            // Load the select register from M1,M0. The register is held clear while the
            // motors are off, so a select then does nothing. 00 selects no drive.
            if (motorOn()) selectDrive((low & 0x03) - 1);
            break;
        case 1: beginWrite(r); break;
        case 2:
            // The step flip-flop drives the STEP line, and the drive moves on the
            // trailing edge of the pulse: when the flip-flop is cleared again.
            if (stepFf_ && !m0) step(dirIn_);
            stepFf_ = m0;
            break;
        case 3: armInterrupt(m0); break;
        case 4: break;  // no operation -- this is how a status byte is read
        case 5: resetSectorFlag(); break;
        case 6: controllerReset(); break;
        case 7: dirIn_ = m0; break;
    }
    return v;
}

void MdsABoard::serialize(StateWriter& w) const {
    NorthStarFdc::serialize(w);
    w.boolean(stepFf_);
    w.boolean(dirIn_);
}

void MdsABoard::deserialize(StateReader& r) {
    NorthStarFdc::deserialize(r);
    stepFf_ = r.boolean();
    dirIn_  = r.boolean();
}

// ===========================================================================
// MDS-A-D -- double density.
// ===========================================================================
const std::vector<NsFormat>& MdsADBoard::formats() const {
    static const std::vector<NsFormat> f = {
        {"sd",   false, 1, 256, 35ull * 10 * 256},      //  89,600
        {"dd",   true,  1, 512, 35ull * 10 * 512},      // 179,200
        {"quad", true,  2, 512, 35ull * 10 * 512 * 2},  // 358,400 -- North Star's "quad capacity"
    };
    return f;
}

MdsADBoard::MdsADBoard() : NorthStarFdc("nsboot-dd", 0xE800, 4) {}

const char* MdsADBoard::caseNote(int kase) const {
    switch (kase) {
        case 0:  return "MDS-A-D boot PROM -- RUN here";
        case 1:  return "MDS-A-D write data: the low address byte goes to the disk";
        case 2:  return "MDS-A-D orders: DD SS DP ST DS(4) -- density, side, step, drive";
        default: return "MDS-A-D command: DM(3) . CC(3) -- reads status or disk data";
    }
}

void MdsADBoard::controllerReset() {
    NorthStarFdc::controllerReset();
    orders_ = 0;
}

// The order register:  DD SS DP ST | DS DS DS DS
//
// DS is ONE-HOT -- 1, 2, 4, 8 for drives 1 to 4, 0 for none. ST is the LEVEL of the step
// line, so a step is three orders (low, high, low) and the drive moves when the pulse
// ends; DP gives the direction (1 = in). DD and SS are read when they are needed.
void MdsADBoard::loadOrders(uint8_t v) {
    const uint8_t old = orders_;
    orders_ = v;

    int n = -1;
    for (int i = 0; i < 4; ++i)
        if (v & (1u << i)) { n = i; break; }
    selectDrive(n);

    if ((old & 0x10) && !(v & 0x10)) step((v & 0x20) != 0);
}

// ---------------------------------------------------------------------------
// One memory read in the block.
//
//   case 0   the PROM
//   case 1   write a byte of data
//   case 2   load the order register
//   case 3   a command:   . DM DM DM . CC CC CC
//
// DM picks what the read returns: 1, 2, 3 = A-, B-, C-status, 4 = disk data. The manual
// gives no meaning to 0 or 5-7; they read 0 here. As on the MDS-A, the byte is taken
// before the command code acts.
// ---------------------------------------------------------------------------
uint8_t MdsADBoard::access(int kase, uint8_t low) {
    if (kase == 0) return promByte(low);
    if (kase == 1) { writeData(low); return 0; }
    if (kase == 2) { loadOrders(low); return 0; }

    const Rotation r = where();

    // The four bits every status byte starts with:  SF IX DD MO
    //
    // IX is "true if index hole detected during previous sector". The index hole lies in
    // the last sector of the revolution -- it is what sets the sector counter, so that the
    // next sector hole starts sector 0 -- which makes IX true during sector 0.
    uint8_t hi = 0;
    if (sectorFlag())             hi |= 0x80;
    if (r.real && r.sector == 0)  hi |= 0x40;
    if (ddDetected(r))            hi |= 0x20;
    if (motorOn())                hi |= 0x10;

    uint8_t v = 0;
    switch ((low >> 4) & 0x07) {
        case 1:  // A-status:  SF IX DD MO | WI RE SP BD
            v = hi;
            if (inWindow(r))    v |= 0x08;
            if (readEnabled(r)) v |= 0x04;
            if (body(r))        v |= 0x01;
            break;
        case 2:  // B-status:  SF IX DD MO | WR SP WP T0
            v = hi;
            if (writeActive(r))   v |= 0x08;
            if (writeProtected()) v |= 0x02;
            if (track0())         v |= 0x01;
            break;
        case 3:  // C-status:  SF IX DD MO | sector counter
            v = (uint8_t)(hi | (r.sector & 0x0F));
            break;
        case 4:
            v = readData(r);
            break;
        default:
            break;
    }

    switch (low & 0x07) {
        case 0: break;  // no operation
        case 1: resetSectorFlag(); break;
        case 2: armInterrupt(false); break;
        case 3: armInterrupt(true); break;
        case 4: setBody(); break;  // diagnostic: enter body mode with no sync character
        case 5: motorSet(); break;
        case 6: beginWrite(r); break;
        case 7: controllerReset(); break;
    }
    return v;
}

void MdsADBoard::serialize(StateWriter& w) const {
    NorthStarFdc::serialize(w);
    w.u8(orders_);
}

void MdsADBoard::deserialize(StateReader& r) {
    NorthStarFdc::deserialize(r);
    orders_ = r.u8();
}

} // namespace altair
