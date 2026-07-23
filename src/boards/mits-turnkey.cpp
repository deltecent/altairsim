#include "boards/mits-turnkey.h"

#include "core/roms.h"
#include "core/statefile.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <utility>

namespace altair {

TurnkeyBoard::TurnkeyBoard()
    // The section is handed the card's intChanged() so it can drive pin 73 -- it is not
    // a Board and cannot reach it. `this` is valid here: the base subobject is built.
    : sio_({{"tty", 0}}, [this] { intChanged(); }) {
    std::fill(std::begin(prom_), std::end(prom_), (uint8_t)0xFF);  // unprogrammed EPROM
}

// ---------------------------------------------------------------------------
// Decode. Four things live on this card, and the bus asks one question.
// ---------------------------------------------------------------------------
bool TurnkeyBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;

    if (c.type == Cycle::IoRead || c.type == Cycle::IoWrite) {
        if (sio_.decodesPort(c.port())) return true;
        // Sense switches: `IN 0FFH` and nothing else. INPUT ONLY, like the front panel
        // (mits-frontpanel.h) -- an `OUT 0FFH` is not ours. This card OWNS the port here,
        // which is why a Turnkey machine carries no `fp` (reference §4).
        if (c.type == Cycle::IoRead && c.port() == 0xFF) return true;
        return false;
    }

    if (c.type == Cycle::MemRead) {
        // The auto-start jam drives the first three fetches after reset. The CPU resets
        // its PC to 0, so those fetches are at addresses 0, 1, 2 in turn -- gating on the
        // address (not just a fetch count) is what the real sequence does and keeps a
        // stray read elsewhere from being mistaken for a jammed byte.
        if (autostartArmed_ && c.addr == (uint16_t)autostartStep_ && autostartStep_ < 3)
            return true;
        // The boot PROM answers reads in its window while armed.
        if (promArmed_ && inPromWindow(c.addr)) return true;
    }
    return false;
}

// PHANTOM* -- held, like an interrupt, until released. The honoring RAM board's
// `honors_phantom = read` strap is what lets writes fall through to the RAM under the
// shadow (reference §9; the Tarbell in tests/test_phantom.cpp is the same shape).
bool TurnkeyBoard::assertsPhantom(const BusCycle& c) const {
    // The jam holds MEMR low so no other memory board drives the three JMP-byte cycles.
    if (autostartArmed_ && c.type == Cycle::MemRead && c.addr == (uint16_t)autostartStep_ &&
        autostartStep_ < 3)
        return true;
    // The boot PROM shadows RAM in its window while armed.
    if (promArmed_ && inPromWindow(c.addr) &&
        (c.type == Cycle::MemRead || c.type == Cycle::MemWrite))
        return true;
    return false;
}

uint8_t TurnkeyBoard::read(const BusCycle& c) {
    if (c.type == Cycle::MemRead) {
        if (autostartArmed_ && c.addr == (uint16_t)autostartStep_ && autostartStep_ < 3) {
            // C3 00 <hi>: JMP to (START ADDR switches << 8). The low byte is always 0 --
            // SW8/SW9 are the high eight bits and the address is a multiple of 256.
            if (autostartStep_ == 0) return 0xC3;
            if (autostartStep_ == 1) return 0x00;
            return (uint8_t)(start_ >> 8);
        }
        if (promArmed_ && inPromWindow(c.addr)) return prom_[c.addr - promBase_];
        return 0xFF;
    }
    if (c.type == Cycle::IoRead) {
        if (c.port() == 0xFF) return sense_;  // SA8..SA15; snoop() latches the phantom off
        if (sio_.decodesPort(c.port())) return sio_.read(c.port());
    }
    return 0xFF;
}

void TurnkeyBoard::write(const BusCycle& c) {
    if (c.type == Cycle::IoWrite && sio_.decodesPort(c.port())) sio_.write(c.port(), c.data);
    // The PROM is ROM: it never answers a write. A write into its window falls through to
    // the RAM underneath (the honoring board), which is the whole point of the shadow.
}

// The clocked half. Two independent flip-flops live here (board.h): the auto-start
// sequencer, and the phantom-disable latch.
void TurnkeyBoard::snoop(const BusCycle& c) {
    if (autostartArmed_ && c.type == Cycle::MemRead && c.addr == (uint16_t)autostartStep_ &&
        autostartStep_ < 3) {
        if (++autostartStep_ >= 3) {
            autostartArmed_ = false;
            decodeChanged();  // the jam is over; addr 0,1,2 revert to RAM
        }
    }
    // The boot PROM disables itself on the first INPUT from port FE or FF (reference §9,
    // post-SB007). The sense-switch read at FF is exactly this event -- the card answers
    // that read AND snoops it here. It SNOOPS FE/FF rather than answering FE, so it does
    // not contend for a port it does not own.
    if (promArmed_ && c.type == Cycle::IoRead && (c.port() == 0xFE || c.port() == 0xFF)) {
        promArmed_ = false;
        decodeChanged();
    }
}

// ---------------------------------------------------------------------------
// Lifecycle. Any system reset re-arms both latches -- POC* and RESET* alike, because
// reference §9 is explicit that a front-panel reset re-enables the PROM.
// ---------------------------------------------------------------------------
void TurnkeyBoard::reset(Reset r) {
    bool changed = !promArmed_ || !autostartArmed_ || autostartStep_ != 0;
    promArmed_      = true;
    autostartArmed_ = true;
    autostartStep_  = 0;
    sio_.reset(r);
    if (changed) decodeChanged();
}

void TurnkeyBoard::power() {
    loadProm();             // re-read the socket ROMs from the host, like a memory card
    reset(Reset::PowerOn);  // re-arm the latches and power-on the 6850
}

void TurnkeyBoard::configChanged() {
    decodeChanged();  // `prom`, `sio_base` or `sense` may have moved the decode
    sio_.refresh();   // ...and a SIO strap (baud/interrupt/connect) moved a deadline
}

// ---------------------------------------------------------------------------
// The PROM sockets. `builtin:` and a host path travel the SAME Intel HEX parser as a
// memory card's ROM region (src/boards/s100-memory.cpp) -- DESIGN.md 10.3.1.
// ---------------------------------------------------------------------------
void TurnkeyBoard::loadProm() {
    std::fill(std::begin(prom_), std::end(prom_), (uint8_t)0xFF);

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
            } else {
                loadBin(raw, sock.at, img);
            }
        }

        if (img.empty()) {
            log_.push_back(id + ": " + sock.mount + ": no bytes");
            continue;
        }
        // A HEX file places itself; if it disagrees with `at`, say so rather than silently
        // relocating it -- a ROM at the wrong address is an hour of chasing.
        if (img.lo() != sock.at) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "%s: %s places bytes at %04X but socket says at = %04X",
                          id.c_str(), sock.mount.c_str(), img.lo(), sock.at);
            log_.push_back(buf);
        }
        for (const auto& [a, b] : img.bytes)
            if (a >= promBase_ && (uint32_t)a < (uint32_t)promBase_ + kPromSize)
                prom_[a - promBase_] = b;
    }
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> TurnkeyBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "prom";
        x.help  = "PROM ADDR switches: base of the 1K boot-PROM window (FC00-FFFF normal)";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0x10000 - kPromSize;  // the 1K window must fit under FFFF
        x.get   = [this] { return Value::ofInt(promBase_); };
        x.set   = [this](const Value& v, std::string&) {
            promBase_ = (uint16_t)v.i();
            loadProm();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "start";
        x.help  = "START ADDR switches: Auto-Start jams JMP here at reset (a multiple of 256)";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xFF00;
        x.get   = [this] { return Value::ofInt(start_); };
        x.set   = [this](const Value& v, std::string& err) {
            if (v.i() & 0xFF) {
                err = "start must be a multiple of 256 (the low byte is always 0)";
                return false;
            }
            start_ = (uint16_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "sense";
        x.help  = "Sense switches (SW6/SW7), read at port FF";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xFF;
        x.get   = [this] { return Value::ofInt(sense_); };
        x.set   = [this](const Value& v, std::string&) {
            sense_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "sio_base";
        x.help  = "Base address of the integrated 6850 SIO (0x10 = 2SIO Port A)";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xFE;  // two ports must fit under 0xFF
        x.get   = [this] { return Value::ofInt(sio_.base()); };
        x.set   = [this](const Value& v, std::string&) {
            sio_.setBase((uint8_t)v.i());
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<MapEntry> TurnkeyBoard::memMap() const {
    return {{(uint32_t)promBase_, (uint32_t)promBase_ + kPromSize - 1, "read",
             "boot PROM (phantom-latched; hides on the first IN FE/FF)"}};
}

std::vector<MapEntry> TurnkeyBoard::ioMap() const {
    std::vector<MapEntry> m;
    uint8_t b = sio_.base();
    for (const auto& ch : sio_.channels())
        m.push_back({(uint32_t)(b + ch.offset), (uint32_t)(b + ch.offset + 1), "read/write",
                     "6850 SIO '" + ch.name + "' -- status/control, data"});
    m.push_back({0xFF, 0xFF, "read", "sense switches (SA8-SA15); also disables the boot PROM"});
    return m;
}

std::vector<std::string> TurnkeyBoard::drainLog() {
    std::vector<std::string> out;
    for (auto& s : sio_.drainLog()) out.push_back(id + ":" + s);
    for (auto& s : log_) out.push_back(std::move(s));
    log_.clear();
    return out;
}

// ---------------------------------------------------------------------------
// The [[board.socket]] sub-unit table -- which built-in/file is in each PROM socket.
// ---------------------------------------------------------------------------
std::vector<Property> TurnkeyBoard::subUnitProperties(const std::string& table) const {
    if (table != "socket") return {};
    std::vector<Property> p;
    {
        Property x;
        x.name  = "at";
        x.help  = "Where the socket sits in the window (FC00/FD00/FE00/FF00)";
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

bool TurnkeyBoard::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
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

std::vector<Board::SubUnit> TurnkeyBoard::subUnits() const {
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

// ---------------------------------------------------------------------------
// SNAPSHOT / RESTORE. The runtime latches (phantom armed, auto-start step) and the
// sense switches travel; the PROM bytes are host-backed config, re-read on power
// (DESIGN.md 13). The 6850's state travels via the section.
// ---------------------------------------------------------------------------
void TurnkeyBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.boolean(promArmed_);
    w.boolean(autostartArmed_);
    w.u8((uint8_t)autostartStep_);
    w.u8(sense_);
    sio_.serialize(w);
}

void TurnkeyBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    promArmed_      = r.boolean();
    autostartArmed_ = r.boolean();
    autostartStep_  = r.u8();
    sense_          = r.u8();
    sio_.deserialize(r);
    decodeChanged();
}

} // namespace altair
