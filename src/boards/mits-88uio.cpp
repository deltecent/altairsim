#include "boards/mits-88uio.h"

#include "core/statefile.h"
#include "host/tapemodem.h"

#include <utility>

namespace altair {

UioBoard::UioBoard() {
    // AcrBoard() has already strapped the cassette section: base_ = 0x06, 300 baud, 8N1,
    // Rev 1, interrupt pads left open. The serial section carries Sio2Port's own default
    // base of 0x10 -- 2SIO Port A, the SW-2-OFF address -- so nothing here needs setting.
    // motorOn_ (relay closed) and standard_ ("mits") hold their power-up defaults.
}

bool UioBoard::isSerial(const std::string& unit) { return lowerAscii(unit) == "serial"; }

// ---------------------------------------------------------------------------
// The bus. Two disjoint port ranges on one card: the serial section answers first,
// the inherited cassette (SioBoard) answers for 0x06/0x07.
// ---------------------------------------------------------------------------
bool UioBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if ((c.type == Cycle::IoRead || c.type == Cycle::IoWrite) && serial_.decodesPort(c.port()))
        return true;
    return SioBoard::decodes(c);  // the cassette section's control/data pair
}

uint8_t UioBoard::read(const BusCycle& c) {
    if (c.type == Cycle::IoRead && serial_.decodesPort(c.port()))
        return serial_.read(c.port());
    return SioBoard::read(c);  // cassette status (0x06) / data (0x07)
}

void UioBoard::write(const BusCycle& c) {
    if (c.type == Cycle::IoWrite && serial_.decodesPort(c.port())) {
        serial_.write(c.port(), c.data);
        return;
    }

    // THE CASSETTE CONTROL PORT DOUBLES AS THE MOTOR-RELAY REGISTER on this card. Latch
    // the relay from D6/D7 and then STILL hand the write to SioBoard, so the two
    // interrupt-enable flip-flops (D0/D1) it also carries are set as usual -- the motor
    // bits and the enable bits do not collide. The one thing this must never do is
    // corrupt the UART or the tape, which is why it swallows nothing: it only reads D6/D7.
    if (c.type == Cycle::IoWrite && c.port() == base_) {
        if (!(c.data & 0x80)) motorOn_ = true;        // D7 low  -> motor ON  (OUT 6,127)
        else if (!(c.data & 0x40)) motorOn_ = false;  // D6 low  -> motor OFF (OUT 6,191)
    }

    SioBoard::write(c);  // cassette data, or the control channel's interrupt enables
}

// ---------------------------------------------------------------------------
// pin 73 / VI0-VI7: either section can be strapped and asking, so OR them.
// ---------------------------------------------------------------------------
bool UioBoard::assertsInt() const { return serial_.assertsInt() || SioBoard::assertsInt(); }

uint8_t UioBoard::assertsVi() const {
    return (uint8_t)(serial_.assertsVi() | SioBoard::assertsVi());
}

// ---------------------------------------------------------------------------
// Lifecycle -- both halves. power() is inherited: SioBoard::power() calls reset(PowerOn)
// virtually, which lands here and resets both sections.
// ---------------------------------------------------------------------------
void UioBoard::reset(Reset r) {
    SioBoard::reset(r);  // the cassette UART + its interrupt-enable flip-flops
    serial_.reset(r);    // the serial 6850 (a bus reset is a no-op for it; PowerOn is not)

    // The relay comes up closed at power-up (the manual). We re-arm it on RESET* too,
    // the same assumption SioBoard::reset makes about its interrupt-enable flip-flops:
    // the manual documents the power-up state and not the warm-reset clear line, and no
    // period program should be able to tell, since a driver sets the motor before use.
    motorOn_ = true;
}

void UioBoard::pump() {
    SioBoard::pump();  // the cassette UART's door to the outside world
    serial_.pump();    // ...and the serial section's
}

void UioBoard::configChanged() {
    SioBoard::configChanged();  // decode (covers a moved serial base too) + reprogram the
                                // cassette line + refresh
    serial_.refresh();          // a serial baud/strap/connect moved one of the section's
                                // deadlines
}

// ---------------------------------------------------------------------------
// Reflection: the cassette's properties (AcrBoard's, which are the SIO's minus CONNECT)
// plus the serial base, the SW-1 modulation, and the read-only motor state.
// ---------------------------------------------------------------------------
std::vector<Property> UioBoard::properties() {
    std::vector<Property> p = AcrBoard::properties();

    // GUARD THE INHERITED CASSETTE `port` SETTER against overlapping the serial pair.
    // SioBoard's setter only checks even-ness; on this card the cassette must also stay
    // clear of the 6850, or moving it onto 0x10 would shadow the serial section.
    for (Property& x : p) {
        if (x.name != "port") continue;
        auto inner = x.set;  // SioBoard's even-check + assign base_
        x.set      = [this, inner](const Value& v, std::string& err) {
            if (portPairsOverlap((uint8_t)v.i(), serial_.base())) {
                err = "the cassette would overlap the serial section at 0x" +
                      Value::ofInt(serial_.base()).text(16) +
                      " -- move one section clear of the other";
                return false;
            }
            return inner(v, err);
        };
        break;
    }

    // THE SERIAL SECTION'S BASE -- SW-2. The inherited `port` property is the CASSETTE's
    // base (0x06); this is the 6850's, which SW-2 moves between 0x10 and 0x18.
    {
        Property x;
        x.name  = "serial_port";
        x.help  = "Serial (6850) base -- SW-2. 0x10 = 2SIO Port A (default); SW-2 ON = 0x18";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xFE;  // the even/odd pair must fit under 0xFF
        x.get   = [this] { return Value::ofInt(serial_.base()); };
        x.set   = [this](const Value& v, std::string& err) {
            if (v.i() & 1) {
                err = "the 6850 decodes an even/odd PAIR -- the base must be even";
                return false;
            }
            // ...and it must not land on the cassette pair (0x06/0x07 by default).
            if (portPairsOverlap((uint8_t)v.i(), base_)) {
                err = "the serial section would overlap the cassette at 0x" +
                      Value::ofInt(base_).text(16) +
                      " -- move one section clear of the other";
                return false;
            }
            serial_.setBase((uint8_t)v.i());
            return true;
        };
        p.push_back(std::move(x));
    }

    // SW-1 -- which modulation the one modem lays down and reads back. modem() reads this.
    {
        Property x;
        x.name    = "standard";
        x.help    = "SW-1 tape modulation: mits (2400/1850) | kansas (Kansas City 2400/1200)";
        x.kind    = Kind::Enum;
        x.choices = {"mits", "kansas"};
        x.get     = [this] { return Value::ofStr(standard_); };
        x.set     = [this](const Value& v, std::string&) {
            standard_ = (v.s() == "kansas") ? "kansas" : "mits";
            return true;
        };
        p.push_back(std::move(x));
    }

    // THE MOTOR RELAY -- READ-ONLY (no setter). The GUEST drives it with an OUT to the
    // cassette control port; a SET that fought the program for it would be offering a
    // control the hardware does not have. SHOW reads it; the guest writes it.
    {
        Property x;
        x.name    = "motor";
        x.help    = "Tape-recorder motor relay (guest-driven: OUT 6,127 = on, OUT 6,191 = off)";
        x.kind    = Kind::Enum;
        x.choices = {"on", "off"};
        x.get     = [this] { return Value::ofStr(motorOn_ ? "on" : "off"); };
        p.push_back(std::move(x));
    }

    return p;
}

std::vector<MapEntry> UioBoard::ioMap() const {
    std::vector<MapEntry> m = AcrBoard::ioMap();  // cassette status/data (0x06/0x07)
    uint8_t b = serial_.base();
    for (const auto& ch : serial_.channels())
        m.push_back({(uint32_t)(b + ch.offset), (uint32_t)(b + ch.offset + 1), "read/write",
                     "6850 serial '" + ch.name + "' -- status/control, data"});
    return m;
}

// ---------------------------------------------------------------------------
// Units: the inherited `tape` (MOUNT) plus the serial section's `serial` (CONNECT).
// ---------------------------------------------------------------------------
std::vector<UnitDef> UioBoard::units() const {
    std::vector<UnitDef> u = AcrBoard::units();          // {tape}
    for (const auto& s : serial_.units()) u.push_back(s);  // {serial}
    return u;
}

std::vector<Property> UioBoard::unitProperties(const std::string& unit) {
    if (isSerial(unit)) return serial_.unitProperties(lowerAscii(unit));
    return AcrBoard::unitProperties(unit);  // the tape's `mode`/`format`/leader/...
}

bool UioBoard::connect(const std::string& unit, const std::string& endpoint, std::string& err) {
    if (isSerial(unit)) return serial_.connect(lowerAscii(unit), endpoint, err);
    if (lowerAscii(unit) == "tape") return AcrBoard::connect(unit, endpoint, err);  // refused, with reason
    err = "uio has a 'serial' unit to CONNECT and a 'tape' unit to MOUNT, not '" + unit + "'";
    return false;
}

bool UioBoard::disconnect(const std::string& unit, std::string& err) {
    if (isSerial(unit)) return serial_.disconnect(lowerAscii(unit), err);
    if (lowerAscii(unit) == "tape") return AcrBoard::disconnect(unit, err);  // refused, with reason
    err = "uio has a 'serial' unit to CONNECT and a 'tape' unit to MOUNT, not '" + unit + "'";
    return false;
}

ByteStream* UioBoard::unitStream(const std::string& unit) {
    if (isSerial(unit)) return serial_.unitStream(lowerAscii(unit));
    return AcrBoard::unitStream(unit);  // tape -> nullptr (SioBoard exposes only its line)
}

uint64_t UioBoard::rxBytes() const { return SioBoard::rxBytes() + serial_.rxBytes(); }

std::vector<std::string> UioBoard::drainLog() {
    std::vector<std::string> out = AcrBoard::drainLog();  // tape codec + cassette UART
    for (auto& s : serial_.drainLog()) out.push_back(std::move(s));
    return out;
}

// ---------------------------------------------------------------------------
// SNAPSHOT / RESTORE. AcrBoard writes [Board fields][cassette UART][int-enables][tape
// mode + head], then the serial section's chip(s), then this card's one runtime latch --
// the motor relay. standard_ and the two base ports are CONFIG (re-applied from the
// machine file), so they do not travel. Deserialize in the same order.
// ---------------------------------------------------------------------------
void UioBoard::serialize(StateWriter& w) const {
    AcrBoard::serialize(w);
    serial_.serialize(w);
    w.boolean(motorOn_);
}

void UioBoard::deserialize(StateReader& r) {
    AcrBoard::deserialize(r);
    serial_.deserialize(r);
    motorOn_ = r.boolean();
}

// ---------------------------------------------------------------------------
// THE MODEM SW-1 SELECTS. One format -- the switch picks one, and a tape in the other
// standard is refused, exactly as on the plain ACR. Both constants already ship
// (host/tapemodem.h); the UIO is the "future card that adds a TapeFormat and no code".
// ---------------------------------------------------------------------------
std::vector<TapeFormat> UioBoard::modem() const {
    return {standard_ == "kansas" ? tapeformats::kcs300() : tapeformats::fsk300_1850()};
}

} // namespace altair
