#include "boards/newtech-music.h"

#include "core/clock.h"
#include "core/statefile.h"

#include <cstdio>

namespace altair {
namespace {

// C7 (0.1 uF) into R14 (2 megohms) + R15 (25 K): 1 / (2 pi R C).
constexpr double kCouplingHz = 0.786;

} // namespace

NewtechMusicBoard::NewtechMusicBoard() { spk_.setAcCoupled(kCouplingHz); }

// ---------------------------------------------------------------------------
// Bus: one output port at four addresses. A1 and A0 do not reach the board.
// ---------------------------------------------------------------------------
bool NewtechMusicBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoWrite) return false;
    return (c.port() & 0xFC) == base_;
}

void NewtechMusicBoard::write(const BusCycle& c) {
    // DO7..DO2 are latched; DO1 and DO0 are not wired. A change is recorded with the time
    // it happened. That is all a bus cycle does; pump() makes the sound.
    const uint8_t v = c.data & 0xFC;
    if (v == latch_) return;
    latch_ = v;
    if (clock_) spk_.edge(clock_->now(), level());
}

// ---------------------------------------------------------------------------
// Lifecycle. There is no reset(): the latch's clear pin is tied high.
// ---------------------------------------------------------------------------
void NewtechMusicBoard::power() {
    // A 74LS174 comes up in no defined state. Zero is as good as any, and a guest cannot
    // read it back.
    latch_ = 0;
    spk_.clear();
    spk_.resync(0, level());
}

void NewtechMusicBoard::pump() {
    if (clock_) spk_.pump(*clock_, true);
}

// ---------------------------------------------------------------------------
// State.
// ---------------------------------------------------------------------------
void NewtechMusicBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u8(latch_);
}

void NewtechMusicBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    latch_ = r.u8() & 0xFC;
    // The clock and the latch are another moment's now.
    spk_.resync(clock_ ? clock_->now() : 0, level());
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> NewtechMusicBoard::properties() {
    std::vector<Property> p;
    Property x;
    x.name  = "port";
    x.help  = "The output port (jumpers J1..J8 set A7..A4): 04, 14, 24 ... F4. The board "
              "answers at this address and the next three. Default 24";
    x.kind  = Kind::Int;
    x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
    x.min   = 0;
    x.max   = 0xF4;
    x.get   = [this] { return Value::ofInt(base_); };
    x.set   = [this](const Value& v, std::string& err) {
        if ((v.i() & 0x0F) != 4) {
            err = "the Model 6 decodes A7..A4 by jumper with A3 = 0 and A2 = 1 fixed -- "
                  "the port must end in 4 (04, 14, 24 ... F4)";
            return false;
        }
        base_ = (uint8_t)v.i();
        return true;
    };
    p.push_back(std::move(x));
    p.push_back(volumeProperty(spk_.volume()));
    return p;
}

// The live picture for SHOW <id>: what the latch holds, and whether the speaker plays --
// and if it does not, the reason.
std::vector<std::string> NewtechMusicBoard::statusLines() const {
    char buf[64];
    std::snprintf(buf, sizeof buf, "latch    %02X  (D/A step %d of 63)", latch_, latch_ >> 2);
    return {buf, "speaker  " + spk_.status(clock_)};
}

std::vector<MapEntry> NewtechMusicBoard::ioMap() const {
    return {
        {(uint32_t)base_, (uint32_t)(base_ + 3), "write",
         "Model 6 -- 6-bit D/A latch (DO7..DO2); one port at four addresses"},
    };
}

} // namespace altair
