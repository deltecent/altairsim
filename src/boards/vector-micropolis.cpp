#include "boards/vector-micropolis.h"

#include "core/bus.h"
#include "core/clock.h"
#include "core/roms.h"
#include "core/statefile.h"
#include "host/media.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace altair {

MicropolisBoard::MicropolisBoard() { loadProm(); }

MicropolisBoard::~MicropolisBoard() {
    if (clock_) clock_->cancel(wake_);
}

void MicropolisBoard::loadProm() {
    std::memset(prom_, 0xFF, sizeof prom_);
    const BuiltinRom* rom = findRom("mboot");
    if (!rom) {
        say("built-in ROM 'mboot' is missing. SHOW ROMS lists them.");
        return;
    }
    Image       img;
    std::string err;
    if (!decodeRom(*rom, 0, img, err)) {
        say("mboot: " + err);
        return;
    }
    for (const auto& [a, b] : img.bytes)
        if (a < kProm) prom_[a] = b;
}

// ---------------------------------------------------------------------------
// Decode. A10-A15 pick the block, and the board answers reads and writes in all of it.
// ---------------------------------------------------------------------------
bool MicropolisBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::MemRead && c.type != Cycle::MemWrite) return false;
    return c.addr >= base_ && c.addr <= base_ + 0x3FFu;
}

std::vector<MapEntry> MicropolisBoard::memMap() const {
    const uint32_t b = base_;
    return {
        {b,          b + 0x0FFu, "rom",  "boot PROM"},
        {b + 0x100u, b + 0x1FFu, "read", "no chip"},
        {b + 0x200u, b + 0x3FFu, "read/write", "sector, status, data, command, repeated"},
    };
}

bool MicropolisBoard::peek(uint16_t addr, uint8_t& out) const {
    if (addr < base_ || addr >= base_ + kProm) return false;
    out = prom_[addr - base_];
    return true;
}

MicropolisBoard::Drive* MicropolisBoard::selected() {
    return (selected_ && clock_) ? &drive_[unit_] : nullptr;
}
const MicropolisBoard::Drive* MicropolisBoard::selected() const {
    return (selected_ && clock_) ? &drive_[unit_] : nullptr;
}

// A standard Rev. 1 board ties Drive Ready true (reference 7), so RDY needs only a selected drive.
bool MicropolisBoard::ready() const {
    const Drive* d = selected();
    return d && (!readyDrive_ || d->img);
}

// ---------------------------------------------------------------------------
// The timer. See the header.
// ---------------------------------------------------------------------------
void MicropolisBoard::settle() {
    if (timedOut_ || now() < timerEnd_) return;
    timedOut_ = true;
    controllerReset();
}

// RST/ clears US, the interrupt logic and the write latch (manual 1-11, 1-13).
void MicropolisBoard::controllerReset() {
    endWrite();
    selected_  = false;
    intEnable_ = false;
    if (int_) {
        int_ = false;
        intChanged();
    }
    rearm();
}

// ---------------------------------------------------------------------------
// The bus. DIG/ goes low on any read of the block, and it drives the timer (reference Q2).
// ---------------------------------------------------------------------------
uint8_t MicropolisBoard::read(const BusCycle& c) {
    settle();
    if (clock_) {
        timerEnd_ = now() + (uint64_t)clock_->hz() * kTimerSec;
        timedOut_ = false;
    }

    const uint32_t off = c.addr - (uint32_t)base_;
    if (off < kProm) return prom_[off];
    if (off < 0x200) return 0xFF;  // reference Q4
    switch (off & 3) {
    case 0:  return sectorRegister();
    case 1:  return statusRegister();
    default: return readData();
    }
}

void MicropolisBoard::write(const BusCycle& c) {
    settle();
    const uint32_t off = c.addr - (uint32_t)base_;
    if (off < 0x200) return;
    if (off & 2) writeData(c.data);
    else if (!timedOut_) command(c.data);  // RST/ is low. See the header
}

void MicropolisBoard::command(uint8_t v) {
    switch (v >> 5) {
    case 1:
        selected_ = true;
        unit_     = v & 3;
        head_     = (v >> 4) & 1;
        rearm();
        break;
    case 2:
        intEnable_ = v & 1;
        if (!intEnable_ && int_) {
            int_ = false;
            intChanged();
        }
        rearm();
        break;
    case 3:
        step(v & 1);
        break;
    case 4:
        if (Drive* d = selected(); d && d->img && ready() && !wrt_) {
            wrt_      = true;
            wrSector_ = d->spindle.sectorAt(*clock_);
            wrPos_    = 0;
            std::memset(wrSlot_, 0, sizeof wrSlot_);
            rearm();
        }
        break;
    case 5:
        controllerReset();
        break;
    default:
        break;
    }
}

void MicropolisBoard::step(bool in) {
    Drive* d = selected();
    if (!d) return;
    int next = d->track + (in ? 1 : -1);
    if (next >= 0 && next < kTracks) d->track = next;
    if (auto* dc = debugChannel(); dc && dc->on(SEEK))
        dbg::line(*dc) << "step drive=" << (int)unit_ << " track=" << d->track << "\n";
}

// ---------------------------------------------------------------------------
// Status. SECP comes from the selected drive, so with no disk turning there is no SF.
// ---------------------------------------------------------------------------
uint8_t MicropolisBoard::sectorRegister() const {
    uint8_t v = 0x10;  // W10 out, R7 pulls it up
    if (speed2Mhz_) v |= 0x20;
    if (int_) v |= 0x40;
    const Drive* d = selected();
    if (d && d->spindle.spinning()) {
        v |= (uint8_t)d->spindle.sectorAt(*clock_);
        if (d->spindle.intoSector(*clock_) < tFromUs(kSfUs)) v |= 0x80;
    }
    return v;
}

// Bit 6 is PINTE, which the Board interface does not carry, so it reads 0. Vector and Micropolis
// software does not use interrupts (Fig 1-6).
uint8_t MicropolisBoard::statusRegister() {
    const Drive* d = selected();
    if (!d) return 0x04;  // SLTD/ is low true

    uint8_t v = unit_;
    if (d->track == 0) v |= 0x08;
    if (d->img && d->img->readOnly()) v |= 0x10;
    if (ready()) v |= 0x20;
    if (transferFlag()) v |= 0x80;
    return v;
}

// A sector with no sync byte never starts the read logic, so TF stays false on it.
bool MicropolisBoard::transferFlag() {
    Drive* d = selected();
    if (!d || !d->spindle.spinning()) return false;
    const uint64_t into = d->spindle.intoSector(*clock_);
    if (wrt_) return into >= tFromUs(kWriteTfUs);
    return into >= tFromUs(kReadTfUs) && slotUnderHead()[0] != 0;
}

const uint8_t* MicropolisBoard::slotUnderHead() {
    Drive&    d      = drive_[unit_];
    const int sector = d.spindle.sectorAt(*clock_);
    const int key    = (((unit_ * 2 + head_) * kTracks + d.track) * kSectors) + sector;
    if (key != slotKey_) {
        slotKey_ = key;
        size_t n = sizeof slot_;
        if (head_ >= d.sides || !d.img->readSector(d.track, head_, sector, slot_, &n))
            std::memset(slot_, 0, sizeof slot_);
    }
    return slot_;
}

// ---------------------------------------------------------------------------
// Data. With TF false a read does not hold PRDY (manual 1-12), and no source gives the value, so
// it reads 00 (reference Q10). Each byte is the one that next finishes under the head, so a guest
// that falls behind loses bytes and no flag says so, as on the board.
// ---------------------------------------------------------------------------
uint8_t MicropolisBoard::readData() {
    if (wrt_ || !transferFlag()) return 0;

    const uint64_t into = drive_[unit_].spindle.intoSector(*clock_);
    const uint64_t sync = tFromUs(kReadSyncUs);
    const uint64_t byte = tFromUs(kByteUs);
    const uint64_t k    = into <= sync ? 0 : (into - sync + byte - 1) / byte;
    const uint64_t at   = sync + k * byte;
    holdReady((uint32_t)(at - into));
    return k < kSlot ? slotUnderHead()[k] : 0;
}

// The sync byte is taken one byte time after TF, and each next byte one byte time later. A write
// with TF false loads nothing.
void MicropolisBoard::writeData(uint8_t v) {
    if (!wrt_ || !transferFlag()) return;

    const Drive&   d    = drive_[unit_];
    const uint64_t into = d.spindle.intoSector(*clock_);
    const uint64_t at   = tFromUs(kWriteTfUs + (wrPos_ + 1) * kByteUs);
    if (at > into) holdReady((uint32_t)(std::min(at, d.spindle.tPerSector(*clock_)) - into));
    if (wrPos_ < kSlot) wrSlot_[wrPos_] = v;
    ++wrPos_;
}

// The rest of the slot stays 0, which is the postamble the shift register makes. A write that
// sent no byte still stores a sector of zeros, as the preamble went on. No source says what a
// write to a protected disk does (reference Q10), so the disk keeps its bytes.
void MicropolisBoard::endWrite() {
    if (!wrt_) return;
    wrt_     = false;
    slotKey_ = -1;

    Drive& d = drive_[unit_];
    if (!d.img) return;
    if (auto* dc = debugChannel(); dc && dc->on(SECTOR))
        dbg::line(*dc) << "write drive=" << (int)unit_ << " track=" << d.track << " side=" << (int)head_
                       << " sector=" << wrSector_ << "\n";
    if (d.img->readOnly()) {
        if (!d.roSaid) {
            d.roSaid = true;
            say(id + ": drive" + std::to_string(unit_) +
                " is write-protected, so the guest's writes are discarded");
        }
        return;
    }
    size_t n = sizeof wrSlot_;
    if (head_ >= d.sides || !d.img->writeSector(d.track, head_, wrSector_, wrSlot_, &n)) {
        say(id + ": write failed at track " + std::to_string(d.track) + " side " +
            std::to_string(head_) + " sector " + std::to_string(wrSector_));
        return;
    }
    d.img->sync();
}

// ---------------------------------------------------------------------------
// The sector boundary. See the header.
// ---------------------------------------------------------------------------
void MicropolisBoard::rearm() {
    if (!clock_) return;
    clock_->cancel(wake_);
    wake_ = Clock::kNone;
    if (!wrt_ && !intEnable_) return;
    const Drive* d = selected();
    if (!d || !d->spindle.spinning()) return;
    wake_ = clock_->at(d->spindle.nextBoundary(*clock_), [this] { tick(); });
}

void MicropolisBoard::tick() {
    wake_ = Clock::kNone;
    settle();
    endWrite();
    if (intEnable_ && !int_) {
        int_ = true;
        intChanged();
    }
    rearm();
}

// Only POC/ reaches the board: the bus interface table has no RESET line. A head stays where it is.
void MicropolisBoard::reset(Reset r) {
    if (r != Reset::PowerOn) return;
    controllerReset();
    timedOut_ = true;
    timerEnd_ = 0;
}

void MicropolisBoard::power() {
    reset(Reset::PowerOn);
}

void MicropolisBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.boolean(selected_);
    w.u8(unit_);
    w.u8(head_);
    w.boolean(intEnable_);
    w.boolean(int_);
    w.boolean(timedOut_);
    w.u64(timerEnd_);
    w.boolean(wrt_);
    w.u32((uint32_t)wrSector_);
    w.u32((uint32_t)wrPos_);
    w.raw(wrSlot_, kSlot);
    for (const auto& d : drive_) {
        w.u32((uint32_t)d.track);
        w.boolean(d.roSaid);
    }
}

void MicropolisBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    selected_  = r.boolean();
    unit_      = r.u8();
    head_      = r.u8();
    intEnable_ = r.boolean();
    int_       = r.boolean();
    timedOut_  = r.boolean();
    timerEnd_  = r.u64();
    wrt_       = r.boolean();
    wrSector_  = (int)r.u32();
    wrPos_     = r.u32();
    r.raw(wrSlot_, kSlot);
    for (auto& d : drive_) {
        d.track  = (int)r.u32();
        d.roSaid = r.boolean();
    }
    slotKey_ = -1;
    intChanged();
    rearm();
}

// ---------------------------------------------------------------------------
// Media, as on the Dual-Mode board: a `.VGI` holds side 0, then side 1. A file with a few bytes
// past the slots is still one side (reference Q9), and those bytes stay as they are.
// ---------------------------------------------------------------------------
void MicropolisBoard::probe(Drive& d) {
    const uint64_t oneSide = (uint64_t)kTracks * kSectors * kSlot;
    d.sides = sizeMatches(d.img->size(), oneSide) ? 1 : 2;
    d.img->init(kTracks, d.sides, /*interleaved=*/false);
    d.img->initFormat(0, kTracks - 1, 0, d.sides - 1, Density::DD, kSectors, (int)kSlot,
                      /*startSector=*/0);
    d.img->setExtendsOnWrite(true);
    d.spindle.configure(kRpm, kSectors);
}

std::vector<UnitDef> MicropolisBoard::units() const {
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

// "drive3" -> 3, or -1.
static int driveIndex(const std::string& unit, int count) {
    if (unit.size() != 6 || unit.rfind("drive", 0) != 0) return -1;
    int i = unit[5] - '0';
    return (i >= 0 && i < count) ? i : -1;
}

bool MicropolisBoard::mount(const std::string& unit, const std::string& path, bool ro,
                          std::string& err) {
    int i = driveIndex(unit, kDrives);
    if (i < 0) {
        err = "no unit `" + unit + "` on " + id + " (it has drive0..drive3)";
        return false;
    }

    auto media = openMedia(resolvePath(path), ro, err);
    if (!media) { err += pathNote(path); return false; }

    Drive& d = drive_[(size_t)i];
    d.img    = std::make_unique<DiskImage>(std::move(media));
    d.path   = path;
    d.roSaid = false;
    probe(d);
    slotKey_ = -1;

    if (d.img->readOnlyForced())
        say(id + ": drive" + std::to_string(i) +
            " mounted write-protected, because the host will not let us write " + path);
    return true;
}

bool MicropolisBoard::unmount(const std::string& unit, std::string& err) {
    int i = driveIndex(unit, kDrives);
    if (i < 0) { err = "no unit `" + unit + "` on " + id; return false; }

    Drive& d = drive_[(size_t)i];
    if (!d.img) { err = id + ":" + unit + " is empty"; return false; }

    d.img->sync();
    d.img.reset();
    d.path.clear();
    d.sides = 0;
    d.spindle.stop();
    slotKey_ = -1;
    rearm();
    return true;
}

std::vector<std::string> MicropolisBoard::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    return out;
}

// ---------------------------------------------------------------------------
// Properties, and the [[board.drive]] table.
// ---------------------------------------------------------------------------
std::vector<Property> MicropolisBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "base";
        x.help  = "Base address, set by W1-W4. Any 1K boundary from C000 to FC00. Vector's 56K "
                  "machines use F800, and 48K machines use D800";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0xC000;
        x.max   = 0xFC00;
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string& err) {
            if (v.i() & 0x3FF) {
                err = "W1-W4 set only A13-A10 -- base must be a 1K boundary";
                return false;
            }
            base_ = (uint16_t)v.i();
            decodeChanged();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "speed";
        x.help    = "Jumper W9: the processor speed that sector register bit 5 reports";
        x.kind    = Kind::Enum;
        x.choices = {"4mhz", "2mhz"};
        x.get     = [this] { return Value::ofStr(speed2Mhz_ ? "2mhz" : "4mhz"); };
        x.set     = [this](const Value& v, std::string&) {
            speed2Mhz_ = v.s() == "2mhz";
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "ready";
        x.help    = "RDY status: forced is true with a drive selected, the Rev. 1 standard. drive "
                    "is true only with a disk in the drive";
        x.kind    = Kind::Enum;
        x.choices = {"forced", "drive"};
        x.get     = [this] { return Value::ofStr(readyDrive_ ? "drive" : "forced"); };
        x.set     = [this](const Value& v, std::string&) {
            readyDrive_ = v.s() == "drive";
            return true;
        };
        p.push_back(std::move(x));
    }
    p.push_back(irqJumperProperty("interrupt", "Interrupt jumper, for the interrupt on each sector",
                                  irq_));
    return p;
}

std::vector<Property> MicropolisBoard::subUnitProperties(const std::string& table) const {
    if (table != "drive") return {};
    std::vector<Property> p;
    {
        Property x;
        x.name  = "unit";
        x.help  = "Which drive, 0 to 3. The boot ROM reads drive 0";
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
        x.help    = "Write-protect the disk. The guest reads it at status bit 4, and the board "
                    "drops the write";
        x.kind    = Kind::Bool;
        x.aliases = {"writeprotect"};
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "create";
        x.help = "Make the disk file (empty) if it is not there, then mount it";
        x.kind = Kind::Bool;
        p.push_back(std::move(x));
    }
    return p;
}

bool MicropolisBoard::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
    if (table != "drive") {
        err = type() + " has no [[board." + table + "]] table";
        return false;
    }

    int         unit = -1;
    std::string path;
    bool        ro     = false;
    bool        create = false;
    for (const auto& [k, v] : kv) {
        if (k == "unit") unit = std::stoi(v);
        else if (k == "mount") path = v;
        else if (k == "readonly") ro = (v == "true" || v == "1" || v == "yes" || v == "on");
        else if (k == "create") create = (v == "true" || v == "1" || v == "yes" || v == "on");
    }

    if (unit < 0) {
        err = "[[board.drive]] needs a `unit`";
        return false;
    }
    if (path.empty()) return true;

    if (create) {
        std::string     rp = resolvePath(path);
        std::error_code ec;
        if (!std::filesystem::exists(rp, ec)) {
            std::string cerr;
            if (!writeHostFile(rp, {}, cerr)) { err = cerr + pathNote(path); return false; }
        }
    }
    return mount("drive" + std::to_string(unit), path, ro, err);
}

// An empty drive writes no table.
std::vector<Board::SubUnit> MicropolisBoard::subUnits() const {
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

} // namespace altair
