#pragma once
//
// The Altair 680b CPU card -- docs/boards/mits-680cpu.md.
//
// THE DIRECT ANALOG OF THE 88-CPU (mits-88cpu.h), one core down: it is a `6800`
// the way that card is an `8080`, carrying a Motorola 6800 instead of an Intel
// 8080. Everything the 88-CPU says applies here unchanged -- it is a Board AND a
// BusMaster, it decodes NOTHING (it originates cycles, it does not answer them),
// the crystal and the idle policy live on the card, and the core is a UNIT. The
// ONLY differences from Cpu8080Board are the core type and the strings.
//
// The 680b's I/O is MEMORY-MAPPED -- the onboard 6850 console lives at an address,
// not a port -- but that changes nothing here: this card decodes nothing either
// way, and the console ACIA is a SEPARATE board (`680io`, a later step). This card
// is the processor and the crystal, full stop.

#include "core/board.h"
#include "cpu/cpu.h"
#include "cpu/cpu6800.h"

#include <memory>

namespace altair {

class Cpu6800Board : public Board, public BusMaster, public CpuCard {
public:
    Cpu6800Board() : core_(std::make_unique<Cpu6800>()) {}

    std::string type() const override { return "6800"; }

    // ---- BusMaster: this card drives the bus ----
    StepResult step(Bus& bus) override { return core_->step(bus); }

    // ---- CpuCard ----
    CpuCore* activeCore() override { return core_.get(); }
    void      reportAchievedHz(long long hz) override { achievedHz_ = hz; }
    long long achievedHz() const override { return achievedHz_; }

    // ---- Board ---- (decodes() stays false, which is the truth for this card)
    void reset(Reset r) override { core_->reset(r); }
    void power() override {
        publishPolicy();
        core_->reset(Reset::PowerOn);
    }

    // A NEW CLOCK HAS NEVER HEARD OF THIS CARD'S CRYSTAL, so tell it (board.h, #34).
    void clockAttached() override { publishPolicy(); }

    std::vector<Property> properties() override;

    long long clockHz() const { return clockHz_; }

    std::vector<UnitDef> units() const override;

    // SNAPSHOT/RESTORE (DESIGN.md 13). The card's state is the core's; the crystal
    // and idle straps are config, re-published on attach.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

private:
    void publishPolicy();

    std::unique_ptr<Cpu6800> core_;

    // The same three policies as the 88-CPU, with the same defaults and for the
    // same reasons (mits-88cpu.h). FLAT OUT (0) IS THE DEFAULT, and it is the same
    // default the 8080 and Z80 cards carry: the real 680b ran its 6800 at 500 KHz
    // (a 2 MHz crystal divided by four), and you can have that back with one word --
    // `SET cpu0 clock_hz=500000`, or the same key in the machine file -- but it is
    // not what you get for free, because the EMULATED time is unchanged either way
    // (the 6850 still divides by the same crystal, the guest cannot tell) and the
    // only thing 500 KHz adds is the host sleep, for period feel. Defaulting the
    // three CPU cards alike is the point: a 6800 machine behaves like an 8080 one
    // unless you ask it not to.
    long long clockHz_ = 0;   // 0 = flat out, the default
    bool idle_ = true;        // stand down on an empty poll loop
    long long achievedHz_ = 0;
};

} // namespace altair
