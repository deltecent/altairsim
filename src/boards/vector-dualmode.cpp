#include "boards/vector-dualmode.h"

#include "core/bus.h"
#include "core/clock.h"
#include "core/statefile.h"
#include "host/media.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace altair {

// ---------------------------------------------------------------------------
// Decode. A3 and A2 must be 0, so base+4 to base+F are not decoded. A1 and A0 pick the port.
// ---------------------------------------------------------------------------
bool DualModeBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    return (c.port() & 0xFC) == port_;
}

std::vector<MapEntry> DualModeBoard::ioMap() const {
    return {
        {port_,      port_,      "control 0/status 0", "drive, side, step, direction / status 0"},
        {port_ + 1u, port_ + 1u, "control 1/status 1", "sector, read/write, ECC / status 1"},
        {port_ + 2u, port_ + 2u, "data",               "one byte of the 512-byte RAM"},
        {port_ + 3u, port_ + 3u, "start/reset",        "OUT starts a sector, IN clears the counter"},
    };
}

DualModeBoard::Drive* DualModeBoard::selected() {
    return motorOn() ? &drive_[c0_ & 3] : nullptr;
}
const DualModeBoard::Drive* DualModeBoard::selected() const {
    return motorOn() ? &drive_[c0_ & 3] : nullptr;
}

uint32_t DualModeBoard::ecc(const uint8_t* p, size_t n) {
    constexpr uint32_t kPoly = 0x0104C981;  // the taps, without x^32
    uint32_t reg = 0;
    for (size_t i = 0; i < n; ++i)
        for (int bit = 7; bit >= 0; --bit) {
            bool feedback = ((reg >> 31) ^ (uint32_t)(p[i] >> bit)) & 1;
            reg <<= 1;
            if (feedback) reg ^= kPoly;
        }
    return reg;
}

// ---------------------------------------------------------------------------
// Ports.
// ---------------------------------------------------------------------------
uint8_t DualModeBoard::read(const BusCycle& c) {
    if (busy()) return 0xFF;  // BUSY* gates the decode. See the header.

    switch (c.port() & 3) {
    case 0: {
        // Bits 0-4 come through an inverting 74LS368 from lines that Z1 holds high, so a bit
        // with no drive behind it reads 0. READY, WRITE FAULT and SEEK COMPLETE are the hard
        // disk's. Bit 5 reads 1 because a floppy clears the one-shot U60. Z2 pulls up 6 and 7.
        uint8_t v = 0xE0;
        if (const Drive* d = selected()) {
            if (d->img && d->img->readOnly()) v |= 0x01;
            if (d->track == 0) v |= 0x04;
        }
        return v;
    }
    case 1: {
        // Bit 0: the floppy-only jumper makes every drive select decode a floppy. Bit 1: tied
        // to ground. Bit 3: tied to ground, the type of hard disk. Z2 pulls up bits 4-7.
        uint8_t v = 0xF1;
        if (motorOn()) v |= 0x04;
        return v;
    }
    case 2: {
        uint8_t v = ram_[counter_];
        counter_ = (counter_ + 1) % kRamBytes;
        return v;
    }
    default:
        counter_ = 0;
        return 0xFF;  // nothing drives the bus. PHYSWRITE keeps this value (reference Q3)
    }
}

void DualModeBoard::write(const BusCycle& c) {
    if (busy()) return;  // lost

    switch (c.port() & 3) {
    case 0: {
        bool rising = (c.data & 0x20) && !(c0_ & 0x20);
        c0_ = c.data;
        if (clock_) motorEnd_ = now() + (uint64_t)clock_->hz() * kMotorSec;  // WR0* triggers U60
        if (rising) {
            Drive& d    = drive_[c0_ & 3];
            int    next = d.track + ((c0_ & 0x40) ? 1 : -1);
            if (next >= 0 && next < kTracks) d.track = next;
            if (auto* dc = debugChannel(); dc && dc->on(SEEK))
                dbg::line(*dc) << "step drive=" << (c0_ & 3) << " track=" << d.track << "\n";
        }
        break;
    }
    case 1:
        c1_ = c.data;
        break;
    case 2:
        ram_[counter_] = c.data;
        counter_ = (counter_ + 1) % kRamBytes;
        break;
    default:
        start();
        break;
    }
}

// ---------------------------------------------------------------------------
// START (reference §4). U24 goes busy, and U17 compares the latched sector against the sector
// counter. A sector pulse with a compare runs the transfer, and the next pulse ends busy, so
// busy lasts until the wanted sector comes around plus one sector time.
//
// MOT presets the first flip-flop, so a START with the timer ended does not stay busy, and the
// timer ending frees a board that is waiting for a pulse that never comes. A transfer the timer
// cuts short does not happen at all here. A real board would leave part of a sector behind.
// ---------------------------------------------------------------------------
void DualModeBoard::start() {
    Drive* d = selected();
    if (!d) return;

    busyEnd_ = motorEnd_;
    if (!d->spindle.spinning()) return;  // no disk, no pulses

    const uint64_t perSector = d->spindle.tPerSector(*clock_);
    const int      wanted    = c1_ & 0x0F;
    const int      next      = (d->spindle.sectorAt(*clock_) + 1) % kSectors;
    const uint64_t begin = d->spindle.nextBoundary(*clock_) +
                           (uint64_t)((wanted - next + kSectors) % kSectors) * perSector;
    const uint64_t end = begin + perSector;
    if (end > motorEnd_) return;

    busyEnd_ = end;
    if (auto* dc = debugChannel(); dc && dc->on(SECTOR))
        dbg::line(*dc) << (isReadCmd() ? "read" : "write") << " drive=" << (c0_ & 3)
                       << " track=" << d->track << " side=" << ((c0_ >> 2) & 1)
                       << " sector=" << wanted << "\n";
    if (isReadCmd()) readSlot(*d, wanted);
    else             writeSlot(*d, wanted);
}

// The preamble is never stored, so RAM takes the bytes from the sync byte on, and the 0s after
// the slot are the postamble. A sector with no sync stores nothing and RAM keeps what it held.
//
// With ECC ENABLE the hardware makes the ECC again as the bytes go by, and the four bytes that
// reach RAM are the difference: 00 on a match, a syndrome if not (reference 1-6). Without it,
// they are the bytes on the disk.
void DualModeBoard::readSlot(Drive& d, int sector) {
    uint8_t slot[kSlot];
    size_t  n = sizeof slot;
    int     side = (c0_ >> 2) & 1;
    if (side >= d.sides || !d.img->readSector(d.track, side, sector, slot, &n) || !slot[0]) return;

    if (eccEnabled()) {
        uint32_t made = ecc(slot, kEccAt);
        for (size_t i = 0; i < 4; ++i)
            slot[kEccAt + i] ^= (uint8_t)(made >> (24 - 8 * i));
    }
    for (size_t i = 0; i < kRamBytes; ++i)
        ram_[(counter_ + i) % kRamBytes] = i < kSlot ? slot[i] : 0;
}

// The preamble is 0s and sync is the first byte that is not, so that byte starts the slot. All
// 0s leaves a slot of 0s, which is an unformatted sector. The ECC goes in at a RAM address, not
// at an offset from the sync byte, because U51 times it from the address (reference §7).
void DualModeBoard::writeSlot(Drive& d, int sector) {
    size_t skip = 0;
    while (skip < kRamBytes && !ram_[(counter_ + skip) % kRamBytes]) ++skip;
    const size_t sync = (counter_ + (skip % kRamBytes)) % kRamBytes;

    uint8_t slot[kSlot];
    for (size_t i = 0; i < kSlot; ++i) slot[i] = ram_[(sync + i) % kRamBytes];

    if (eccEnabled() && kEccRam >= sync && kEccRam - sync + 4 <= kSlot) {
        size_t   at = kEccRam - sync;
        uint32_t e  = ecc(slot, at);
        for (size_t i = 0; i < 4; ++i) slot[at + i] = (uint8_t)(e >> (24 - 8 * i));
    }

    // The controller has no write-protect logic. The drive refuses the write, and the driver
    // tests the bit afterwards (reference Q4), so a protected write must end with no error.
    if (d.img->readOnly()) {
        if (!d.roSaid) {
            d.roSaid = true;
            say(id + ": drive" + std::to_string(c0_ & 3) +
                " is write-protected, so the guest's writes are discarded");
        }
        return;
    }

    size_t n    = sizeof slot;
    int    side = (c0_ >> 2) & 1;
    if (side >= d.sides || !d.img->writeSector(d.track, side, sector, slot, &n)) {
        say(id + ": write failed at track " + std::to_string(d.track) + " side " +
            std::to_string(side) + " sector " + std::to_string(sector));
        return;
    }
    d.img->sync();
}

// POC* clears both latches and the motor one-shot, and the board is not busy. The schematic
// takes neither from RESET*. A head stays where it is on both.
void DualModeBoard::reset(Reset r) {
    if (r != Reset::PowerOn) return;
    c0_ = c1_ = 0;
    motorEnd_ = busyEnd_ = 0;
}

void DualModeBoard::power() {
    std::memset(ram_, 0, sizeof ram_);
    counter_ = 0;
    c0_ = c1_ = 0;
    motorEnd_ = busyEnd_ = 0;
}

void DualModeBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u8(c0_);
    w.u8(c1_);
    w.raw(ram_, kRamBytes);
    w.u32(counter_);
    w.u64(motorEnd_);
    w.u64(busyEnd_);
    for (const auto& d : drive_) {
        w.u32((uint32_t)d.track);
        w.boolean(d.roSaid);
    }
}

void DualModeBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    c0_ = r.u8();
    c1_ = r.u8();
    r.raw(ram_, kRamBytes);
    counter_  = (uint16_t)r.u32();
    motorEnd_ = r.u64();
    busyEnd_  = r.u64();
    for (auto& d : drive_) {
        d.track  = (int)r.u32();
        d.roSaid = r.boolean();
    }
}

// ---------------------------------------------------------------------------
// Media. The board probes, because only the board knows what a size means (DESIGN.md 7.3). A
// `.vgi` holds side 0, then side 1. Any other size is an unformatted disk at full reach, which
// the guest's own format program fills in. Side 0 alone then ends at the one-sided size.
// ---------------------------------------------------------------------------
void DualModeBoard::probe(Drive& d) {
    const uint64_t oneSide = (uint64_t)kTracks * kSectors * kSlot;
    d.sides = sizeMatches(d.img->size(), oneSide) ? 1 : 2;
    d.img->init(kTracks, d.sides, /*interleaved=*/false);
    d.img->initFormat(0, kTracks - 1, 0, d.sides - 1, Density::DD, kSectors, (int)kSlot,
                      /*startSector=*/0);
    d.img->setExtendsOnWrite(true);
    d.spindle.configure(kRpm, kSectors);
}

std::vector<UnitDef> DualModeBoard::units() const {
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

bool DualModeBoard::mount(const std::string& unit, const std::string& path, bool ro,
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

    if (d.img->readOnlyForced())
        say(id + ": drive" + std::to_string(i) +
            " mounted write-protected, because the host will not let us write " + path);
    return true;
}

bool DualModeBoard::unmount(const std::string& unit, std::string& err) {
    int i = driveIndex(unit, kDrives);
    if (i < 0) { err = "no unit `" + unit + "` on " + id; return false; }

    Drive& d = drive_[(size_t)i];
    if (!d.img) { err = id + ":" + unit + " is empty"; return false; }

    d.img->sync();
    d.img.reset();
    d.path.clear();
    d.sides = 0;
    d.spindle.stop();
    return true;
}

std::vector<std::string> DualModeBoard::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    return out;
}

// ---------------------------------------------------------------------------
// Properties, and the [[board.drive]] table.
// ---------------------------------------------------------------------------
std::vector<Property> DualModeBoard::properties() {
    std::vector<Property> p;
    Property x;
    x.name  = "port";
    x.help  = "Base address, set by jumper block D. The board decodes BASE+0 .. BASE+3";
    x.kind  = Kind::Int;
    x.radix = 16;
    x.min   = 0;
    x.max   = 0xF0;
    x.get   = [this] { return Value::ofInt(port_); };
    x.set   = [this](const Value& v, std::string& err) {
        if (v.i() & 0x0F) {
            err = "jumper block D sets only A7-A4 -- port must end in 0";
            return false;
        }
        port_ = (uint16_t)v.i();
        return true;
    };
    p.push_back(std::move(x));
    return p;
}

std::vector<Property> DualModeBoard::subUnitProperties(const std::string& table) const {
    if (table != "drive") return {};
    std::vector<Property> p;
    {
        Property x;
        x.name  = "unit";
        x.help  = "Which drive, 0 to 3. Monitor 4.3 boots from drive 1";
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
        x.help    = "Write-protect the disk. The guest reads it at status 0 bit 0, and the drive "
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

bool DualModeBoard::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
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
std::vector<Board::SubUnit> DualModeBoard::subUnits() const {
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
