#include "boards/mits-hardsector.h"

#include "core/bus.h"
#include "core/clock.h"
#include "host/media.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace altair {

// ---------------------------------------------------------------------------
// Decode. Three ports: select/status, command/sector, data.
// ---------------------------------------------------------------------------
bool HardSectorFdc::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    uint8_t p = c.port();
    return p >= port_ && p < port_ + 3;
}

std::vector<MapEntry> HardSectorFdc::ioMap() const {
    return {
        {port_,      port_,      "select/status",  "out: drive select   in: status (INVERTED)"},
        {port_ + 1u, port_ + 1u, "command/sector", "out: step/head/write   in: sector position"},
        {port_ + 2u, port_ + 2u, "data",           "137-byte sector slots"},
    };
}

HardSectorFdc::Drive*       HardSectorFdc::selected()       { return sel_ < 0 ? nullptr : &drive_[(size_t)sel_]; }
const HardSectorFdc::Drive* HardSectorFdc::selected() const { return sel_ < 0 ? nullptr : &drive_[(size_t)sel_]; }

// A microsecond of WALL time, in T-states of whatever crystal the CPU has. These cards'
// one-shots are RC networks: they do not know or care what the processor is doing, so a
// 4 MHz machine executes twice as many instructions inside the same 30 us window.
uint64_t HardSectorFdc::tFromUs(uint64_t us) const {
    if (!clock_) return 1;
    uint64_t t = (uint64_t)clock_->hz() * us / 1000000;  // multiply FIRST
    return t ? t : 1;
}

// ---------------------------------------------------------------------------
// WHERE THE HEAD IS -- the one place rotation is computed.
//
// The sector is a reading taken off the Clock; nothing here advances anything. Reading the
// sector port does NOT bump a counter -- do that and the platter turns at the speed of
// whatever loop is polling it, the drive runs at the speed of the software watching it, and
// a recorded session stops replaying identically.
//
// Inside one sector the card's one-shots lay out like this:
//
//   0     SECTOR TRUE      read clear            137 bytes x byteUs           slack
//   |--------|-----------------|--------------------------------------------|------|
//   |  30 us |   readStartUs   |         the payload the guest can see       |
//
// SECTOR TRUE IS 30 MICROSECONDS -- not the inter-sector gap. The BIOS catches it with a
// 24-T in/rar/jc loop, so it has about two and a half spins of margin. That is tight ON
// PURPOSE: the 88-DCDD manual tells the programmer "the write mode should begin as close as
// possible to the time that D0 goes true." The hardware expected software to be right on the
// edge, and a simulator with a luxuriously wide window is not being kind, it is being wrong
// -- it hides exactly the races the real card would punish.
// ---------------------------------------------------------------------------
HardSectorFdc::Position HardSectorFdc::where() const {
    Position p;
    const Drive* d = selected();
    if (!d || !d->img || !clock_ || !d->spindle.spinning()) return p;

    p.spinning = true;
    p.sector   = d->spindle.sectorAt(*clock_);

    uint64_t into  = d->spindle.intoSector(*clock_);
    uint64_t start = tFromUs(readStartUs());

    p.sectorTrue = into < tFromUs(kSectorTrueUs);

    if (into < start) {
        p.byteIndex = -1;  // the read path is still held cleared
    } else {
        uint64_t i  = (into - start) / tPerByte();
        p.byteIndex = (int)(i < (uint64_t)kHsSectorBytes ? i : kHsSectorBytes - 1);
    }

    // The write circuit asks for its first byte writeStartUs in, and another every byteUs
    // after that. ENWD is that request, so the guest cannot run ahead of the head any more
    // than it can on the read side.
    uint64_t wstart = tFromUs(writeStartUs());
    if (into < wstart) {
        p.writeIndex = -1;
    } else {
        uint64_t i   = (into - wstart) / tPerByte();
        p.writeIndex = (int)(i < (uint64_t)kHsSectorBytes ? i : kHsSectorBytes - 1);
    }
    return p;
}

// ---------------------------------------------------------------------------
// THE READ HEAD DOES NOT WAIT TO BE ASKED.
//
// The card's shift register clocks bytes off the medium continuously, whether or not the
// guest is looking -- the same fact as rotation, one level down. So the slot under the head
// is loaded HERE, from every access, and not as a side effect of reading the sector port.
//
// It used to be exactly that side effect, and CP/M booted anyway, because its BIOS always
// polls the sector port before it touches the data port. The bug was hiding behind a habit
// of the software: a guest that read the data port across a sector boundary without polling
// first got STALE BYTES FROM THE PREVIOUS SECTOR, and one that never polled at all got an
// empty buffer. tests/test_dcdd.cpp caught it; the boot could not, and never would have.
// ---------------------------------------------------------------------------
void HardSectorFdc::syncSector(const Position& pos) {
    Drive* d = selected();
    if (!d || !d->img || !headLoaded(*d) || !pos.spinning) return;
    if (pos.sector == bufSector_) return;

    flushWrite();  // the position we are leaving is the one the pending write needs

    bufSector_ = pos.sector;
    readPos_   = 0;
    std::memset(buf_, 0, sizeof buf_);

    size_t n = sizeof buf_;
    if (!d->img->readSector(d->track, 0, pos.sector, buf_, &n)) {
        char m[128];
        std::snprintf(m, sizeof m, "%s: read failed at track %d sector %d", id.c_str(), d->track,
                      pos.sector);
        say(m);
    }
}

uint8_t HardSectorFdc::read(const BusCycle& c) {
    uint8_t p = (uint8_t)(c.port() - port_);
    Drive*  d = selected();

    // ---- base+0: STATUS. Inverted, and 0xFF when nothing is answering. ----
    if (p == 0) {
        // ~0x00: every flag false. Which is the truth -- and on the 88-MDS it is also the
        // truth for an empty drive and for a card whose disable timer has run out.
        if (!d || !online(*d)) return 0xFF;

        Position pos = where();
        syncSector(pos);

        st_.dsken  = true;
        st_.track0 = (d->track == 0);
        st_.hdstat = headLoaded(*d);
        st_.moveok = moveOk(*d);

        // ENWD -- "the write circuit wants the next byte". Paced by the head, exactly like
        // NRDA: first at writeStartUs, then one every byteUs.
        st_.enwd = writing_ && pos.spinning && pos.writeIndex >= 0 &&
                   writePos_ < kHsSectorBytes && writePos_ <= pos.writeIndex;

        // NRDA: has the disk turned past a byte the guest has not taken yet? The guest
        // cannot read ahead of the medium -- that is the whole point of the flag, and it is
        // what paces BOOT.ASM's cycle-counted transfer loop.
        st_.nrda = headLoaded(*d) && pos.spinning && pos.byteIndex >= 0 &&
                   readPos_ < kHsSectorBytes && readPos_ <= pos.byteIndex;

        uint8_t v = 0x10;  // D4: "Not used, always = 0 when Drive is enabled" -- and it MEANS
                           // it. The bit reads ZERO on an enabled controller, so it has to be
                           // SET here, before the inversion, or it comes out as a one. Both
                           // manuals say this about both cards; the 88-MDS manual is the
                           // blunter of the two (p31), and its front-panel checkout on p74
                           // shows D3 and D4 dark. D3 (dsken) is the same idea: an enabled
                           // card pulls the bit low, and a card that is not there floats the
                           // whole byte to FF.
        if (st_.enwd)   v |= 0x01;
        if (st_.moveok) v |= 0x02;
        if (st_.hdstat) v |= 0x04;
        if (st_.dsken)  v |= 0x08;
        if (st_.inten)  v |= 0x20;
        if (st_.track0) v |= 0x40;
        if (st_.nrda)   v |= 0x80;
        return (uint8_t)~v;  // THE INVERSION. Once, here, on the way out.
    }

    // ---- base+1: SECTOR POSITION. ----
    //
    //     | 1 | 1 | sector[4:0] | T |     T = sector true, ACTIVE LOW
    //
    // The sector sits in bits 5..1 so that the BIOS's single RAR can drop T into carry AND
    // shift the sector number down into bits 4..0 in one instruction.
    //
    // sectorChannelLive() is the 88-MDS's: "The Sector position channel will be disabled
    // (all 1s) for 1 second after the Drive is enabled, and 50 ms after a step command is
    // issued" (manual p34). The 88-DCDD never gates it.
    if (p == 1) {
        if (!d || !online(*d) || !headLoaded(*d) || !sectorChannelLive()) return 0xFF;
        Position pos = where();
        if (!pos.spinning) return 0xFF;
        syncSector(pos);

        uint8_t v = 0xC0;                          // bits 7,6 read as 1
        v |= (uint8_t)((pos.sector << 1) & 0x3E);  // sector in bits 5..1
        if (!pos.sectorTrue) v |= 0x01;            // T is LOW when true
        return v;
    }

    // ---- base+2: DATA. ----
    if (!d || !online(*d) || !headLoaded(*d)) return 0xFF;
    syncSector(where());  // the head has kept turning since you last looked
    if (readPos_ >= kHsSectorBytes) return 0xFF;
    return buf_[readPos_++];
}

// ---------------------------------------------------------------------------
// Write.
// ---------------------------------------------------------------------------
void HardSectorFdc::write(const BusCycle& c) {
    uint8_t p = (uint8_t)(c.port() - port_);

    // Bit 7 turns the system off; otherwise the low bits pick a drive. The 88-DCDD reads
    // four of them (sixteen drives on the daisy chain), the 88-MDS only two (four drives).
    if (p == 0) { selectDrive((c.data & 0x80) ? -1 : (c.data & selectMask())); return; }
    if (p == 1) { command(c.data); return; }

    // ---- base+2: DATA. Exactly 137 bytes, then commit. ----
    if (!writing_) return;  // not armed: the byte goes nowhere, as on the real card
    if (writePos_ < kHsSectorBytes) wbuf_[writePos_++] = c.data;
    if (writePos_ == kHsSectorBytes) flushWrite();
}

// ---------------------------------------------------------------------------
// Drive select.
//
// The flush comes FIRST, before anything is invalidated -- the buffer being flushed is
// positioned at the track and sector we are about to forget. Do it the other way round and
// the write lands wherever the new selection happens to be, which is the doc's "silently
// corrupts disks".
// ---------------------------------------------------------------------------
void HardSectorFdc::selectDrive(int n) {
    flushWrite();

    if (n >= drives_) {
        char m[96];
        std::snprintf(m, sizeof m, "%s: no drive %d (this card has %d)", id.c_str(), n, drives_);
        say(m);
        n = -1;
    }

    // The card sees the TRANSITION, not just the destination -- the 88-MDS's motor-on delay
    // is fired by the Disk Enable flip-flop toggling, so it has to know whether the system
    // was already on.
    onSelect(sel_, n);

    if (n < 0) {
        sel_ = -1;
        st_  = Status{};
        invalidatePosition();
        return;
    }
    if (n != sel_) {
        sel_ = n;
        invalidatePosition();
    }
}

// ---------------------------------------------------------------------------
// WRITE ENABLE -- arm the write. 137 bytes, starting now.
// ---------------------------------------------------------------------------
void HardSectorFdc::armWrite() {
    Drive* d = selected();
    if (!d) return;

    Position pos = where();
    if (d->img && d->img->readOnly()) {
        char m[128];
        std::snprintf(m, sizeof m, "%s: drive%d is write-protected", id.c_str(), sel_);
        say(m);
        return;
    }
    writing_  = true;
    writePos_ = 0;
    wTrack_   = d->track;
    wSector_  = pos.spinning ? pos.sector : -1;
    std::memset(wbuf_, 0, sizeof wbuf_);
}

// ---------------------------------------------------------------------------
// COMMIT A PENDING WRITE -- and the partial ones are the whole point.
//
// A system sector is 133 bytes (SSECLEN) and NEVER reaches 137. A card that waited for the
// 137th byte before committing would lose every system sector ever written -- which is the
// doc's "System tracks corrupt on write", and it would only show up on a disk you had booted
// from. So: whatever the guest gave us, we write.
//
// Called BEFORE any invalidation, from every path that moves the head or the selection:
// select, step, head-unload, and crossing a sector boundary.
// ---------------------------------------------------------------------------
void HardSectorFdc::flushWrite() {
    if (!writing_) return;
    writing_ = false;

    Drive* d = selected();
    if (!d || !d->img || writePos_ == 0 || wSector_ < 0) { writePos_ = 0; return; }

    // THE TAIL OF A SHORT WRITE IS THE LAST BYTE, NOT ZERO -- and that is the card, not a
    // convention. The 88-DCDD manual: "Write circuit will continue writing LAST BYTE
    // OUTPUTTED on CH #012 to the end of that sector." The shift register simply keeps
    // re-clocking whatever it was last handed. Which is why the manual also tells the
    // programmer that "the last or 138th byte written must be a 000" -- that trailing zero is
    // not a terminator, it is the FILL PATTERN, and the reason a 133-byte system sector ends
    // up with four zeros after it on real media. The 88-MDS manual says the same thing in the
    // same words (p32: "The last or 138th byte written must be all zeros (000). This pattern
    // will be written to the end of the Sector.").
    //
    // Software does write the zero, so memset would have produced the same bytes here in
    // every period case. It is written this way round because the card's rule is the one that
    // is true, and the next person to read this should not have to rediscover that the zeros
    // were a coincidence.
    for (int i = writePos_; i < kHsSectorBytes; ++i) wbuf_[i] = wbuf_[writePos_ - 1];

    size_t n = kHsSectorBytes;
    if (!d->img->writeSector(wTrack_, 0, wSector_, wbuf_, &n)) {
        char m[128];
        std::snprintf(m, sizeof m, "%s: write failed at track %d sector %d", id.c_str(), wTrack_,
                      wSector_);
        say(m);
    } else {
        d->img->sync();
    }
    writePos_ = 0;
    if (bufSector_ == wSector_) bufSector_ = -1;  // the buffer we hold is now stale
}

void HardSectorFdc::invalidatePosition() {
    bufSector_ = -1;
    readPos_   = 0;
    st_.nrda   = false;
}

// ---------------------------------------------------------------------------
// RESET (DESIGN.md 6.1). Deselect, unload, invalidate -- but KEEP THE IMAGES MOUNTED and
// DO NOT SEEK TO TRACK 0. A real drive does not move its head because the CPU was reset, and
// a warm reset that homed every drive would be inventing a convenience the hardware never
// had.
// ---------------------------------------------------------------------------
void HardSectorFdc::reset(Reset) {
    flushWrite();
    int old = sel_;
    sel_ = -1;
    onSelect(old, -1);  // the 88-MDS's motor stops; the 88-DCDD's does not have one
    st_ = Status{};
    for (auto& d : drive_) d.loaded = false;
    invalidatePosition();
}

// ---------------------------------------------------------------------------
// The probe. THE BOARD DOES THIS, not DiskImage (DESIGN.md 7.3).
// ---------------------------------------------------------------------------
bool HardSectorFdc::probe(Drive& d, std::string& err) {
    uint64_t got = d.img->size();

    const HsFormat* hit = nullptr;
    for (const auto& f : formats()) {
        if (!d.forced.empty()) {
            if (d.forced == f.name) hit = &f;
            continue;
        }
        // sizeMatches(), NOT `==`: the real images are XMODEM-padded up to a 128-byte block
        // boundary and a strict compare rejects every disk anyone actually has. Both 8"
        // images in the tree are 337,664 (not 337,568), and all four minidisk images are
        // 76,800 (not 76,720) -- 76,720 is not even a multiple of 128, so that format needs
        // the tolerance quite as much as the 8" one does.
        if (sizeMatches(got, f.bytes)) { hit = &f; break; }
    }

    if (!hit) {
        std::string sizes;
        for (const auto& f : formats()) {
            if (!sizes.empty()) sizes += ", ";
            sizes += std::string(f.name) + "=" + std::to_string(f.bytes);
        }
        err = std::to_string(got) + " bytes matches no " + type() + " format (" + sizes +
              "). Set `media` to force one.";
        return false;
    }

    d.fmt = *hit;
    d.img->init(hit->tracks, 1, /*interleaved=*/false);
    d.img->initFormat(0, hit->tracks - 1, 0, 0, Density::SD, hit->sectors, kHsSectorBytes,
                      /*startSector=*/0);  // ZERO. The Tarbell's is one.

    // The RPM is the CARD's -- 360 for an 8" Pertec, 300 for a 5.25" minidisk (88-MDS manual
    // p4: "Rotational Speed -- 300 rpm (200 ms/rev.)") -- and the sector count is the
    // MEDIUM's. Getting this wrong is silent: the disk still reads, it just turns at a speed
    // no such drive ever turned at.
    d.spindle.configure(rpm(), hit->sectors);
    return true;
}

// ---------------------------------------------------------------------------
// Units, MOUNT, UNMOUNT.
// ---------------------------------------------------------------------------
std::vector<UnitDef> HardSectorFdc::units() const {
    std::vector<UnitDef> u;
    for (int i = 0; i < drives_; ++i) {
        UnitDef x;
        x.name  = "drive" + std::to_string(i);
        x.kind  = UnitKind::Disk;
        x.state = drive_[(size_t)i].img ? drive_[(size_t)i].path : "(empty)";
        u.push_back(std::move(x));
    }
    return u;
}

// "drive3" -> 3, or -1.
static int driveIndex(const std::string& unit, int count) {
    if (unit.rfind("drive", 0) != 0) return -1;
    const std::string n = unit.substr(5);
    if (n.empty()) return -1;
    for (char ch : n)
        if (ch < '0' || ch > '9') return -1;
    int i = std::stoi(n);
    return (i >= 0 && i < count) ? i : -1;
}

bool HardSectorFdc::mount(const std::string& unit, const std::string& path, bool ro,
                          std::string& err) {
    int i = driveIndex(unit, drives_);
    if (i < 0) {
        err = "no unit `" + unit + "` on " + id + " (it has drive0.." +
              std::to_string(drives_ - 1) + ")";
        return false;
    }

    // WHERE WE LOOK is resolvePath(); WHAT WE REMEMBER is `path`, as it was written
    // (core/board.h). A disk mounted by a machine file sitting in disks/mits-88mds/
    // says `mount = "CPM56K-1.DSK"` and means the disk beside it -- but SHOW and
    // CONFIG SAVE must still say `CPM56K-1.DSK`, or the file would not load from its
    // own directory the next time.
    //
    // Nothing re-opens the medium afterwards: HostFile holds the handle it was given
    // and syncs back through it, so resolving once, here, is resolving for good.
    auto media = openMedia(resolvePath(path), ro, err);
    if (!media) return false;

    Drive& d = drive_[(size_t)i];
    Drive  fresh;             // probe into a FRESH drive: a probe that fails must not leave
    fresh.forced = d.forced;  // the old disk half-replaced
    fresh.track  = d.track;
    fresh.img    = std::make_unique<DiskImage>(std::move(media));
    fresh.path   = path;

    if (!probe(fresh, err)) return false;

    if (fresh.img->readOnlyForced()) {
        char m[192];
        std::snprintf(m, sizeof m,
                      "%s: drive%d mounted READ-ONLY -- the host will not let us write %s",
                      id.c_str(), i, path.c_str());
        say(m);
    }

    if (sel_ == i) { flushWrite(); invalidatePosition(); }
    d.img     = std::move(fresh.img);
    d.path    = std::move(fresh.path);
    d.fmt     = fresh.fmt;
    d.spindle = fresh.spindle;
    return true;
}

bool HardSectorFdc::unmount(const std::string& unit, std::string& err) {
    int i = driveIndex(unit, drives_);
    if (i < 0) { err = "no unit `" + unit + "` on " + id; return false; }

    Drive& d = drive_[(size_t)i];
    if (!d.img) { err = id + ":" + unit + " is empty"; return false; }

    if (sel_ == i) { flushWrite(); invalidatePosition(); }
    d.img->sync();
    d.img.reset();
    d.path.clear();
    d.spindle.stop();  // no disk, no rotation. sectorAt() reads 0 and arms nothing.
    d.loaded = false;
    return true;
}

std::vector<std::string> HardSectorFdc::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    return out;
}

// ---------------------------------------------------------------------------
// Properties, and the [[board.drive]] sub-unit table.
// ---------------------------------------------------------------------------
std::vector<Property> HardSectorFdc::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Base address. The card decodes three ports: BASE+0 .. BASE+2";
        x.kind  = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xFD;
        x.get   = [this] { return Value::ofInt(port_); };
        x.set   = [this](const Value& v, std::string&) {
            port_ = (uint16_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "drives";
        x.help  = "Drives on the daisy chain";
        x.kind  = Kind::Int;
        x.radix = 10;  // NEVER on the wire -> DECIMAL. It is a count of things.
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
            if (sel_ >= n) sel_ = -1;
            drives_ = n;
            drive_.resize((size_t)n);
            onDrivesChanged();
            return true;
        };
        p.push_back(std::move(x));
    }
    p.push_back(irqJumperProperty("interrupt", "Where the card's interrupt is soldered", irq_));
    return p;
}

bool HardSectorFdc::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
    if (table != "drive") {
        err = type() + " has no [[board." + table + "]] table";
        return false;
    }

    int         unit = -1;
    std::string path, media;
    bool        ro = false;

    for (const auto& [k, v] : kv) {
        if (k == "unit") {
            unit = std::stoi(v);
        } else if (k == "mount") {
            path = v;
        } else if (k == "readonly") {
            ro = (v == "true" || v == "1" || v == "yes");
        } else if (k == "media") {
            bool        ok = false;
            std::string choices;
            for (const auto& f : formats()) {
                if (v == f.name) ok = true;
                if (!choices.empty()) choices += ", ";
                choices += f.name;
            }
            if (!ok) {
                err = "unknown media `" + v + "` on a " + type() + " (" + choices + ")";
                return false;
            }
            media = v;
        } else {
            err = "[[board.drive]] has no `" + k + "`";
            return false;
        }
    }

    if (unit < 0 || unit >= drives_) {
        err = "[[board.drive]] needs unit = 0.." + std::to_string(drives_ - 1);
        return false;
    }

    drive_[(size_t)unit].forced = media;
    if (path.empty()) return true;
    return mount("drive" + std::to_string(unit), path, ro, err);
}

// The inverse (DESIGN.md 5). An EMPTY drive with no forced media has nothing to say, so it
// writes no table at all -- a machine with four drives and one disk in it should save as one
// [[board.drive]], not four, three of which are noise.
std::vector<Board::SubUnit> HardSectorFdc::subUnits() const {
    std::vector<SubUnit> out;
    for (int i = 0; i < drives_; ++i) {
        const Drive& d = drive_[(size_t)i];
        if (!d.img && d.forced.empty()) continue;

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

} // namespace altair
