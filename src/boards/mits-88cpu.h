#pragma once
//
// The MITS 88-CPU -- docs/boards/mits-88cpu.md.
//
// THE CARD, NOT THE CHIP. In a real Altair the processor IS a card, and people
// pulled it out and put a Z80 card in its place. So it lives in boards/ with
// everything else and gets properties(), both resets, a line in BOARDS, and
// no special cases anywhere (DESIGN.md 3).
//
// It is a Board AND a BusMaster, and that pair of words is the whole design:
//
//     Boards RESPOND to bus cycles.  A bus master ORIGINATES them.
//
// So this card decodes NOTHING. It answers no address and no port -- it is not
// being coy, it genuinely has nothing on it that a bus cycle can reach. Ask it
// `WHO 0100` and it will tell you truthfully that it is not there.
//
// The payoff is DMA, and it is already paid for: a disk controller that steals
// the bus is a Board that BECOMES a BusMaster and drives the same cycles through
// the same interface. Nothing in the bus needs to learn about it.

#include "core/board.h"
#include "cpu/cpu.h"
#include "cpu/cpu8080.h"

#include <memory>

namespace altair {

class Cpu8080Board : public Board, public BusMaster, public CpuCard {
public:
    Cpu8080Board() : core_(std::make_unique<Cpu8080>()) {}

    std::string type() const override { return "8080"; }

    // ---- BusMaster: this is the only board that drives the bus ----
    StepResult step(Bus& bus) override { return core_->step(bus); }

    // ---- CpuCard ----
    CpuCore* activeCore() override { return core_.get(); }

    // The achieved crystal (cpu/cpu.h). The run loop hands us what it really
    // retired; we hold it for SHOW. Just a store and a load -- the measurement,
    // and every call to the host clock it takes, belongs to the run loop.
    void      reportAchievedHz(long long hz) override { achievedHz_ = hz; }
    long long achievedHz() const override { return achievedHz_; }

    // ---- Board ----
    // decodes() is not overridden. THAT IS THE POINT: the default is false, and
    // false is the truth for this card.

    void reset(Reset r) override { core_->reset(r); }
    void power() override {
        publishPolicy();  // the crystal and the idle policy, announced to the Clock
        core_->reset(Reset::PowerOn);
    }

    std::vector<Property> properties() override;

    // THE CLOCK IS THE CARD'S, not the machine's -- because that is where the
    // crystal physically is, and because a backplane with no CPU card in it (which
    // is what milestone 1a ran) has no clock rate to speak of.
    long long clockHz() const { return clockHz_; }

    // The core is a UNIT (DESIGN.md 3.0.1). A plain 88-CPU has exactly one, and a
    // card carrying an 8080 and an 8085 would list two with one active. It is
    // neither mountable nor connectable -- it is soldered on -- and `MOUNT
    // cpu0:8080 foo.dsk` gets told so in a sentence instead of half-working.
    std::vector<UnitDef> units() const override;

private:
    void publishPolicy();

    std::unique_ptr<Cpu8080> core_;

    // 0 = FLAT OUT, AND IT IS THE DEFAULT (Patrick, 2026-07-13).
    //
    // The real card ran at 2 MHz and you can have that back with one word --
    // `SET cpu0 clock_hz=2000000`, or the same key in the machine file -- but it is
    // not what you get for free, because almost nobody wants it. What a 2 MHz
    // default buys you is a 110-second wait for a cassette and a simulator that
    // feels broken. What it costs is nothing, because the EMULATED time is unchanged
    // either way: the ACR still spends 66,666 T-states on a 300-baud byte, the 6850
    // still divides by the same crystal, and the guest cannot tell (core/clock.h).
    // The only thing 2 MHz adds is the sleep, and the sleep is for period feel.
    long long clockHz_ = 0;

    // FLAT OUT STILL STANDS DOWN AT A PROMPT, AND THAT IS THE DEFAULT (Patrick,
    // 2026-07-13).
    //
    // `clock_hz = 0` says "go as fast as the host can". It never said "burn a core on an
    // empty poll loop", but that is what it did: CP/M at `A0>` sits in a three-instruction
    // CONIN spin, and the run loop ran it forty million times a second to no purpose at
    // all. So when the guest's ONLY activity in a slice is asking an empty keyboard for a
    // byte, the loop naps -- and the moment a key arrives, or the guest does anything
    // else, it is flat out again inside one slice.
    //
    // It is ORTHOGONAL to clock_hz: a 2 MHz machine idles too, and either can be turned
    // off without the other. `SET cpu0 idle=off` gets the spin back, which is what you
    // want if you are measuring the host, and nothing else.
    //
    // No hardware behaves differently -- this is the HOST sleeping, exactly as the
    // throttle is (core/clock.h). The guest cannot tell.
    bool idle_ = true;

    // THE CRYSTAL WE ACTUALLY TURNED, T-states per real second, as last measured by
    // the run loop (cpu/cpu.h, reportAchievedHz). Read-only out through `achieved_hz`;
    // 0 until the machine has run. Host-measured and so NOT on the Clock -- the run
    // loop times itself and tells us; we never ask the host the time ourselves.
    long long achievedHz_ = 0;
};

} // namespace altair
