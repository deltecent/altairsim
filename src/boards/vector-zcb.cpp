#include "boards/vector-zcb.h"

#include "core/clock.h"
#include "core/hex.h"
#include "core/roms.h"
#include "core/statefile.h"
#include "core/value.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <utility>

namespace altair {

namespace {

EndpointResolver g_resolver;

Clock& deadCard() {
    static Clock stopped;
    return stopped;
}

// Switch S1 (reference 6.2). CLK is 2 MHz, 13 divides it, and the labels are for a clock
// factor of 16. Each rocker halves the one before, and 110 is the 1200 rate over 11.
struct Rocker {
    long long label;
    long long divide;
};
constexpr Rocker kRockers[] = {{9600, 1}, {4800, 2}, {2400, 4}, {1200, 8},
                               {600, 16}, {300, 32}, {150, 64}, {110, 88}};

constexpr long long kBaudClock = 2000000 / 13;

} // namespace

void ZcbBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

ZcbBoard::ZcbBoard() {
    u_.disconnect();
    u_.dsrSrc = DsrSource::Line;  // R23 pulls DSR* active with the line open (reference 6.3)
    setBaud(9600);
}

ZcbBoard::~ZcbBoard() {
    if (clock_) clock_->cancel(wake_);
}

void ZcbBoard::setBaud(long long label) {
    for (const Rocker& r : kRockers)
        if (r.label == label) {
            u_.baud  = label;
            u_.txcHz = kBaudClock / r.divide;
        }
}

// ---------------------------------------------------------------------------
// The bus. A memory write is not decoded: it goes out to the bus and snoop() takes the
// on-board RAM's copy. A write to the PROM range reaches nothing on the board.
// ---------------------------------------------------------------------------
bool ZcbBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type == Cycle::IoRead || c.type == Cycle::IoWrite)
        return isSerial(c.port()) || isParallel(c.port());
    if (c.type == Cycle::MemRead) {
        uint16_t t;
        return onboard(c.addr, t) != Onboard::None;
    }
    return false;
}

bool ZcbBoard::peek(uint16_t addr, uint8_t& out) const {
    uint16_t t;
    switch (onboard(addr, t)) {
    case Onboard::Prom: out = prom_[t - kPromBase]; return true;
    case Onboard::Ram:  out = ram_[t - kRamBase]; return true;
    case Onboard::None: break;
    }
    return false;
}

uint8_t ZcbBoard::read(const BusCycle& c) {
    if (c.type == Cycle::MemRead) {
        uint8_t v = 0xFF;
        peek(c.addr, v);
        return v;
    }
    const uint8_t port = c.port();
    if (isParallel(port)) return p_.read(port & 3);

    const Clock& clk = clock_ ? *clock_ : deadCard();
    uint8_t v = (port & 1) ? u_.readStatus(clk) : u_.readData(clk);
    refresh();
    return v;
}

void ZcbBoard::write(const BusCycle& c) {
    const uint8_t port = c.port();
    if (isParallel(port)) {
        p_.write(port & 3, c.data);
        return;
    }
    const Clock& clk = clock_ ? *clock_ : deadCard();
    if (port & 1) u_.writeControl(c.data, clk);
    else          u_.writeData(c.data, clk);
    refresh();
}

void ZcbBoard::snoop(const BusCycle& c) {
    if (wait_ == Wait::All || (wait_ == Wait::M1 && (c.status & StM1))) holdReady(1);

    if (c.type != Cycle::MemRead && c.type != Cycle::MemWrite) return;

    uint16_t t;
    if (c.type == Cycle::MemWrite && onboard(c.addr, t) == Onboard::Ram)
        ram_[t - kRamBase] = c.data;

    // The match that clocks U27 comes from the address on the bus, not from the override.
    if (armed_ && c.addr >= kBlock) {
        armed_ = false;
        decodeChanged();
    }
}

// ---------------------------------------------------------------------------
// The receiver and the transmitter run on the chip's own time, as on the SBC. No interrupt
// wire hangs off this: refresh() only has to keep a queued byte moving.
// ---------------------------------------------------------------------------
void ZcbBoard::refresh() {
    if (!clock_) return;

    u_.poll(*clock_);

    clock_->cancel(wake_);
    wake_ = Clock::kNone;
    if (uint64_t next = nextEdge()) wake_ = clock_->at(next, [this] { refresh(); });
}

uint64_t ZcbBoard::nextEdge() const {
    const Clock& clk = clock_ ? *clock_ : deadCard();

    uint64_t best = 0;
    auto consider = [&](uint64_t when) {
        if (when <= clk.now()) return;
        if (!best || when < best) best = when;
    };

    if (u_.rxFrameActive()) {
        consider(u_.rxDoneAt());
    } else if (u_.rxWaiting()) {
        consider(u_.rxNextAt());
    }
    return best;
}

// RESET* reaches the processor, U27, and through U34 the RESET pins of both chips (reference
// 3). The RAM keeps its contents.
void ZcbBoard::reset(Reset r) {
    CpuZ80Board::reset(r);
    armed_ = true;
    p_.reset();
    decodeChanged();
    if (!clock_) return;
    u_.powerOn(*clock_);
    refresh();
}

void ZcbBoard::power() {
    loadProm();

    // The RAM chips just powered up. A fixed seed, so a POWER is repeatable.
    std::mt19937 rng(0x2C2Bu);
    for (uint8_t& b : ram_) b = (uint8_t)(rng() & 0xFF);

    CpuZ80Board::power();
    reset(Reset::PowerOn);
}

void ZcbBoard::pump() {
    u_.pump();
    refresh();
}

void ZcbBoard::configChanged() {
    decodeChanged();
    u_.programLine();
    refresh();
}

void ZcbBoard::serialize(StateWriter& w) const {
    CpuZ80Board::serialize(w);
    u_.serialize(w);
    p_.serialize(w);
    w.boolean(armed_);
    w.raw(ram_, kRamSize);
}

void ZcbBoard::deserialize(StateReader& r) {
    CpuZ80Board::deserialize(r);
    u_.deserialize(r);
    p_.deserialize(r);
    armed_ = r.boolean();
    r.raw(ram_, kRamSize);
    decodeChanged();
    refresh();
}

// ---------------------------------------------------------------------------
// Reflection
// ---------------------------------------------------------------------------
std::vector<Property> ZcbBoard::properties() {
    std::vector<Property> p = CpuZ80Board::properties();

    // Both chips hang off one decoder, so the group is one jumper area and the bases are
    // offsets in it. Written this way, the two chips cannot leave the group.
    {
        Property x;
        x.name  = "group";
        x.help  = "The 32-port group of both chips (area G): 00, 20, 40 ... E0";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xE0;
        x.get   = [this] { return Value::ofInt(group_); };
        x.set   = [this](const Value& v, std::string& err) {
            if (v.i() & 0x1F) {
                err = "group is a multiple of 20: 00, 20, 40 ... E0";
                return false;
            }
            group_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }

    auto base = [](const char* name, const char* help, uint8_t& mine, const uint8_t& other) {
        Property x;
        x.name  = name;
        x.help  = help;
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0x1C;
        x.get   = [&mine] { return Value::ofInt(mine); };
        x.set   = [&mine, &other, n = std::string(name)](const Value& v, std::string& err) {
            if (v.i() & 3) {
                err = n + " is a multiple of 4: 00, 04 ... 1C";
                return false;
            }
            if ((uint8_t)v.i() == other) {
                err = "the serial and parallel ports cannot share a base";
                return false;
            }
            mine = (uint8_t)v.i();
            return true;
        };
        return x;
    };
    p.push_back(base("serial_port", "Base of the 8251 in the group (area E). Data at BASE, status "
                                    "and command at BASE+1, repeated at BASE+2 and BASE+3",
                     serial_, parallel_));
    p.push_back(base("parallel_port", "Base of the 8255 in the group (area E): ports A, B, C and "
                                      "control",
                     parallel_, serial_));

    {
        Property x;
        x.name = "boot";
        x.help = "Jumper B: after a reset every address decodes as the E000 block, until the "
                 "first memory cycle in E000-FFFF";
        x.kind = Kind::Bool;
        x.get  = [this] { return Value::ofBool(boot_); };
        x.set  = [this](const Value& v, std::string&) {
            boot_ = v.b();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "phantom";
        x.help = "Jumper C: the board pulls PHANTOM* from a reset until the first memory cycle "
                 "in E000-FFFF";
        x.kind = Kind::Bool;
        x.get  = [this] { return Value::ofBool(phantom_); };
        x.set  = [this](const Value& v, std::string&) {
            phantom_ = v.b();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "wait";
        x.help    = "Area N, the automatic wait state: m1 = one after each M1, none, or all = one "
                    "on each bus cycle";
        x.kind    = Kind::Enum;
        x.choices = {"m1", "none", "all"};
        x.get     = [this] {
            return Value::ofStr(wait_ == Wait::M1 ? "m1" : wait_ == Wait::None ? "none" : "all");
        };
        x.set = [this](const Value& v, std::string&) {
            wait_ = v.s() == "m1" ? Wait::M1 : v.s() == "none" ? Wait::None : Wait::All;
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<UnitDef> ZcbBoard::units() const {
    std::vector<UnitDef> out = CpuZ80Board::units();
    out.push_back({"serial", UnitKind::Serial, u_.endpoint()});
    return out;
}

std::vector<Property> ZcbBoard::unitProperties(const std::string& unit) {
    if (unit != "serial") return {};
    std::vector<Property> p;
    {
        Property x;
        x.name    = "baud";
        x.help    = "Switch S1, for a guest that selects a clock factor of 16. The 2 MHz clock "
                    "over 13 makes the label 9600 run at 9615";
        x.kind    = Kind::Enum;
        x.choices = {"110", "150", "300", "600", "1200", "2400", "4800", "9600"};
        x.get     = [this] { return Value::ofStr(std::to_string(u_.baud)); };
        x.set     = [this](const Value& v, std::string&) {
            setBaud(std::stoll(v.s()));
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "connect";
        x.help = "The endpoint on the serial line (CONNECT sets this)";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(u_.endpoint()); };
        x.set  = [this](const Value& v, std::string& err) { return connect("serial", v.s(), err); };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<MapEntry> ZcbBoard::ioMap() const {
    const uint32_t s = serialBase(), q = parallelBase();
    return {
        {s, s, "read/write", "8251 -- receive/transmit data"},
        {s + 1, s + 1, "read/write", "8251 -- status / mode+command"},
        {s + 2, s + 3, "read/write", "8251 again: A1 is not decoded"},
        {q, q, "read/write", "8255 -- port A"},
        {q + 1, q + 1, "read/write", "8255 -- port B"},
        {q + 2, q + 2, "read/write", "8255 -- port C"},
        {q + 3, q + 3, "write", "8255 -- mode word, or port C bit set and reset"},
    };
}

std::vector<MapEntry> ZcbBoard::memMap() const {
    std::vector<MapEntry> m;
    for (const auto& sock : sockets_) {
        if (sock.mount.empty()) continue;
        uint32_t hi = sock.at;
        while (hi <= kPromLast && promLine_[(hi - kPromBase) >> 10]) hi += 0x400;
        if (hi == sock.at) continue;  // the socket did not load
        m.push_back({(uint32_t)sock.at, hi - 1, "read", "on-board PROM socket (" + sock.mount + ")"});
    }
    m.push_back({(uint32_t)kRamBase, 0xFFFF, "read/write",
                 "on-board 1K RAM; a write also reaches any board under it"});
    return m;
}

bool ZcbBoard::connect(const std::string& unit, const std::string& ep, std::string& err) {
    if (unit != "serial") {
        err = "zcb has no unit '" + unit + "' to connect -- it has 'serial'";
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
    u_.connect(std::move(s));
    refresh();
    return true;
}

bool ZcbBoard::disconnect(const std::string& unit, std::string& err) {
    if (unit != "serial") {
        err = "zcb has no unit '" + unit + "' to disconnect -- it has 'serial'";
        return false;
    }
    u_.disconnect();
    refresh();
    return true;
}

bool ZcbBoard::connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                             std::string& err) {
    if (unit != "serial") {
        err = "zcb has no unit '" + unit + "' to connect -- it has 'serial'";
        return false;
    }
    u_.connect(std::move(s));
    refresh();
    return true;
}

std::vector<std::string> ZcbBoard::drainLog() {
    std::vector<std::string> out = u_.drainLog();
    for (auto& s : log_) out.push_back(std::move(s));
    log_.clear();
    return out;
}

// ---------------------------------------------------------------------------
// The PROM sockets. The size of the image picks the chip: 1K (2708), 2K (2716) or 4K (2732).
// A chip sits on a boundary of its own size, and the whole chip answers, on 1K decode lines.
// ---------------------------------------------------------------------------
void ZcbBoard::loadProm() {
    std::fill(std::begin(prom_), std::end(prom_), (uint8_t)0xFF);
    std::fill(std::begin(promLine_), std::end(promLine_), false);
    decodeChanged();

    for (const auto& sock : sockets_) {
        if (sock.mount.empty()) continue;

        Image       img;
        std::string err;
        if (sock.mount.rfind("builtin:", 0) == 0) {
            std::string       name = sock.mount.substr(8);
            const BuiltinRom* rom  = findRom(name);
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

        uint32_t last = 0;
        unsigned outside = 0;
        for (const auto& [a, b] : img.bytes) {
            (void)b;
            if (a >= sock.at && a <= kPromLast) last = std::max<uint32_t>(last, a);
            else ++outside;
        }
        char buf[200];
        if (last == 0) {
            log_.push_back(id + ": " + sock.mount + ": no bytes for this socket");
            continue;
        }
        if (outside) {
            std::snprintf(buf, sizeof buf, "%s: %s: %u byte(s) outside %04X-EFFF were not loaded",
                          id.c_str(), sock.mount.c_str(), outside, sock.at);
            log_.push_back(buf);
        }
        const uint32_t used = last - sock.at + 1;
        const uint32_t chip = used <= 0x400 ? 0x400 : used <= 0x800 ? 0x800 : 0x1000;
        if (sock.at & (chip - 1)) {
            std::snprintf(buf, sizeof buf, "%s: %s: a %uK PROM cannot sit at %04X", id.c_str(),
                          sock.mount.c_str(), (unsigned)(chip / 1024), sock.at);
            log_.push_back(buf);
            continue;
        }
        const int first = (sock.at - kPromBase) >> 10;
        const int lines = (int)(chip >> 10);
        bool      taken = false;
        for (int n = first; n < first + lines; ++n) taken = taken || promLine_[n];
        if (taken) {
            std::snprintf(buf, sizeof buf, "%s: %s at %04X overlaps another socket", id.c_str(),
                          sock.mount.c_str(), sock.at);
            log_.push_back(buf);
            continue;
        }
        for (int n = first; n < first + lines; ++n) promLine_[n] = true;
        for (const auto& [a, b] : img.bytes)
            if (a >= sock.at && a <= last) prom_[a - kPromBase] = b;
    }
}

std::vector<Property> ZcbBoard::subUnitProperties(const std::string& table) const {
    if (table != "socket") return {};
    std::vector<Property> p;
    {
        Property x;
        x.name  = "at";
        x.help  = "Where the PROM sits: E000, E400, E800 or EC00";
        x.kind  = Kind::Int;
        x.radix = 16;
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "mount";
        x.help = "What is in the socket: builtin:<name> or a HEX/BIN path, 1K to 4K. Relative to "
                 "THIS FILE.";
        x.kind = Kind::Str;
        p.push_back(std::move(x));
    }
    return p;
}

bool ZcbBoard::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
    if (table != "socket") {
        err = type() + " has no [[board." + table + "]] table";
        return false;
    }
    Socket sock{0, {}};
    bool   haveAt = false;
    for (const auto& [k, v] : kv) {
        if (k == "at") {
            long long n = 0;
            if (!parseNumber(v, n, err, 16)) return false;
            if (n < kPromBase || n > kPromLast || (n & 0x3FF)) {
                err = "[[board.socket]] at is E000, E400, E800 or EC00";
                return false;
            }
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
    if (sockets_.size() == kSockets) {
        err = "the ZCB has three PROM sockets";
        return false;
    }
    sockets_.push_back(std::move(sock));
    loadProm();
    return true;
}

std::vector<Board::SubUnit> ZcbBoard::subUnits() const {
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
