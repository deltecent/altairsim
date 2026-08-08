#include "boards/sd-sbc.h"

#include "core/bus.h"
#include "core/clock.h"
#include "core/roms.h"
#include "core/statefile.h"
#include "core/value.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

namespace altair {

namespace {

EndpointResolver g_resolver;

// A card in a backplane always has a clock, but Bus::attach() is public, so a board
// CAN be wired up without a machine around it. A USART with no clock is a chip with no
// crystal: it reads as a dead card rather than dereferencing a null pointer. (Same
// idiom as SioBoard::deadCard.)
Clock& deadCard() {
    static Clock stopped;
    return stopped;
}

} // namespace

void SbcBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

SbcBoard::SbcBoard() {
    u_.disconnect();  // -> NullStream: a card with nothing plugged in has a DEAD line,
                      // never a dangling one.
    // THE DEFINING STRAP: RxD is soldered to /DSR, so the monitor's auto-baud can watch
    // the serial line in status bit 7. This is the etch default; the `rxd2dsr` board
    // property flips it. See reference/SD Systems SBC-100 & SBC-200.md.
    u_.dsrSrc = DsrSource::FollowRxD;
}

SbcBoard::~SbcBoard() {
    // A card can be pulled from a RUNNING machine; a deadline that fires into a
    // destroyed board is a use-after-free with a two-week fuse on it.
    if (clock_) clock_->cancel(wake_);
}

// ---------------------------------------------------------------------------
// The bus. One 8-port I/O block, plus the onboard PROM's memory reads and the IntAck
// cycle when the keyboard interrupt is pending.
//
//   78-7B  Z80-CTC        7C  8251 data      7E  parallel data
//                         7D  8251 status/   7F  parallel handshake / mem switch-out
//                             command
//
// The 8251 orientation is data LOW, status HIGH -- the REVERSE of the 6850 section
// (Sio2Port), which is exactly why this card does not reuse it.
// ---------------------------------------------------------------------------
bool SbcBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;

    // The keyboard interrupt's vector: claim the IntAck cycle only when we are the one
    // asking (like the 88-VI), so an unclaimed acknowledge still floats 0xFF.
    if (c.type == Cycle::IntAck) return ctc_.ch1IntArmed_ && ch1Triggered();

    if (c.type == Cycle::IoRead || c.type == Cycle::IoWrite) {
        uint8_t p = c.port();
        return p >= blockBase() && (uint8_t)(p - blockBase()) < 8;  // 78-7F, no wrap
    }

    // The onboard PROM answers memory reads in its window while switched in. Writes
    // fall through to the RAM under the shadow (assertsPhantom keeps the RAM off the
    // read but not the write), so we do NOT decode MemWrite.
    if (c.type == Cycle::MemRead) return promArmed_ && inPromWindow(c.addr);
    return false;
}

uint8_t SbcBoard::read(const BusCycle& c) {
    if (c.type == Cycle::IntAck) return ctc_.ch1Vector();  // 0x82 -> ISR pointer at FF82

    if (c.type == Cycle::MemRead) {
        // Only reached while promArmed_ && inPromWindow (see decodes()).
        return prom_[c.addr - kOnboardBase];
    }

    const Clock& clk = clock_ ? *clock_ : deadCard();
    uint8_t p = c.port();
    uint8_t v;
    if (p == base_)                 v = u_.readData(clk);    // 7C: RX data (clears RxRDY)
    else if (p == (uint8_t)(base_ + 1)) v = u_.readStatus(clk);  // 7D: status
    else                            v = 0xFF;  // CTC (78-7B) and parallel (7E/7F) read back FF
    refresh();  // reading data cleared RxRDY; either 8251 read advanced the receiver
    return v;
}

void SbcBoard::write(const BusCycle& c) {
    if (c.type != Cycle::IoWrite) return;  // a MemWrite in the PROM window falls through

    const Clock& clk = clock_ ? *clock_ : deadCard();
    uint8_t p   = c.port();
    uint8_t off = (uint8_t)(p - blockBase());

    if (p == base_) {
        u_.writeData(c.data, clk);          // 7C: the DATA port -> transmit
    } else if (p == (uint8_t)(base_ + 1)) {
        u_.writeControl(c.data, clk);       // 7D: the CONTROL port -> mode, then commands
    } else if (off < 4) {
        ctc_.writePort(off, c.data);        // 78-7B: the Z80-CTC channels
    } else if (p == (uint8_t)(blockBase() + 7)) {
        // 7F bit 1: the SBC-200 memory switch-out. A=2 drops the onboard PROM out of
        // the map (RAM shows through); A=0 puts it back. Bit 0 is the parallel strobe.
        bool armed = (c.data & 0x02) == 0;
        if (armed != promArmed_) {
            promArmed_ = armed;
            decodeChanged();  // the E000-FFFF window just changed hands
        }
    }
    // 7E (parallel data latch) has no observable effect with nothing wired to J3.
    refresh();
}

// ---------------------------------------------------------------------------
// THE CTC, THE INTERRUPT WIRE AND THE PROM SHADOW.
// ---------------------------------------------------------------------------

// A write to one of the four CTC channels. We watch for exactly two things: the vector
// register (channel 0, a word with D0=0) and channel 1's interrupt-enable (a control
// word, D0=1, with D7). Everything else -- time constants, the baud divider -- is
// absorbed. A control word with D2 says "the next write to this channel is a time
// constant", which is the only reason we must not mistake that follow-up byte (which
// can have any bit pattern, including D0=0) for another vector or control word.
void SbcBoard::Ctc::writePort(uint8_t chan, uint8_t v) {
    if (expectTc_[chan]) {          // this byte is a time constant -- absorb it
        expectTc_[chan] = false;
        return;
    }
    if ((v & 0x01) == 0) {          // D0=0: an interrupt-vector write (channel 0 only)
        if (chan == 0) vectorBase_ = v & 0xF8;
        return;
    }
    // D0=1: a channel control word.
    if (v & 0x04) expectTc_[chan] = true;          // D2: "time constant follows"
    if (chan == 1) ch1IntArmed_ = (v & 0x80) != 0; // D7: interrupt enable
}

// PIN 73 (the Z80 /INT), combinational and pure (DESIGN.md 4.4.1). The CTC has armed
// channel 1, and the 8251 has a byte waiting: the same shape as the 88-SIO's input
// interrupt. Cleared when the ISR reads the data port (RxRDY drops), so no daisy-chain
// end-of-interrupt is needed -- RETI is a no-op for a single-channel level like this.
bool SbcBoard::assertsInt() const {
    return clock_ && ctc_.ch1IntArmed_ && ch1Triggered();
}

// The CTC ch1 trigger, from either the on-card 8251 (serial console) or, in a video
// machine, the VDB-8024's keyboard strobe carried on S-100 VI2 (reference 6). Reading
// the VI wire off the bus is why the card watchesVi(): when the VDB pulls or drops VI2
// the bus re-derives our /INT for us, exactly as it does for the 88-VI.
bool SbcBoard::ch1Triggered() const {
    if (u_.rxReady()) return true;
    return bus_ && (bus_->viLines() & viBit(IrqJumper::Vi2)) != 0;
}

// PHANTOM*, held while the onboard memory is switched in. On a READ in a socket window
// the PROM drives and the RAM under it stands down (honors_phantom = read); on a WRITE
// we still assert it, but the RAM honors phantom only on reads, so the write lands in
// RAM -- which is the whole point of the shadow. Same shape as the Turnkey boot PROM.
bool SbcBoard::assertsPhantom(const BusCycle& c) const {
    if (!promArmed_) return false;
    if (c.type != Cycle::MemRead && c.type != Cycle::MemWrite) return false;
    return inPromWindow(c.addr);
}

// ---------------------------------------------------------------------------
// THE CARD'S OWN CLOCK (DESIGN.md 7.5). The receiver is advanced here, the interrupt
// wire is re-driven (the bus is not going to come and ask), and one alarm is armed for
// the next moment the chip changes with nobody touching it -- a receive frame
// completing (RxRDY rises, which may raise /INT), or the transmitter draining.
// ---------------------------------------------------------------------------
void SbcBoard::refresh() {
    if (!clock_) return;

    u_.poll(*clock_);
    intChanged();  // RxRDY may have moved -- drive /INT and the IntAck decode

    clock_->cancel(wake_);
    wake_ = Clock::kNone;
    if (uint64_t next = nextEdge()) wake_ = clock_->at(next, [this] { refresh(); });
}

uint64_t SbcBoard::nextEdge() const {
    const Clock& clk = clock_ ? *clock_ : deadCard();

    uint64_t best = 0;
    auto consider = [&](uint64_t when) {
        if (when <= clk.now()) return;  // already past: it is already showing in status
        if (!best || when < best) best = when;
    };

    if (u_.rxFrameActive()) {
        consider(u_.rxDoneAt());      // the frame will finish and raise RxRDY on its own
    } else if (u_.rxWaiting()) {
        consider(u_.rxNextAt());      // a queued byte will start its frame at this T-state
    }
    consider(u_.txFreeAt());          // the transmitter drains on its own clock

    return best;
}

// ---------------------------------------------------------------------------
// RESET. The 8251's RESET pin is not driven from the backplane here: the monitor
// always software-programs the chip (mode then command) out of reset, so nothing
// period-correct can tell. A bus reset just re-polls and re-arms; power-on puts the
// chip in its known-good state. (Same stance, and same reasoning, as SioBoard.)
// ---------------------------------------------------------------------------
void SbcBoard::reset(Reset r) {
    // Any reset switches the onboard memory back IN (the board comes up with the PROM
    // mapped, reference §7). This is independent of the clock -- do it first.
    if (!promArmed_) {
        promArmed_ = true;
        decodeChanged();
    }
    if (!clock_) return;
    if (r == Reset::PowerOn) u_.powerOn(*clock_);
    refresh();  // do NOT clear wake_ first: a bus reset does not empty the queue, and a
                // character still leaving has an alarm on the books.
}

void SbcBoard::power() {
    loadProm();             // re-read the socket ROMs from the host, like a memory card
    reset(Reset::PowerOn);
}

// THE ONE DOOR THE OUTSIDE WORLD COMES THROUGH (DESIGN.md 7.1).
void SbcBoard::pump() {
    u_.pump();
    refresh();
}

// A strap moved: `port` (moves the card in I/O space) or a unit property (`baud`
// changes how long a character takes, so every deadline is now aimed at the wrong
// T-state; `connect` is a new line, possibly with something already on it).
void SbcBoard::configChanged() {
    decodeChanged();
    u_.programLine();
    refresh();
}

// ---------------------------------------------------------------------------
// Snapshot: the chip's live state travels, and so do the CTC's arm/vector latches and
// the memory switch. The straps (variant/base/sockets) and the PROM bytes are config,
// re-read on power (DESIGN.md 13).
// ---------------------------------------------------------------------------
void SbcBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    u_.serialize(w);
    w.u8(ctc_.vectorBase_);
    w.boolean(ctc_.ch1IntArmed_);
    for (bool b : ctc_.expectTc_) w.boolean(b);
    w.boolean(promArmed_);
}

void SbcBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    u_.deserialize(r);
    ctc_.vectorBase_  = r.u8();
    ctc_.ch1IntArmed_ = r.boolean();
    for (bool& b : ctc_.expectTc_) b = r.boolean();
    promArmed_ = r.boolean();
    decodeChanged();  // the PROM window may have changed hands
    refresh();        // re-drive /INT and re-arm the deadline from the restored state
}

// ---------------------------------------------------------------------------
// Reflection
// ---------------------------------------------------------------------------
uint8_t SbcBoard::statusByte() const {
    return u_.statusByte(clock_ ? *clock_ : deadCard());
}

std::vector<Property> SbcBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name    = "variant";
        x.help    = "Which board: sbc100 (2.4576 MHz) or sbc200 (4 MHz). The console, CTC "
                    "and PROM behave alike here; the CPU crystal is set on the z80 card";
        x.kind    = Kind::Enum;
        x.choices = {"sbc100", "sbc200"};
        x.get     = [this] {
            return Value::ofStr(variant_ == Variant::Sbc100 ? "sbc100" : "sbc200");
        };
        x.set = [this](const Value& v, std::string&) {
            variant_ = (v.s() == "sbc100") ? Variant::Sbc100 : Variant::Sbc200;
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        // THE RxD->/DSR JUMPER, AND IT IS A STRAP ON THIS CARD. The SBC-200 solders the
        // 8251's receive-data line to its /DSR input so MSMONR21 can auto-detect baud by
        // timing the start bit in status bit 7. On by default (that is the etch); turn it
        // off for a plain 8251 console, or to run a monitor that does not auto-baud.
        Property x;
        x.name = "rxd2dsr";
        x.help = "RxD strapped to /DSR (the SBC auto-baud jumper). Off = a plain 8251 /DSR";
        x.kind = Kind::Bool;
        x.get  = [this] { return Value::ofBool(u_.dsrSrc == DsrSource::FollowRxD); };
        x.set  = [this](const Value& v, std::string&) {
            u_.dsrSrc = v.b() ? DsrSource::FollowRxD : DsrSource::Inactive;
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "port";
        x.help  = "Base I/O address (a card jumper). Data at BASE, status/command at "
                  "BASE+1. The etch default is 7C";
        x.kind  = Kind::Int;
        x.radix = 16;  // on the wire -> hex (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xFE;
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string&) {
            base_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

// One serial unit. The card has one USART and one connector; every other jumper is a
// board property. The unit exists because CONNECT names one.
std::vector<UnitDef> SbcBoard::units() const {
    return {{"tty", UnitKind::Serial, u_.endpoint()}};
}

std::vector<Property> SbcBoard::unitProperties(const std::string& unit) {
    if (unit != "tty") return {};
    // The 8251's own `connect` property setter opens the endpoint, so it -- like the
    // card's connect() -- must rebase a relative in:/out: PATH from a machine file.
    return u_.properties(
        rebasingResolver(g_resolver, [this](const std::string& p) { return resolvePath(p); }));
}

std::vector<MapEntry> SbcBoard::ioMap() const {
    uint8_t b = blockBase();
    return {
        {(uint32_t)b, (uint32_t)b + 3, "write",
         "Z80-CTC channels 0-3 (ch0 = baud gen / interrupt vector, ch1 = keyboard interrupt)"},
        {(uint32_t)base_, (uint32_t)base_, "read/write", "8251 -- receive/transmit data"},
        {(uint32_t)base_ + 1, (uint32_t)base_ + 1, "read/write",
         "8251 -- status / mode+command"},
        {(uint32_t)b + 6, (uint32_t)b + 6, "read/write", "parallel port -- data latch"},
        {(uint32_t)b + 7, (uint32_t)b + 7, "read/write",
         "parallel handshake; bit 1 switches the onboard PROM out of the map"},
    };
}

std::vector<MapEntry> SbcBoard::memMap() const {
    std::vector<MapEntry> m;
    for (const auto& sock : sockets_) {
        if (sock.mount.empty()) continue;
        m.push_back({(uint32_t)sock.at, (uint32_t)sock.at, "read",
                     "onboard PROM socket (" + sock.mount +
                     "); shadows RAM until OUT 7F bit 1 switches it out"});
    }
    return m;
}

bool SbcBoard::connect(const std::string& unit, const std::string& ep, std::string& err) {
    if (unit != "tty") {
        err = "sbc has no unit '" + unit + "' -- it has one, and it is called 'tty'";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    // A machine-file in:/out: PATH is relative to the machine file; rebase the copy the
    // resolver opens (rebaseEndpointPaths knows the grammar). The 8251 echoes describe()
    // for its `connect` property, so SHOW/CONFIG SAVE report the resolved path, which
    // reloads idempotently. Nothing else in the grammar carries a path.
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
    u_.connect(std::move(s));
    refresh();  // a new line, and it may already have something waiting on it
    return true;
}

bool SbcBoard::disconnect(const std::string& unit, std::string& err) {
    if (unit != "tty") {
        err = "sbc has no unit '" + unit + "' -- it has one, and it is called 'tty'";
        return false;
    }
    u_.disconnect();
    refresh();
    return true;
}

std::vector<std::string> SbcBoard::drainLog() {
    std::vector<std::string> out = u_.drainLog();
    for (auto& s : log_) out.push_back(std::move(s));
    log_.clear();
    return out;
}

// ---------------------------------------------------------------------------
// The onboard PROM sockets. `builtin:` and a host path travel the SAME Intel HEX parser
// as a memory card's ROM region (DESIGN.md 10.3.1) -- the same loader the Turnkey uses.
// ---------------------------------------------------------------------------
void SbcBoard::loadProm() {
    std::fill(std::begin(prom_), std::end(prom_), (uint8_t)0xFF);
    std::fill(std::begin(promPresent_), std::end(promPresent_), false);

    for (const auto& sock : sockets_) {
        if (sock.mount.empty()) continue;
        Image       img;
        std::string err;

        if (sock.mount.rfind("builtin:", 0) == 0) {
            std::string name = sock.mount.substr(8);
            const BuiltinRom* rom = findRom(name);
            if (!rom) {
                log_.push_back(id + ": no built-in ROM named '" + name + "'. SHOW ROMS lists them.");
                continue;
            }
            if (!decodeRom(*rom, sock.at, img, err)) {
                log_.push_back(id + ": " + err);
                continue;
            }
        } else {
            const std::string path = resolvePath(sock.mount);
            std::ifstream     f(path, std::ios::binary);
            if (!f) {
                log_.push_back(id + ": cannot open '" + path + "'" + pathNote(sock.mount));
                continue;
            }
            std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
            if (looksLikeHex(raw)) {
                if (!loadHex(raw, img, err)) {
                    log_.push_back(id + ": " + sock.mount + ": " + err);
                    continue;
                }
            } else if (looksLikeSrec(raw)) {
                if (!loadSrec(raw, img, err)) {
                    log_.push_back(id + ": " + sock.mount + ": " + err);
                    continue;
                }
            } else {
                loadBin(raw, sock.at, img);
            }
        }

        if (img.empty()) {
            log_.push_back(id + ": " + sock.mount + ": no bytes");
            continue;
        }
        // A HEX file places itself; if it disagrees with `at`, say so rather than silently
        // relocating it (the same courtesy the Turnkey and memory cards extend).
        if (img.lo() != sock.at) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "%s: %s places bytes at %04X but socket says at = %04X",
                          id.c_str(), sock.mount.c_str(), img.lo(), sock.at);
            log_.push_back(buf);
        }
        for (const auto& [a, b] : img.bytes) {
            if (a >= kOnboardBase && a < (uint32_t)kOnboardBase + kOnboardSize) {
                prom_[a - kOnboardBase]        = b;
                promPresent_[a - kOnboardBase] = true;
            } else {
                char buf[160];
                std::snprintf(buf, sizeof buf,
                              "%s: %s byte at %04X is outside the onboard window E000-FFFF",
                              id.c_str(), sock.mount.c_str(), (unsigned)a);
                log_.push_back(buf);
            }
        }
    }
}

std::vector<Property> SbcBoard::subUnitProperties(const std::string& table) const {
    if (table != "socket") return {};
    std::vector<Property> p;
    {
        Property x;
        x.name  = "at";
        x.help  = "Where the socket sits in the onboard window (E000 = monitor, F000 = disk BIOS)";
        x.kind  = Kind::Int;
        x.radix = 16;
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "mount";
        x.help = "What is in the socket: builtin:<name> or a HEX/BIN path. Relative to THIS FILE.";
        x.kind = Kind::Str;
        p.push_back(std::move(x));
    }
    return p;
}

bool SbcBoard::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
    if (table != "socket") {
        err = type() + " has no [[board." + table + "]] table";
        return false;
    }

    Socket sock{0, {}};
    bool    haveAt = false;
    for (const auto& [k, v] : kv) {
        if (k == "at") {
            long long n = 0;
            if (!parseNumber(v, n, err, 16)) return false;
            sock.at = (uint16_t)n;
            haveAt  = true;
        } else if (k == "mount") {
            sock.mount = v;
        }
    }
    if (!haveAt) {
        err = "[[board.socket]] needs an `at`";
        return false;
    }
    sockets_.push_back(std::move(sock));
    loadProm();
    return true;
}

std::vector<Board::SubUnit> SbcBoard::subUnits() const {
    std::vector<SubUnit> out;
    for (const auto& sock : sockets_) {
        if (sock.mount.empty()) continue;
        char at[8];
        std::snprintf(at, sizeof at, "%04X", sock.at);
        SubUnit su;
        su.table = "socket";
        su.fields.push_back({"at", at, false});
        su.fields.push_back({"mount", sock.mount, true});
        out.push_back(std::move(su));
    }
    return out;
}

} // namespace altair
