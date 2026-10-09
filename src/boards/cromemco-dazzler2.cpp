#include "boards/cromemco-dazzler2.h"

#include "core/statefile.h"

#include <random>

namespace altair {

Dazzler2Board::Dazzler2Board() {
    undrivenBits_ = 0x00;   // the II drives IN BASE D5-D0 low (BJL_ver4.pld, LDI0-LDI5)
}

// A memory write goes to card RAM when the bus address, less the base, is 0000-0FFF.
// The card's logic subtracts A15-A9 and accepts when the difference has A15-A12 clear and
// there was no borrow, so an address below the base does not wrap round into the window. It
// is NOT gated by the display being on: the card captures from the moment a base is set.
// Main memory is untouched by this -- the cycle is simply watched.
void Dazzler2Board::snoop(const BusCycle& c) {
    if (c.type != Cycle::MemWrite) return;
    uint32_t off = (uint32_t)c.addr - base_;   // below the base wraps to a huge value: refused
    if (off >= kRamBytes) return;
    ram_[off] = c.data;
}

// What the scan reads: a 2 KB half of the 4 KB array. The picture is at most 2 KB, so the
// offset never leaves the half.
uint8_t Dazzler2Board::sample(uint16_t off) const {
    return ram_[(off + (secondPage_ ? kPageBytes : 0)) & (kRamBytes - 1)];
}

// Static RAM does not power up holding zero. The `seed` makes a power repeatable, the way
// the memory board's does.
void Dazzler2Board::power() {
    DazzlerBoard::power();
    std::mt19937_64 rng(seed_);
    for (auto& b : ram_) b = (uint8_t)(rng() & 0xFF);
}

void Dazzler2Board::serialize(StateWriter& w) const {
    DazzlerBoard::serialize(w);
    w.raw(ram_, kRamBytes);
}

void Dazzler2Board::deserialize(StateReader& r) {
    DazzlerBoard::deserialize(r);
    r.raw(ram_, kRamBytes);
}

std::vector<Property> Dazzler2Board::properties() {
    std::vector<Property> p = DazzlerBoard::properties();
    {
        Property x;
        x.name    = "page";
        x.help    = "Which 2 KB half of the card's 4 KB RAM is shown: 0 = the first, 800 = the "
                    "second, at +800H (jumper P18 pins 31-32 closed)";
        x.kind    = Kind::Enum;
        x.choices = {"0", "800"};
        x.get     = [this] { return Value::ofStr(secondPage_ ? "800" : "0"); };
        x.set     = [this](const Value& v, std::string&) {
            secondPage_ = (v.s() == "800");
            dirty_      = true;   // the picture changes without a write
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "seed";
        x.help = "Seed for the undefined contents of the card RAM at power-on. The same seed "
                 "gives the same contents at every power";
        x.kind = Kind::Int;
        x.min  = 0;
        x.max  = 0;   // unbounded
        x.get  = [this] { return Value::ofInt((long long)seed_); };
        x.set  = [this](const Value& v, std::string&) {
            seed_ = (uint64_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

} // namespace altair
