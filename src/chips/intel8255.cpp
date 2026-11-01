#include "chips/intel8255.h"

#include "core/statefile.h"

namespace altair {

namespace {

// ---- the mode word (reference/Intel 8255.md 3): a 1 makes the port an input ----
constexpr uint8_t kModeSet = 0x80;
constexpr uint8_t kInA     = 0x10;
constexpr uint8_t kInCHi   = 0x08;
constexpr uint8_t kInB     = 0x02;
constexpr uint8_t kInCLo   = 0x01;

// All four inputs, in mode 0.
constexpr uint8_t kAllInputs = kModeSet | kInA | kInCHi | kInB | kInCLo;

} // namespace

void Intel8255::reset() {
    control_ = kAllInputs;
    outA_ = outB_ = outC_ = 0;
}

uint8_t Intel8255::read(uint8_t reg) const {
    switch (reg & 3) {
    case 0: return (control_ & kInA) ? pinA : outA_;
    case 1: return (control_ & kInB) ? pinB : outB_;
    case 2: {
        uint8_t hi = (control_ & kInCHi) ? pinC : outC_;
        uint8_t lo = (control_ & kInCLo) ? pinC : outC_;
        return (uint8_t)((hi & 0xF0) | (lo & 0x0F));
    }
    default: return 0xFF;  // not readable: the data bus stays 3-state
    }
}

void Intel8255::write(uint8_t reg, uint8_t v) {
    switch (reg & 3) {
    case 0: outA_ = v; break;
    case 1: outB_ = v; break;
    case 2: outC_ = v; break;
    default:
        if (v & kModeSet) {
            control_ = v;
            outA_ = outB_ = outC_ = 0;  // a mode set resets all the output registers
        } else {
            uint8_t bit = (uint8_t)(1u << ((v >> 1) & 7));
            outC_ = (v & 1) ? (uint8_t)(outC_ | bit) : (uint8_t)(outC_ & ~bit);
        }
        break;
    }
}

void Intel8255::serialize(StateWriter& w) const {
    w.u8(control_);
    w.u8(outA_);
    w.u8(outB_);
    w.u8(outC_);
}

void Intel8255::deserialize(StateReader& r) {
    control_ = r.u8();
    outA_    = r.u8();
    outB_    = r.u8();
    outC_    = r.u8();
}

} // namespace altair
