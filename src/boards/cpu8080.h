#pragma once
//
// The MITS 88-CPU -- docs/boards/88-cpu.md.
//
// THE CARD, NOT THE CHIP. In a real Altair the processor IS a card, and people
// pulled it out and put a Z80 card in its place. So it lives in boards/ with
// everything else and gets properties(), both resets, a line in BOARD LIST, and
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

    // ---- Board ----
    // decodes() is not overridden. THAT IS THE POINT: the default is false, and
    // false is the truth for this card.

    void reset(Reset r) override { core_->reset(r); }
    void power() override { core_->reset(Reset::PowerOn); }

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
    std::unique_ptr<Cpu8080> core_;
    long long clockHz_ = 2000000;  // 2 MHz: the Altair's 88-CPU, as shipped
};

} // namespace altair
