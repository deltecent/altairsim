#include "boards/compupro-mdriveh.h"

#include "core/statefile.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <random>

namespace altair {

MDriveHBoard::MDriveHBoard() { resize(boards_); }

// Each card gets its own rng stream, so a change of `boards` does not disturb the bytes on
// the cards already fitted -- the idiom the bankmem board uses per segment.
void MDriveHBoard::fillCard(int card) {
    uint8_t* p = ram_.data() + (size_t)card * kCardBytes;
    if (fill_ == Fill::Zero) {
        std::fill(p, p + kCardBytes, uint8_t{0});
        return;
    }
    std::mt19937_64 rng(seed_ ^ (0x9E3779B97F4A7C15ULL * (uint64_t)(card + 1)));
    for (uint32_t i = 0; i < kCardBytes; i += 8) {
        uint64_t r = rng();
        for (int k = 0; k < 8; ++k) p[i + k] = (uint8_t)(r >> (8 * k));
    }
}

void MDriveHBoard::resize(int boards) {
    const int had = (int)(ram_.size() / kCardBytes);
    // Sized up front, then filled: no growth of a byte vector by push_back (GCC 15).
    std::vector<uint8_t> next((size_t)boards * kCardBytes);
    std::copy(ram_.begin(), ram_.begin() + (std::ptrdiff_t)((size_t)std::min(had, boards) * kCardBytes),
              next.begin());
    ram_.swap(next);
    boards_ = boards;
    for (int c = had; c < boards; ++c) fillCard(c);
}

// The power comes up: DRAM holds nothing it was given, and the counter's state is not
// defined on the real card. It starts at 0 here.
void MDriveHBoard::power() {
    for (int c = 0; c < boards_; ++c) fillCard(c);
    addr_ = 0;
}

// ---------------------------------------------------------------------------
// The bus (§2-§4).
// ---------------------------------------------------------------------------
bool MDriveHBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    const uint8_t p = c.port();
    if (c.type == Cycle::IoWrite) return p == port_ || p == (uint8_t)(port_ + 1);
    if (c.type == Cycle::IoRead) return p == port_;  // the address port has no read path
    return false;
}

// The data port. A card is selected when its cascade number (MA21-MA19) is fitted; with
// none selected nothing drives the bus and the CPU reads FF. The counter counts either way.
uint8_t MDriveHBoard::read(const BusCycle&) {
    const uint8_t v = addr_ < ram_.size() ? ram_[addr_] : 0xFF;
    addr_ = (addr_ + 1) & kAddrMask;
    return v;
}

void MDriveHBoard::write(const BusCycle& c) {
    if (c.port() == port_) {
        if (addr_ < ram_.size()) ram_[addr_] = c.data;
        addr_ = (addr_ + 1) & kAddrMask;
        return;
    }
    // The address port: the 74LS161 chain shifts up one byte and loads the new one low.
    addr_ = ((addr_ << 8) | c.data) & kAddrMask;
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> MDriveHBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Base address (S1-1..7 = A7-A1). The data port is BASE, the address port "
                  "BASE+1 (default C6)";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xFE;
        x.get   = [this] { return Value::ofInt(port_); };
        x.set   = [this](const Value& v, std::string& err) {
            if (v.i() % 2) {
                err = "the base is even: A0 picks the data or the address port, and S1 sets "
                      "A7-A1 only";
                return false;
            }
            port_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "boards";
        x.help = "How many 512K boards share the ports, 1-8. They take the cascade numbers "
                 "0 to boards-1 (S1-8..10), so they fill the address space from 0 without a gap";
        x.kind = Kind::Int;
        x.min  = 1;
        x.max  = kMaxBoards;
        x.get  = [this] { return Value::ofInt(boards_); };
        x.set  = [this](const Value& v, std::string&) {
            resize((int)v.i());
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "fill";
        x.help    = "RAM contents at power-on: zero | random (real RAM is not zeroed)";
        x.kind    = Kind::Enum;
        x.choices = {"zero", "random"};
        x.get     = [this] { return Value::ofStr(fill_ == Fill::Zero ? "zero" : "random"); };
        x.set     = [this](const Value& v, std::string&) {
            fill_ = (v.s() == "zero") ? Fill::Zero : Fill::Random;
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "seed";
        x.help = "seed for fill=random -- the same seed fills RAM the same way at every "
                 "POWER, so a run is repeatable";
        x.kind = Kind::Int;
        x.get  = [this] { return Value::ofInt((long long)seed_); };
        x.set  = [this](const Value& v, std::string&) {
            seed_ = (uint64_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "address";
        x.help  = "the 22-bit address counter -- the guest loads it with three OUTs to "
                  "BASE+1";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.get   = [this] { return Value::ofInt(addr_); };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<MapEntry> MDriveHBoard::ioMap() const {
    return {
        {port_, port_, "read/write", "M-Drive/H data, the counter counts up after each access"},
        {(uint32_t)port_ + 1, (uint32_t)port_ + 1, "write",
         "M-Drive/H address, 3 bytes, high byte first"},
    };
}

std::vector<std::string> MDriveHBoard::statusLines() const {
    char buf[96];
    const int card = (int)(addr_ >> 19);
    std::snprintf(buf, sizeof buf, "address %06X  board %d %s  boards %d (%dK)",
                  (unsigned)addr_, card, card < boards_ ? "present" : "absent (reads FF)",
                  boards_, boards_ * 512);
    return {buf};
}

// ---------------------------------------------------------------------------
// SNAPSHOT / RESTORE: the counter, the card count and every byte of RAM.
// ---------------------------------------------------------------------------
void MDriveHBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u32(addr_);
    w.u8((uint8_t)boards_);
    w.blob(ram_);
}

void MDriveHBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    addr_ = r.u32() & kAddrMask;
    int n = r.u8();
    if (n < 1) n = 1;
    if (n > kMaxBoards) n = kMaxBoards;
    boards_ = n;
    const std::vector<uint8_t> raw = r.blob();
    std::vector<uint8_t> next((size_t)n * kCardBytes);  // sized up front (GCC 15)
    std::copy(raw.begin(), raw.begin() + (std::ptrdiff_t)std::min(raw.size(), next.size()),
              next.begin());
    ram_.swap(next);
    decodeChanged();
}

}  // namespace altair
