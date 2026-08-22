#include "boards/ssm-pb1.h"

#include "core/bus.h"
#include "core/hex.h"
#include "core/roms.h"
#include "core/statefile.h"
#include "core/value.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace altair {

namespace {

// Read a socket/PROM image the way the memory card's ROM region does: a `builtin:<name>`
// travels the registry, a file is slurped and auto-detected (Intel HEX / Motorola S-record /
// flat binary). `relocAt` is where a flat binary lands (a HEX/SREC file places itself).
bool readImage(const std::string& mount, const std::string& file, uint16_t relocAt,
               Image& img, std::string& err) {
    if (mount.rfind("builtin:", 0) == 0) {
        const BuiltinRom* rom = findRom(mount.substr(8));
        if (!rom) {
            err = "no built-in ROM named '" + mount.substr(8) + "'. SHOW ROMS lists them.";
            return false;
        }
        if (!decodeRom(*rom, relocAt, img, err)) return false;
    } else {
        std::ifstream f(file, std::ios::binary);
        if (!f) { err = "cannot open '" + file + "'"; return false; }
        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
        if (looksLikeHex(raw)) {
            if (!loadHex(raw, img, err)) { err = mount + ": " + err; return false; }
        } else if (looksLikeSrec(raw)) {
            if (!loadSrec(raw, img, err)) { err = mount + ": " + err; return false; }
        } else {
            loadBin(raw, relocAt, img);
        }
    }
    if (img.empty()) { err = mount + ": no bytes"; return false; }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// The bus. One output port (arm/type), the 4K programming window, and the on-board PROM area.
// The decode is PURE and does NOT depend on the arm flip-flop or the type latch, so the bus can
// cache it: whether a window write BURNS is decided in write(), not in decodes().
// ---------------------------------------------------------------------------
bool Pb1Board::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type == Cycle::IoWrite) return c.port() == (uint8_t)port_;
    if (c.type == Cycle::MemRead) return inWindow(c.addr) || promAt(c.addr) >= 0;
    if (c.type == Cycle::MemWrite) return inWindow(c.addr);  // claim it; an idle/RO write is dropped
    return false;  // IoRead: the control port is write-only -- an IN there is not ours
}

uint8_t Pb1Board::read(const BusCycle& c) {
    if (c.type == Cycle::MemRead) {
        if (inWindow(c.addr)) {
            const Socket* s = active();
            uint32_t off = (uint32_t)(c.addr - window_);
            uint8_t v = (off < s->buf.size()) ? s->buf[off] : 0xFF;  // beyond the chip -> floats
            armed_ = false;  // a read of the socket window RESETS the flip-flop (LED off)
            return v;
        }
        int i = promAt(c.addr);
        if (i >= 0) return proms_[(size_t)i].bytes[(size_t)(c.addr - proms_[(size_t)i].at)];
        return 0xFF;
    }
    return 0xFF;  // we do not decode an IoRead; this path is not reached
}

void Pb1Board::write(const BusCycle& c) {
    if (c.type == Cycle::MemWrite) {
        if (!inWindow(c.addr) || !armed_) return;  // without the arm a stray write cannot burn
        Socket* s = active();
        uint32_t off = (uint32_t)(c.addr - window_);
        if (type_ != Chip::None && off < s->buf.size())
            s->buf[off] &= c.data;  // programming an EPROM cell only clears a 1 to a 0
        return;
    }
    if (c.type != Cycle::IoWrite || c.port() != (uint8_t)port_) return;
    // Arm the programming flip-flop and latch the chip from D0/D1 (reference "Programming").
    armed_ = true;
    if (c.data & 0x01) type_ = Chip::C2708;
    else if (c.data & 0x02) type_ = Chip::C2716;
    else type_ = Chip::None;  // armed, but no pulse source selected -> a window write burns nothing
}

// LOOK WITHOUT TOUCHING: DISASM/TRACE over the sockets and the PROM area, with NO arm reset.
bool Pb1Board::peek(uint16_t addr, uint8_t& out) const {
    if (inWindow(addr)) {
        const Socket* s = active();
        uint32_t off = (uint32_t)(addr - window_);
        out = (off < s->buf.size()) ? s->buf[off] : 0xFF;
        return true;
    }
    int i = promAt(addr);
    if (i >= 0) { out = proms_[(size_t)i].bytes[(size_t)(addr - proms_[(size_t)i].at)]; return true; }
    return false;
}

int Pb1Board::promAt(uint16_t a) const {
    for (size_t i = 0; i < proms_.size(); ++i)
        if (proms_[i].size && a >= proms_[i].at && a < proms_[i].at + proms_[i].size)
            return (int)i;
    return -1;
}

Pb1Board::Socket*       Pb1Board::active()       { return type_ == Chip::C2716 ? &sock2716_ : &sock2708_; }
const Pb1Board::Socket* Pb1Board::active() const { return type_ == Chip::C2716 ? &sock2716_ : &sock2708_; }

// ---------------------------------------------------------------------------
// Lifecycle. A CPU reset drops the flip-flop (the S-100 power-on-clear pin does the same on
// real hardware). Power re-reads every socket/PROM image from the host -- so a burn that was
// not SAVEd or backed by a mount is lost on power-cycle, exactly like re-reading a ROM image.
// ---------------------------------------------------------------------------
void Pb1Board::reset(Reset) { armed_ = false; }

void Pb1Board::power() {
    loadSocket(sock2708_);
    loadSocket(sock2716_);
    for (auto& p : proms_) loadProm(p);
    armed_ = false;
    type_  = Chip::C2708;
}

void Pb1Board::loadSocket(Socket& s) {
    uint32_t sz = (&s == &sock2716_) ? k2716 : k2708;
    s.buf.assign(sz, 0xFF);  // an empty socket is an erased chip
    if (s.mount.empty()) return;
    Image       img;
    std::string err;
    if (!readImage(s.mount, resolvePath(s.mount), 0, img, err)) { say(err + pathNote(s.mount)); return; }
    uint16_t lo = img.lo();
    for (const auto& [a, b] : img.bytes) {
        uint32_t off = (uint32_t)(a - lo);
        if (off < sz) s.buf[off] = b;
    }
}

void Pb1Board::loadProm(Prom& p) {
    p.bytes.clear();
    p.size = 0;
    if (p.mount.empty()) return;
    const std::string file = p.mountFile.empty() ? resolvePath(p.mount) : p.mountFile;
    Image       img;
    std::string err;
    if (!readImage(p.mount, file, p.at, img, err)) { say(err + pathNote(p.mount)); return; }
    uint16_t lo   = img.lo();
    uint32_t span = (uint32_t)(img.hi() - lo) + 1;
    p.size = span;
    p.bytes.assign(span, 0xFF);
    for (const auto& [a, b] : img.bytes) {
        uint32_t off = (uint32_t)(a - lo);
        if (off < span) p.bytes[off] = b;
    }
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> Pb1Board::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Control port: an OUT here arms the board and picks 2708 (D0) or 2716 (D1). Only "
                  "A4-A7 decode, so it must be an x0H address (00, 10, .. F0)";
        x.kind  = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xF0;
        x.get   = [this] { return Value::ofInt(port_); };
        x.set   = [this](const Value& v, std::string& err) {
            int n = (int)v.i();
            if (n & 0x0F) {
                err = "the control port decodes only A4-A7, so it must be an x0H address (00, 10, .. F0)";
                return false;
            }
            port_ = (uint16_t)n;
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "window";
        x.help  = "The 4K programming-socket window, on a 4K boundary (0000, 1000, .. F000). The "
                  "guest writes bytes here to burn, and reads here to disarm";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xF000;
        x.get   = [this] { return Value::ofInt(window_); };
        x.set   = [this](const Value& v, std::string& err) {
            int n = (int)v.i();
            if (n & 0x0FFF) {
                err = "the programming window is a 4K block, so it must be on a 4K boundary "
                      "(0000, 1000, .. F000)";
                return false;
            }
            window_ = (uint16_t)n;
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<MapEntry> Pb1Board::ioMap() const {
    uint8_t b = (uint8_t)port_;
    return {{b, b, "write", "control: arm + select 2708 (D0=1) or 2716 (D1=1)"}};
}

std::vector<MapEntry> Pb1Board::memMap() const {
    std::vector<MapEntry> m;
    m.push_back({window_, (uint32_t)(window_ + kWindow - 1), "read/write",
                 "programming sockets U22 (2708) / U23 (2716)"});
    for (const auto& p : proms_)
        if (p.size)
            m.push_back({p.at, (uint32_t)(p.at + p.size - 1), "read", "on-board EPROM (U11-U14)"});
    return m;
}

// ---------------------------------------------------------------------------
// Units, MOUNT, UNMOUNT, and the two sub-unit tables:
//   [[board.socket]]  chip = 2708|2716, mount = <source/erased chip image>   (writable)
//   [[board.prom]]    at   = <>=8000>,  mount = <firmware image>             (read-only)
// ---------------------------------------------------------------------------
std::vector<UnitDef> Pb1Board::units() const {
    std::vector<UnitDef> u;
    {
        UnitDef x;
        x.name  = "u22";
        x.kind  = UnitKind::Rom;
        x.state = sock2708_.mount.empty() ? "(empty)" : sock2708_.mount;
        u.push_back(std::move(x));
    }
    {
        UnitDef x;
        x.name  = "u23";
        x.kind  = UnitKind::Rom;
        x.state = sock2716_.mount.empty() ? "(empty)" : sock2716_.mount;
        u.push_back(std::move(x));
    }
    for (size_t i = 0; i < proms_.size(); ++i) {
        UnitDef x;
        x.name  = "prom" + std::to_string(i);
        x.kind  = UnitKind::Rom;
        x.state = proms_[i].mount.empty() ? "(empty)" : proms_[i].mount;
        u.push_back(std::move(x));
    }
    return u;
}

bool Pb1Board::mount(const std::string& unit, const std::string& path, bool /*ro*/, std::string& err) {
    std::string u = lowerAscii(unit);
    if (u == "u22" || u == "u23") {
        Socket& s   = (u == "u23") ? sock2716_ : sock2708_;
        std::string was = s.mount;
        s.mount = path;
        // Re-read to surface an error now; loadSocket logs and leaves the socket erased on failure.
        Image       img;
        std::string e;
        if (!readImage(s.mount, resolvePath(s.mount), 0, img, e)) {
            s.mount = was;
            loadSocket(s);
            err = e + pathNote(path);
            return false;
        }
        loadSocket(s);
        return true;
    }
    if (u.rfind("prom", 0) == 0) {
        char* end = nullptr;
        long  i   = std::strtol(u.c_str() + 4, &end, 10);
        if (end && *end == '\0' && i >= 0 && (size_t)i < proms_.size()) {
            Prom&       p   = proms_[(size_t)i];
            std::string was = p.mount, wasF = p.mountFile;
            p.mount     = path;
            p.mountFile = resolvePath(path);
            Image       img;
            std::string e;
            if (!readImage(p.mount, p.mountFile, p.at, img, e)) {
                p.mount = was; p.mountFile = wasF; loadProm(p);
                err = e + pathNote(path);
                return false;
            }
            loadProm(p);
            decodeChanged();
            return true;
        }
    }
    err = "no unit `" + unit + "` on " + id + " (it has u22, u23" +
          (proms_.empty() ? "" : ", prom0..") + ")";
    return false;
}

bool Pb1Board::unmount(const std::string& unit, std::string& err) {
    std::string u = lowerAscii(unit);
    if (u == "u22" || u == "u23") {
        Socket& s = (u == "u23") ? sock2716_ : sock2708_;
        if (s.mount.empty()) { err = id + ":" + unit + " is empty"; return false; }
        s.mount.clear();
        loadSocket(s);  // back to erased
        return true;
    }
    if (u.rfind("prom", 0) == 0) {
        char* end = nullptr;
        long  i   = std::strtol(u.c_str() + 4, &end, 10);
        if (end && *end == '\0' && i >= 0 && (size_t)i < proms_.size()) {
            Prom& p = proms_[(size_t)i];
            if (p.mount.empty()) { err = id + ":" + unit + " is empty"; return false; }
            p.mount.clear();
            p.mountFile.clear();
            loadProm(p);
            decodeChanged();
            return true;
        }
    }
    err = "no unit `" + unit + "` on " + id;
    return false;
}

std::vector<std::string> Pb1Board::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    for (auto& s : out) s = id + ":" + s;
    return out;
}

std::vector<Property> Pb1Board::subUnitProperties(const std::string& table) const {
    std::vector<Property> p;
    if (table == "socket") {
        {
            Property x;
            x.name    = "chip";
            x.help    = "Which programming socket: 2708 (U22, 1K) or 2716 (U23, 2K)";
            x.kind    = Kind::Enum;
            x.choices = {"2708", "2716"};
            p.push_back(std::move(x));
        }
        {
            Property x;
            x.name = "mount";
            x.help = "A source or erased chip image to put in the socket. Relative to THIS FILE.";
            x.kind = Kind::Str;
            p.push_back(std::move(x));
        }
    } else if (table == "prom") {
        {
            Property x;
            x.name  = "at";
            x.help  = "Where the on-board EPROM sits. An address at or above 8000.";
            x.kind  = Kind::Int;
            x.radix = 16;
            x.min   = 0x8000;
            x.max   = 0xFFFF;
            p.push_back(std::move(x));
        }
        {
            Property x;
            x.name = "mount";
            x.help = "The firmware image: a file (relative to THIS FILE), or builtin:<name>.";
            x.kind = Kind::Str;
            p.push_back(std::move(x));
        }
    }
    return p;
}

bool Pb1Board::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
    if (table == "socket") {
        // loadSubUnit() has already refused an undeclared key and a chip that is not 2708/2716.
        std::string chip, path;
        for (const auto& [k, v] : kv) {
            if (k == "chip") chip = v;
            else if (k == "mount") path = v;
        }
        if (chip.empty()) { err = "[[board.socket]] needs a `chip` (2708 or 2716)"; return false; }
        if (path.empty()) return true;  // a declared-but-empty socket is legal
        return mount(chip == "2716" ? "u23" : "u22", path, false, err);
    }
    if (table == "prom") {
        Prom p;
        bool haveAt = false;
        for (const auto& [k, v] : kv) {
            if (k == "at") {
                long long n;
                if (!parseNumber(v, n, err, 16)) return false;
                p.at   = (uint16_t)n;
                haveAt = true;
            } else if (k == "mount") {
                p.mount     = v;
                p.mountFile = resolvePath(v);
            }
        }
        if (!haveAt)          { err = "[[board.prom]] needs an `at` (at or above 8000)"; return false; }
        if (p.at < 0x8000)    { err = "[[board.prom]] at must be at or above 8000"; return false; }
        if (p.mount.empty())  { err = "[[board.prom]] needs a `mount`"; return false; }
        proms_.push_back(std::move(p));
        loadProm(proms_.back());
        decodeChanged();
        return true;
    }
    err = type() + " has no [[board." + table + "]] table";
    return false;
}

std::vector<Board::SubUnit> Pb1Board::subUnits() const {
    std::vector<SubUnit> out;
    if (!sock2708_.mount.empty()) {
        SubUnit su;
        su.table = "socket";
        su.fields.push_back({"chip", "2708", true});
        su.fields.push_back({"mount", sock2708_.mount, true});
        out.push_back(std::move(su));
    }
    if (!sock2716_.mount.empty()) {
        SubUnit su;
        su.table = "socket";
        su.fields.push_back({"chip", "2716", true});
        su.fields.push_back({"mount", sock2716_.mount, true});
        out.push_back(std::move(su));
    }
    char buf[16];
    for (const auto& p : proms_) {
        SubUnit su;
        su.table = "prom";
        std::snprintf(buf, sizeof buf, "0x%04X", p.at);
        su.fields.push_back({"at", buf, false});
        su.fields.push_back({"mount", p.mount, true});
        out.push_back(std::move(su));
    }
    return out;
}

// ---------------------------------------------------------------------------
// SNAPSHOT / RESTORE (DESIGN.md 13). What travels is the RUNTIME state a running burn
// accumulates: the arm flip-flop, the type latch, and the two socket buffers. The straps
// (port, window), the socket mounts and the read-only PROM images are config -- rebuilt from
// the machine file, re-read on power -- so they are not written here.
// ---------------------------------------------------------------------------
void Pb1Board::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.boolean(armed_);
    w.u8((uint8_t)type_);
    w.raw(sock2708_.buf.data(), k2708);
    w.raw(sock2716_.buf.data(), k2716);
}

void Pb1Board::deserialize(StateReader& r) {
    Board::deserialize(r);
    armed_ = r.boolean();
    type_  = (Chip)r.u8();
    sock2708_.buf.assign(k2708, 0xFF);
    sock2716_.buf.assign(k2716, 0xFF);
    r.raw(sock2708_.buf.data(), k2708);
    r.raw(sock2716_.buf.data(), k2716);
}

} // namespace altair
