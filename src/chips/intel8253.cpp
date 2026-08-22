#include "chips/intel8253.h"

#include "core/clock.h"
#include "core/statefile.h"

#include <cstdio>

namespace altair {

// ---------------------------------------------------------------------------
// THE CONTROL WORD (+3, write-only). SC1/SC0 pick the counter (11 is illegal on a
// plain 8253 -- the read-back command is an 8254 addition, so we ignore it); RL1/RL0
// pick the read/load format (00 = Counter Latch Command); M2/M1/M0 the mode; D0 BCD.
// ---------------------------------------------------------------------------
void Intel8253::writeControl(uint8_t v, const Clock& clk) {
    int sc = (v >> 6) & 0x03;
    if (sc == 3) return;  // 11: illegal on the 8253 (no read-back command here)
    Counter& c = c_[sc];

    int rl = (v >> 4) & 0x03;
    if (rl == 0) {
        // COUNTER LATCH COMMAND: freeze the current count for a stable multi-byte read.
        // It does NOT change the mode or the read/load format -- the latched value is
        // read back in whatever format the last real control word set.
        c.latchVal    = (uint16_t)liveCount(c, clk);
        c.latched     = true;
        c.readMsbNext = false;
        return;
    }

    c.rl = (uint8_t)rl;
    uint8_t mode = (v >> 1) & 0x07;
    if (mode > 5) mode -= 4;  // 110 -> mode 2, 111 -> mode 3 (the silicon aliases them)
    c.mode = mode;
    c.bcd  = (v & 0x01) != 0;

    // A new control word disarms the counter: OUT drops to the mode's initial level and
    // counting waits for a fresh count to be loaded (§3.3).
    c.armed        = false;
    c.initial      = 0;
    c.writeMsbNext = false;
    c.readMsbNext  = false;
    c.latched      = false;
}

// ---------------------------------------------------------------------------
// A COUNTER PORT WRITE (+0/+1/+2). Loads the count per the read/load format; counting
// (re)starts the moment a COMPLETE count is present (for LSB-then-MSB, on the MSB).
// ---------------------------------------------------------------------------
void Intel8253::writeCounter(int n, uint8_t v, const Clock& clk) {
    Counter& c = c_[n];
    bool full = false;
    switch (c.rl) {
    case 1:  // LSB only
        c.initial = v;
        full = true;
        break;
    case 2:  // MSB only
        c.initial = (uint16_t)(v << 8);
        full = true;
        break;
    default:  // 3: LSB then MSB
        if (!c.writeMsbNext) {
            c.writeLsb     = v;
            c.writeMsbNext = true;
        } else {
            c.initial      = (uint16_t)((v << 8) | c.writeLsb);
            c.writeMsbNext = false;
            full = true;
        }
        break;
    }
    if (full) {
        c.loadT_      = clk.now();
        c.armed       = true;
        c.latched     = false;
        c.readMsbNext = false;
    }
}

// A COUNTER PORT READ (+0/+1/+2). Returns the latched value if a Latch Command is
// pending, else the live count -- LSB, MSB, or LSB-then-MSB per the format.
uint8_t Intel8253::readCounter(int n, const Clock& clk) {
    Counter& c = c_[n];
    uint32_t dec = c.latched ? c.latchVal : liveCount(c, clk);
    uint16_t v   = c.bcd ? toBcd(dec) : (uint16_t)dec;

    uint8_t byte;
    switch (c.rl) {
    case 1:  // LSB only
        byte = (uint8_t)(v & 0xFF);
        c.latched = false;
        break;
    case 2:  // MSB only
        byte = (uint8_t)((v >> 8) & 0xFF);
        c.latched = false;
        break;
    default:  // 3: LSB then MSB
        if (!c.readMsbNext) {
            byte = (uint8_t)(v & 0xFF);
            c.readMsbNext = true;
        } else {
            byte = (uint8_t)((v >> 8) & 0xFF);
            c.readMsbNext = false;
            c.latched     = false;  // both halves out -- release the latch
        }
        break;
    }
    return byte;
}

void Intel8253::powerOn(const Clock&) {
    for (Counter& c : c_) c = Counter{};
}

// ---------------------------------------------------------------------------
// The counting math. Everything is in decimal counter-ticks; BCD is decoded at the
// register edges (fromBcd on the way in, toBcd on the way out) so the interior is
// one arithmetic.
// ---------------------------------------------------------------------------
uint64_t Intel8253::elapsed(const Counter& c, const Clock& clk) const {
    if (!c.armed) return 0;
    uint64_t per = clk.tStatesPer(counterHz);
    if (per == 0) return 0;
    uint64_t now = clk.now();
    if (now <= c.loadT_) return 0;
    return (now - c.loadT_) / per;
}

uint32_t Intel8253::modulus(const Counter& c) const { return c.bcd ? 10000u : 65536u; }

uint32_t Intel8253::effectiveN(const Counter& c) const {
    uint32_t raw = c.bcd ? fromBcd(c.initial) : c.initial;
    return raw == 0 ? modulus(c) : raw;  // a count of 0 means the full modulus
}

uint32_t Intel8253::liveCount(const Counter& c, const Clock& clk) const {
    if (!c.armed) return c.bcd ? fromBcd(c.initial) : c.initial;
    uint32_t M = modulus(c), N = effectiveN(c);
    uint64_t e = elapsed(c, clk);
    if (c.mode == 2) {
        // Rate generator: counts N..1 and reloads; it never rests at 0.
        uint32_t p = (uint32_t)(e % N);
        return N - p;
    }
    // Modes 0/1/3/4/5 read back as a plain down-counter that wraps through 0.
    uint32_t p = (uint32_t)(e % M);
    return (N - p + M) % M;
}

bool Intel8253::outOf(const Counter& c, const Clock& clk) const {
    if (!c.armed) return c.mode != 0;  // unloaded: mode 0 low, every other mode high

    uint32_t N = effectiveN(c);
    uint64_t e = elapsed(c, clk);
    switch (c.mode) {
    case 0:  // Interrupt on Terminal Count: low, then high at TC and stays high.
        return e >= N;
    case 4:  // Software-triggered strobe: high, one tick low at terminal count.
        return e != N;
    case 2: {  // Rate generator: low for the single tick the count would read 1.
        uint32_t p = (uint32_t)(e % N);
        return p != (N - 1);
    }
    case 3: {  // Square wave: high the first half of the period, low the second.
        uint32_t p  = (uint32_t)(e % N);
        uint32_t hi = (N + 1) / 2;  // an odd count spends the extra tick high
        return p < hi;
    }
    default:  // Modes 1 and 5 need a gate rising edge; the SS-1 ties the gate high, so
        return true;  // one never comes -- they idle at their initial OUT (high).
    }
}

bool     Intel8253::out(int n, const Clock& clk) const { return outOf(c_[n], clk); }

uint16_t Intel8253::count(int n, const Clock& clk) const {
    const Counter& c = c_[n];
    uint32_t dec = c.latched ? c.latchVal : liveCount(c, clk);
    return c.bcd ? toBcd(dec) : (uint16_t)dec;
}

uint64_t Intel8253::nextEdge(int n, const Clock& clk) const {
    const Counter& c = c_[n];
    if (!c.armed) return 0;
    uint64_t per = clk.tStatesPer(counterHz);
    if (per == 0) return 0;

    uint32_t N = effectiveN(c);
    uint64_t e = elapsed(c, clk);
    uint64_t eNext = 0;  // the tick index of the next OUT change; 0 == none from here

    switch (c.mode) {
    case 0:
        if (e < N) eNext = N;   // the one low->high edge at terminal count
        break;
    case 4:
        if (e < N)          eNext = N;      // high -> low (the strobe)
        else if (e < N + 1) eNext = N + 1;  // low -> high (one tick later)
        break;
    case 2: {
        uint32_t p = (uint32_t)(e % N);
        eNext = e + (p == (N - 1) ? 1 : (N - 1 - p));  // to the low tick, or off it
        break;
    }
    case 3: {
        uint32_t p  = (uint32_t)(e % N);
        uint32_t hi = (N + 1) / 2;
        eNext = e + (p < hi ? (hi - p) : (N - p));  // to the next half-period boundary
        break;
    }
    default:  // modes 1, 5: no self-driven edge (see outOf)
        break;
    }

    if (eNext == 0) return 0;
    return c.loadT_ + eNext * per;
}

uint16_t Intel8253::toBcd(uint32_t v) {
    v %= 10000;
    return (uint16_t)(((v / 1000 % 10) << 12) | ((v / 100 % 10) << 8) |
                      ((v / 10 % 10) << 4) | (v % 10));
}

uint32_t Intel8253::fromBcd(uint16_t v) {
    return ((v >> 12) & 0xF) * 1000 + ((v >> 8) & 0xF) * 100 + ((v >> 4) & 0xF) * 10 +
           (v & 0xF);
}

std::string Intel8253::describe(const Clock& clk) const {
    std::string s;
    char buf[64];
    for (int n = 0; n < 3; ++n) {
        std::snprintf(buf, sizeof buf, "%sC%d mode%d count=%u OUT=%d", n ? "; " : "", n,
                      c_[n].mode, (unsigned)count(n, clk), out(n, clk) ? 1 : 0);
        s += buf;
    }
    return s;
}

void Intel8253::serialize(StateWriter& w) const {
    for (const Counter& c : c_) {
        w.u8(c.mode);
        w.boolean(c.bcd);
        w.u8(c.rl);
        w.u16(c.initial);
        w.boolean(c.writeMsbNext);
        w.u8(c.writeLsb);
        w.boolean(c.readMsbNext);
        w.boolean(c.latched);
        w.u16(c.latchVal);
        w.u64(c.loadT_);
        w.boolean(c.armed);
    }
}

void Intel8253::deserialize(StateReader& r) {
    for (Counter& c : c_) {
        c.mode         = r.u8();
        c.bcd          = r.boolean();
        c.rl           = r.u8();
        c.initial      = r.u16();
        c.writeMsbNext = r.boolean();
        c.writeLsb     = r.u8();
        c.readMsbNext  = r.boolean();
        c.latched      = r.boolean();
        c.latchVal     = r.u16();
        c.loadT_       = r.u64();
        c.armed        = r.boolean();
    }
}

}  // namespace altair
