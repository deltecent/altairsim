#include "boards/compupro-disk1.h"

#include "core/statefile.h"
#include "core/value.h"
#include "host/endpoint.h"
#include "host/stream.h"

namespace altair {

namespace {

// Board status register (§2.1): D7 only. D0-D6 are "not significant" and read 0 here.
constexpr uint8_t kStInt = 0x80;

// Port 3 D7 (§2.2): 1 = SPACING, 0 = MARKING.
constexpr uint8_t kSerialD7 = 0x80;

const CompuProDiskBoard::Traits kTraits = {
    "Disk 1",
    0x0800,  // a 2716: 8 routines of 256 bytes
    0x0100,
    "builtin:DISK1",
    "a 2716 (2 KB, 8 routines of 256 bytes)",
    "The boot EPROM: builtin:DISK1 (default), or a 2716 image file (Intel HEX, S-records or "
    "binary)",
    "S2-3..8",
    "Boot routine (S2-1..2 and J17): the 256-byte page of the EPROM at 0000. J17 at A "
    "adds 4. In builtin:DISK1, 4-7 are 8080/Z80 code (default 4)",
    "Boot EPROM enable (S1-4). On (default, S1-4 OFF): the ROM answers reads at 0000-00FF "
    "over PHANTOM* after a reset. Off: a plain disk controller",
    4,
};

BitSerialLine::EndpointResolver g_resolver;

} // namespace

CompuProDisk1Board::CompuProDisk1Board() : CompuProDiskBoard(kTraits) {}

void CompuProDisk1Board::setResolver(BitSerialLine::EndpointResolver r) {
    g_resolver = std::move(r);
}

// ---------------------------------------------------------------------------
// The board's own ports (§2, §2.1, §2.2). Port 0 OUT is not decoded: the Disk 1 has no
// Drive Select Register.
// ---------------------------------------------------------------------------
bool CompuProDisk1Board::decodesPort(uint8_t off, bool write) const {
    return off == 3 || (off == 2 && !write);
}

uint8_t CompuProDisk1Board::readPort(uint8_t off) {
    if (off == 2) return chip_.intPin() ? kStInt : 0;
    return line_.rx() ? 0 : kSerialD7;  // port 3: the serial input, inverted
}

void CompuProDisk1Board::writePort(uint8_t off, uint8_t v) {
    if (off != 3) return;
    latch_ = (v & kSerialD7) != 0;
    if (!motor_) line_.setTx(!latch_);
}

// RESET clears the output latch to MARKING (§2.2).
void CompuProDisk1Board::resetRegisters() {
    latch_ = false;
    if (!motor_) line_.setTx(true);
}

// ---------------------------------------------------------------------------
// Lifecycle: the line rides along with the base.
// ---------------------------------------------------------------------------
void CompuProDisk1Board::power() {
    line_.powerOn();
    CompuProDiskBoard::power();
}

void CompuProDisk1Board::pump() {
    line_.pump();
    CompuProDiskBoard::pump();
}

void CompuProDisk1Board::clockAttached() {
    CompuProDiskBoard::clockAttached();
    line_.attachClock(clock_);
}

std::vector<std::string> CompuProDisk1Board::drainLog() {
    std::vector<std::string> out = CompuProDiskBoard::drainLog();
    for (std::string& s : line_.drainLog()) out.push_back(id + ": " + s);
    return out;
}

// ---------------------------------------------------------------------------
// The serial unit. The drives are the base's; `serial` is the far end of port 3 D7.
// ---------------------------------------------------------------------------
std::vector<UnitDef> CompuProDisk1Board::units() const {
    std::vector<UnitDef> u = CompuProDiskBoard::units();
    u.push_back({"serial", UnitKind::Serial, line_.endpoint()});
    return u;
}

std::vector<Property> CompuProDisk1Board::boardUnitProperties(const std::string& unit) {
    if (unit != "serial") return {};
    return line_.properties(
        rebasingResolver(g_resolver, [this](const std::string& p) { return resolvePath(p); }));
}

bool CompuProDisk1Board::connect(const std::string& unit, const std::string& ep, std::string& err) {
    if (unit != "serial") {
        err = type() + " has no serial unit '" + unit + "' -- the line is 'serial' (the disks "
              "are drive0..drive3, via MOUNT)";
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
    line_.connect(std::move(s));
    return true;
}

bool CompuProDisk1Board::connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                                       std::string& err) {
    if (unit != "serial") {
        err = type() + " has no serial unit '" + unit + "' -- the line is 'serial'";
        return false;
    }
    line_.connect(std::move(s));
    return true;
}

bool CompuProDisk1Board::disconnect(const std::string& unit, std::string& err) {
    if (unit != "serial") {
        err = type() + " has no serial unit '" + unit + "' -- the line is 'serial'";
        return false;
    }
    line_.disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Properties.
// ---------------------------------------------------------------------------
void CompuProDisk1Board::boardProperties(std::vector<Property>& p) {
    {
        Property x;
        x.name    = "port3";
        x.help    = "S1-3: what the port 3 D7 latch drives. serial (default): the RS-232 "
                    "output line. motor: the MOTOR ON line of the minifloppy cable, D7 = 0 on";
        x.kind    = Kind::Enum;
        x.choices = {"serial", "motor"};
        x.get     = [this] { return Value::ofStr(motor_ ? "motor" : "serial"); };
        x.set     = [this](const Value& v, std::string& err) {
            if (v.s() != "serial" && v.s() != "motor") {
                err = "port3 is serial or motor";
                return false;
            }
            motor_ = v.s() == "motor";
            line_.setTx(motor_ || !latch_);  // the line rests at MARKING when not driven
            return true;
        };
        p.push_back(std::move(x));
    }
    p.push_back(irqJumperProperty(
        "interrupt", "Where the 765's INT goes (J0-J7 = vi0-vi7, J8 = int; default none, polled)",
        irq_));
}

std::vector<MapEntry> CompuProDisk1Board::ioMap() const {
    return {
        {(uint32_t)port_ + 0, (uint32_t)port_ + 0, "read", "765 main status"},
        {(uint32_t)port_ + 1, (uint32_t)port_ + 1, "read/write", "765 data"},
        {(uint32_t)port_ + 2, (uint32_t)port_ + 2, "read/write", "board status, D7 = INT (in) / DMA address, 3 bytes MSB first (out)"},
        {(uint32_t)port_ + 3, (uint32_t)port_ + 3, "read/write",
         motor_ ? "D7 = 0 motor on, 1 off (out); D0 = 0 switches the boot ROM off"
                : "D7 = serial in (in) / serial out (out), 1 = SPACING; D0 = 0 switches the boot ROM off"},
    };
}

// ---------------------------------------------------------------------------
// SNAPSHOT/RESTORE (DESIGN.md 13): the latch, the counter, the ROM flip-flop, each head's
// position, the chip and the serial line. The images are host-backed and the straps come
// from the config.
// ---------------------------------------------------------------------------
void CompuProDisk1Board::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.boolean(latch_);
    w.u32(dma_);
    w.boolean(romOn_);
    serializeHeads(w);
    chip_.serialize(w);
    line_.serialize(w);
}

void CompuProDisk1Board::deserialize(StateReader& r) {
    Board::deserialize(r);
    latch_ = r.boolean();
    dma_   = r.u32() & 0xFFFFFFu;
    romOn_ = r.boolean();
    deserializeHeads(r);
    chip_.deserialize(r);
    line_.deserialize(r);
    applyDataRate();
    refresh();
    decodeChanged();
}

} // namespace altair
