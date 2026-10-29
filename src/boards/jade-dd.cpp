#include "boards/jade-dd.h"

#include "core/statefile.h"
#include "core/value.h"
#include "host/media.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace altair {

// A board on no clock is a board in a test rig that never attached one. It reads as a
// machine that never moves: the board Z80 never catches up to anything.
static Clock& stoppedClock() {
    static Clock stopped;
    return stopped;
}

Clock& JadeDdBoard::hostClk() const { return clock_ ? *clock_ : stoppedClock(); }

JadeDdBoard::JadeDdBoard() {
    drive_.resize(kDrives);
    ramBoard_.d = this;
    ramBoard_.id = "ram";
    ioBoard_.d = this;
    ioBoard_.id = "io";
    bbus_.attach(&ramBoard_);
    bbus_.attach(&ioBoard_);
    bclk_.setHz(boardHz());
    // The FD1791 runs its byte-timed model on the board clock: the board Z80 is the one
    // waiting for DRQ (the WAIT strobe), and it is fast enough never to lose a byte. The
    // density check is on, because DCM2 reads SD track 0 and DD track 1 of the same disk.
    fdc_.setDensityChecked(true);
    fdc_.powerOn(bclk_);
    cpu_.reset(Reset::PowerOn);
    applySelection();
}

JadeDdBoard::~JadeDdBoard() {
    // The host clock holds a lambda with `this` in it (the VI deadline).
    if (clock_) clock_->cancel(hostWake_);
    bclk_.cancel(fdcWake_);
    bbus_.detach(&ioBoard_);
    bbus_.detach(&ramBoard_);
}

// ---------------------------------------------------------------------------
// HOST TIME -> BOARD TIME.
//
// board T = host T x boardHz / host hz(). The host's hz() is its crystal, and it is never 0
// (Clock::setHz): flat out still counts T-states at clock_hz, so a flat-out host gets the
// same board time per host T-state as a paced one. The remainder carries from one call to
// the next, so no board T-state is lost to rounding. A SET clock_hz between two calls splits
// the interval at Clock::rateChangedAt(): the part before it at the old rate, the rest at
// the new.
// ---------------------------------------------------------------------------
void JadeDdBoard::catchUp() {
    if (inCatchUp_) return;
    Clock&   h   = hostClk();
    uint64_t now = h.now();
    if (now < anchorH_) anchorH_ = now;  // a host clock this board has not seen before
    long long hz = h.hz();
    if (lastHz_ <= 0) lastHz_ = hz;
    uint64_t bhz = (uint64_t)boardHz();

    // Convert `dh` host T-states at `rate`, carrying rem_ (which is in units of `rate`).
    auto convert = [&](uint64_t dh, long long rate) {
        uint64_t r = (uint64_t)rate;
        uint64_t q = dh / r, m = dh % r;
        uint64_t n = m * bhz + rem_;
        rem_ = n % r;
        return q * bhz + n / r;
    };

    uint64_t adv  = 0;
    uint64_t from = anchorH_;
    if (lastHz_ != hz) {
        uint64_t at = std::clamp(h.rateChangedAt(), from, now);
        adv += convert(at - from, lastHz_);
        rem_ = 0;  // a remainder in old-rate units means nothing at the new rate
        from = at;
    }
    adv += convert(now - from, hz);
    anchorH_ = now;
    lastHz_  = hz;
    targetB_ += adv;

    inCatchUp_ = true;
    runTo(targetB_);
    inCatchUp_ = false;

    intChanged();  // the VI line follows the board Z80's halt
    rearmHost();
}

void JadeDdBoard::runTo(uint64_t target) {
    while (bclk_.now() < target) {
        if (!slvrq_) {
            StepResult s = cpu_.step(bbus_);
            bclk_.advance(s.tStates + bbus_.takeWaitStates());
            // A HLT that is still a HLT after the step is waiting for NMI* or INT*. Both
            // come from the FD1791 or the host, so nothing changes until the next FD1791
            // edge or the target: skip to it.
            if (!cpu_.halted()) continue;
        }
        // Halted, or the RAM is the host's (BUSRQ*: the Z80 is stopped). The FD1791 still
        // runs, and its next edge is the only thing that can happen.
        uint64_t e  = fdc_.nextEdge(bclk_);
        uint64_t to = (e && e < target) ? e : target;
        if (to > bclk_.now()) bclk_.advance(to - bclk_.now());
    }
}

// The VI deadline. Only while the strap is in and the board Z80 is running: then the halt
// must reach the VI line without the host asking. Anything else queues nothing, so a host
// HLT with this board idle is still a final halt (Clock::queued()).
void JadeDdBoard::rearmHost() {
    if (!clock_) return;
    clock_->cancel(hostWake_);
    hostWake_ = Clock::kNone;
    if (irq_ == IrqJumper::None || slvrq_ || cpu_.halted()) return;
    uint64_t dt = std::max<uint64_t>(1, clock_->tStatesPer(1000));  // 1 ms of host time
    hostWake_ = clock_->after(dt, [this] { hostWake(); });
}

void JadeDdBoard::hostWake() {
    hostWake_ = Clock::kNone;
    catchUp();
}

// ---------------------------------------------------------------------------
// THE HOST SIDE (pp. 3-6).
// ---------------------------------------------------------------------------
bool JadeDdBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type == Cycle::IoRead || c.type == Cycle::IoWrite) return c.port() == port_;
    if (c.type == Cycle::MemRead || c.type == Cycle::MemWrite) return windowHit(c.addr);
    return false;
}

// While the host has the RAM, ZINT is internal address bit IA10: it picks which 1 K half of
// the 2 K is in the window (p. 6). The board Z80 is stopped on BUSRQ*, so it cannot move
// anything the host reads here, and a window cycle does not need to catch it up.
uint8_t JadeDdBoard::read(const BusCycle& c) {
    if (c.type == Cycle::IoRead) return hostStatus();
    return ram_[(zint_ ? 0x400 : 0) | (c.addr & 0x3FF)];
}

void JadeDdBoard::write(const BusCycle& c) {
    if (c.type == Cycle::IoWrite) {
        writeControl(c.data);
        return;
    }
    ram_[(zint_ ? 0x400 : 0) | (c.addr & 0x3FF)] = c.data;
}

bool JadeDdBoard::peek(uint16_t addr, uint8_t& out) const {
    if (!enabled_ || !windowHit(addr)) return false;
    out = ram_[(zint_ ? 0x400 : 0) | (addr & 0x3FF)];
    return true;
}

// The PHAN* jumper (ECN 6): the board pulls PHANTOM* while the host reads the window, so the
// window can overlap a 64 K RAM board that honors it.
bool JadeDdBoard::assertsPhantom(const BusCycle& c) const {
    return phantom_ && enabled_ && c.type == Cycle::MemRead && windowHit(c.addr);
}

// Fig 2-11: a NOR of ZHLT*, SLVRQ and ZINT. The Z80 has halted, the host has not taken the
// RAM, and no interrupt to the Z80 is pending.
uint8_t JadeDdBoard::assertsVi() const { return viActive() ? viBit(irq_) : 0; }

// Disk Processor Status (Fig 2-5). D0 = ZHLT*, D1-D3 = SA10-SA12 (a switch that is open
// reads 1). The 74LS244 at 4H drives only D0-D3; D4-D7 are not driven and read high.
uint8_t JadeDdBoard::hostStatus() {
    catchUp();
    uint8_t sa = (uint8_t)((window_ >> 10) & 0x07);
    return (uint8_t)(0xF0 | (sa << 1) | (cpu_.halted() ? 0x00 : 0x01));
}

// Disk Processor Control (Fig 2-4). One write sets SLVRQ (D0) and ZINT (D1), and D7 fires
// the one-shot that pulses the Z80's RESET*. Each release is also a command: 02h gives the
// RAM back AND interrupts the Z80; 80h gives it back AND resets it.
void JadeDdBoard::writeControl(uint8_t v) {
    catchUp();  // the board Z80 lives up to now under the old latches
    zint_ = (v & 0x02) != 0;
    if (v & 0x80) cpu_.reset(Reset::Bus);  // ZRST*: the Z80 starts again at 0000h
    setGranted((v & 0x01) != 0);
    ioBoard_.intChanged();
    intChanged();
    rearmHost();
}

void JadeDdBoard::setGranted(bool host) {
    if (host == slvrq_) return;
    slvrq_ = host;
    decodeChanged();  // the window decodes only while the host has the RAM
}

// ---------------------------------------------------------------------------
// THE BOARD SIDE (pp. 7-12). What the board Z80 sees on its own ports.
// ---------------------------------------------------------------------------
uint8_t JadeDdBoard::localIn(uint8_t port) {
    switch (port & 0x07) {  // decoded on A0-A2 (Table 2-3)
        case 0: {
            // A7: WAIT (ZHLD*) until the FD1791 raises DRQ or INTRQ (p. 12). The wait comes
            // first: the cycle completes, and the status is read, when it ends.
            if (port & 0x80) ioBoard_.holdReady(fdc_.holdUntilReady(bclk_));
            // U0 closed: an FD1791. SERI is a marking (idle) EIA line. CHNG* reads "no
            // change": the board has no disk-change sense to drive it, and DCM2 never reads it.
            uint8_t s = kU1 | kTst | kSeri | kChng;
            if (zint_) s |= kZint;
            if (bclk_.now() >= timerEnd_) s |= kToff;
            FloppyDrive* fd = (cmd_ & kDse) ? &drive_[cmd_ & 0x03].drv : nullptr;
            if (!(fd && fd->twoSided())) s |= kTsd;  // TWO SIDED*, active low
            // The strobes in A3-A6 (Table 2-4). The status above is what the cycle read.
            if ((port & 0x08) && fd) fd->step((cmd_ & kSid1) != 0);  // STEP*, SID1 = DIRC
            if (port & 0x10) timerEnd_ = 0;                          // clear the timer
            if (port & 0x20) {                                       // reset the host interrupt
                zint_ = false;
                ioBoard_.intChanged();
            }
            // Start the timer. A 74LS123 retriggers, so a start while it runs extends it.
            if (port & 0x40) timerEnd_ = bclk_.now() + timerPeriod();
            fdcRefresh();
            return s;
        }
        case 4: { uint8_t v = fdc_.readStatus(bclk_); fdcRefresh(); return (uint8_t)~v; }
        case 5: return (uint8_t)~fdc_.readTrackReg();
        case 6: return (uint8_t)~fdc_.readSectorReg();
        case 7: { uint8_t v = fdc_.readData(bclk_); fdcRefresh(); return (uint8_t)~v; }
        default: return 0xFF;  // 01-03: nothing decodes them
    }
}

// THE FD1791's DAL IS AN INVERTED BUS, and the Double D wires it to the Z80 with nothing in
// between, so the Z80 reads and writes the complement of every FD1791 register. The chip's
// registers are true sense (wd17xx.h). U0 reads closed, which is how DCM2 knows to XOR every
// FD1791 byte with FFh.
void JadeDdBoard::localOut(uint8_t port, uint8_t v) {
    uint8_t w = (uint8_t)~v;
    switch (port & 0x07) {
        case 0:
            cmd_ = v;
            applySelection();
            ioBoard_.nmiChanged();  // DSE gates NMI* (CR18)
            break;
        case 4: fdc_.writeCommand(w, bclk_); break;
        case 5: fdc_.writeTrackReg(w);       break;
        case 6: fdc_.writeSectorReg(w);      break;
        case 7: fdc_.writeData(w, bclk_);    break;
        default: break;
    }
    fdcRefresh();
}

// The board command latch -> the FD1791's drive (DSE + DSB:DSA, Table 2-7), side (SID1),
// density (DDEN) and the media data rate that goes with the density and the drive size.
void JadeDdBoard::applySelection() {
    int  side = (cmd_ & kSid1) ? 1 : 0;
    bool dd   = (cmd_ & kDden) != 0;
    FloppyDrive* fd = nullptr;
    if (cmd_ & kDse) {
        Drive& d = drive_[cmd_ & 0x03];
        d.drv.setSide(side);
        fd = &d.drv;
    }
    fdc_.attach(fd);
    fdc_.setSide(side);
    fdc_.doubleDensity = dd;
    fdc_.fdcClockHz    = size8_ ? 2000000 : 1000000;
    fdc_.dataRateBits  = (dd ? 500000 : 250000) / (size8_ ? 1 : 2);
}

void JadeDdBoard::fdcRefresh() {
    fdc_.poll(bclk_);
    ioBoard_.nmiChanged();
    bclk_.cancel(fdcWake_);
    fdcWake_ = Clock::kNone;
    uint64_t e = fdc_.nextEdge(bclk_);
    if (e) fdcWake_ = bclk_.at(e, [this] { fdcRefresh(); });
}

// ---------------------------------------------------------------------------
// Lifecycle. BR* (SLVCLR*, or POC* with the BRST* jumper) clears both 74LS74s of Fig 2-4:
// SLVRQ* goes low, so the host has the RAM, and ZINT* goes low, so ZINT is set (bank 1 is in
// the window). It clears the 74LS273 command latch too, which deselects every drive. The
// manual does not show BR* reaching the Z80's RESET*, and the Z80 is stopped on BUSRQ* until
// the host lets go, so a bus reset leaves the Z80 alone. DDBOOT resets it with OUT 80h.
// ---------------------------------------------------------------------------
void JadeDdBoard::reset(Reset) {
    catchUp();
    zint_ = true;
    cmd_  = 0;
    setGranted(true);
    applySelection();
    fdcRefresh();
    ioBoard_.intChanged();
    intChanged();
    rearmHost();
}

void JadeDdBoard::power() {
    std::fill(std::begin(ram_), std::end(ram_), (uint8_t)0);
    bclk_.power();
    fdcWake_ = Clock::kNone;
    targetB_ = 0;
    rem_     = 0;
    timerEnd_ = 0;  // the one-shot powers up untriggered
    anchorH_ = hostClk().now();
    lastHz_  = hostClk().hz();
    cpu_.reset(Reset::PowerOn);
    fdc_.powerOn(bclk_);
    for (Drive& d : drive_) d.drv.setHeadTrack(0);
    reset(Reset::PowerOn);
}

// A new host clock: this board has never caught up against it, so it starts from its now.
void JadeDdBoard::clockAttached() {
    anchorH_ = hostClk().now();
    lastHz_  = hostClk().hz();
    rem_     = 0;
    rearmHost();
}

void JadeDdBoard::configChanged() {
    decodeChanged();  // `port` or `window` moved
    bclk_.setHz(boardHz());
    for (Drive& d : drive_) d.drv.setRevsPerSecond(size8_ ? 6 : 5);
    applySelection();
    fdcRefresh();
    intChanged();
    rearmHost();
}

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------
std::vector<Property> JadeDdBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "The host I/O port, set by switches P0 and P1: 40, 41, 42 or 43 (default 43, "
                  "the port the Jade software uses)";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0x40;
        x.max   = 0x43;
        x.get   = [this] { return Value::ofInt(port_); };
        x.set   = [this](const Value& v, std::string&) {
            port_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "window";
        x.help  = "Where the host sees the 1 K memory window, set by switches M10-M12: a 1 K "
                  "boundary from E000 to FC00 (default E000)";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0xE000;
        x.max   = 0xFC00;
        x.get   = [this] { return Value::ofInt(window_); };
        x.set   = [this](const Value& v, std::string& err) {
            if (v.i() & 0x3FF) {
                err = "window is a 1 K boundary (E000, E400, ... FC00): switches M10-M12 set "
                      "only address bits A10-A12";
                return false;
            }
            window_ = (uint16_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "phantom";
        x.help = "The PHAN* jumper. On: the board pulls PHANTOM* while the host reads the "
                 "window, so the window can overlap RAM that honors PHANTOM*";
        x.kind = Kind::Bool;
        x.get  = [this] { return Value::ofBool(phantom_); };
        x.set  = [this](const Value& v, std::string&) {
            phantom_ = v.b();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        // The INTERRUPT BLOCK jumpers to one of VI0*-VI7*. There is no pINT strap.
        Property x = irqJumperProperty(
            "interrupt", "The VI line the board pulls when its Z80 has halted (none by default)",
            irq_);
        x.choices.erase(std::remove(x.choices.begin(), x.choices.end(), "int"), x.choices.end());
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "size";
        x.help    = "The CLOCK SELECT jumper. 8: 8-inch drives, Z80 at 4 MHz, FD1791 at 2 MHz. "
                    "5: 5.25-inch drives, Z80 at 2 MHz, FD1791 at 1 MHz";
        x.kind    = Kind::Enum;
        x.choices = {"8", "5"};
        x.get     = [this] { return Value::ofStr(size8_ ? "8" : "5"); };
        x.set     = [this](const Value& v, std::string&) {
            size8_ = v.s() == "8";
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<std::string> JadeDdBoard::statusLines() const {
    // While the host has the RAM the Z80 is stopped on BUSRQ*, halted or not.
    const char* state = cpu_.halted() ? "halted" : slvrq_ ? "stopped" : "running";
    char buf[96];
    std::snprintf(buf, sizeof buf, "board Z80  PC %04X  %s  RAM held by %s", cpu_.pc(), state,
                  slvrq_ ? "the host" : "the board");
    return {buf};
}

std::vector<MapEntry> JadeDdBoard::ioMap() const {
    return {{port_, port_, "status/control", "Disk Processor status (IN), control (OUT)"}};
}

std::vector<MapEntry> JadeDdBoard::memMap() const {
    return {{window_, (uint32_t)window_ + 0x3FF, "RAM window",
             "1 K of the board's 2 K RAM, while the host has it"}};
}

// ---------------------------------------------------------------------------
// Drives. The board probes the image size into a layout (DESIGN.md 7.3). Jade disks have an
// SD track 0 (26 x 128) and a DD system track 1 (50 x 128 in Release 2), on every disk.
// ---------------------------------------------------------------------------
namespace {

struct FmtRange {
    int     trackLo, trackHi;
    Density density;
    int     sectors;
};

constexpr uint64_t kSd = 26 * 128;  // 3,328
constexpr uint64_t kDd = 50 * 128;  // 6,400

bool describe(uint64_t bytes, std::vector<FmtRange>& ranges, std::string& err) {
    if (sizeMatches(bytes, kSd + 76 * kDd)) {  // 489,728: a double-density data disk
        ranges = {{0, 0, Density::SD, 26}, {1, 76, Density::DD, 50}};
        return true;
    }
    if (sizeMatches(bytes, kSd + kDd + 75 * kSd)) {  // 259,328: a single-density data disk
        ranges = {{0, 0, Density::SD, 26}, {1, 1, Density::DD, 50}, {2, 76, Density::SD, 26}};
        return true;
    }
    if (sizeMatches(bytes, 77 * kSd)) {  // 256,256: an IBM 3740 disk
        ranges = {{0, 76, Density::SD, 26}};
        return true;
    }
    if (bytes < 77 * kSd) {  // blank or short: unformatted, until the guest formats it
        ranges.clear();
        return true;
    }
    err = std::to_string(bytes) + " bytes is not a Jade Double D disk (489,728 double density, "
          "259,328 single density, or 256,256 IBM 3740).";
    return false;
}

int driveIndex(const std::string& unit) {
    if (unit.size() != 6 || unit.rfind("drive", 0) != 0) return -1;
    int i = unit[5] - '0';
    return (i >= 0 && i < JadeDdBoard::kDrives) ? i : -1;
}

}  // namespace

std::vector<UnitDef> JadeDdBoard::units() const {
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

bool JadeDdBoard::mount(const std::string& unit, const std::string& path, bool ro,
                        std::string& err) {
    int i = driveIndex(unit);
    if (i < 0) {
        err = "no unit `" + unit + "` on " + id + " (it has drive0..drive3)";
        return false;
    }
    auto media = openMedia(resolvePath(path), ro, err);
    if (!media) { err += pathNote(path); return false; }
    auto img = std::make_unique<DiskImage>(std::move(media));

    std::vector<FmtRange> ranges;
    if (!describe(img->size(), ranges, err)) return false;  // the old disk stays in
    img->init(77, 1, false);
    for (const FmtRange& r : ranges)
        img->initFormat(r.trackLo, r.trackHi, 0, 0, r.density, r.sectors, 128, 1);
    img->setExtendsOnWrite(true);  // a blank disk grows as the guest formats it
    const bool forcedRo = img->readOnlyForced();

    Drive& d = drive_[(size_t)i];
    d.img  = std::move(img);
    d.path = path;
    d.drv.mount(d.img.get(), ro);
    d.drv.setHeadTrack(0);
    d.drv.setFormatting(true);
    d.drv.setRevsPerSecond(size8_ ? 6 : 5);
    applySelection();

    if (forcedRo)
        log_.push_back("drive" + std::to_string(i) +
                       " mounted WRITE-PROTECTED -- the host will not let us write " + path);
    return true;
}

bool JadeDdBoard::unmount(const std::string& unit, std::string& err) {
    int i = driveIndex(unit);
    if (i < 0) { err = "no unit `" + unit + "` on " + id; return false; }
    Drive& d = drive_[(size_t)i];
    if (!d.img) { err = id + ":" + unit + " is empty"; return false; }
    d.img->sync();
    d.img.reset();
    d.path.clear();
    d.drv.eject();
    applySelection();
    return true;
}

std::vector<Property> JadeDdBoard::subUnitProperties(const std::string& table) const {
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
    return p;
}

bool JadeDdBoard::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
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
    if (path.empty()) return true;
    return mount("drive" + std::to_string(unit), path, ro, err);
}

std::vector<Board::SubUnit> JadeDdBoard::subUnits() const {
    std::vector<SubUnit> out;
    for (int i = 0; i < kDrives; ++i) {
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

std::vector<std::string> JadeDdBoard::drainLog() {
    std::vector<std::string> out = std::move(log_);
    log_.clear();
    for (auto& s : fdc_.drainLog()) out.push_back(std::move(s));
    for (auto& s : out) s = id + ":" + s;
    return out;
}

// ---------------------------------------------------------------------------
// SNAPSHOT/RESTORE (DESIGN.md 13). The whole private machine travels: the board Z80, its
// 2 K RAM, its clock, the FD1791 with any command in flight, the latches, the head
// positions, and where board time stands against host time. The straps do not (a snapshot
// restores into a machine built from the same file), nor do the disk images (host-backed).
// ---------------------------------------------------------------------------
void JadeDdBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.boolean(slvrq_);
    w.boolean(zint_);
    w.u8(cmd_);
    w.u64(timerEnd_);
    w.u64(anchorH_);
    w.u64(rem_);
    w.u64((uint64_t)lastHz_);
    w.u64(targetB_);
    w.raw(ram_, sizeof ram_);
    bclk_.serialize(w);
    cpu_.serialize(w);
    fdc_.serialize(w);
    for (const Drive& d : drive_) w.u32((uint32_t)d.drv.headTrackRaw());
}

void JadeDdBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    slvrq_   = r.boolean();
    zint_    = r.boolean();
    cmd_     = r.u8();
    timerEnd_ = r.u64();
    anchorH_ = r.u64();
    rem_     = r.u64();
    lastHz_  = (long long)r.u64();
    targetB_ = r.u64();
    r.raw(ram_, sizeof ram_);
    bclk_.deserialize(r);  // empties the board clock's queue; fdcRefresh() re-arms it
    fdcWake_ = Clock::kNone;
    cpu_.deserialize(r);
    fdc_.deserialize(r);
    for (Drive& d : drive_) d.drv.setHeadTrack((int)r.u32());
    applySelection();
    fdcRefresh();
    ioBoard_.intChanged();
    ioBoard_.nmiChanged();
    decodeChanged();
    intChanged();
    rearmHost();
}

} // namespace altair
