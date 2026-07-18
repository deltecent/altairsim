#pragma once
//
// A generic Z80 CPU card -- docs/boards/mits-z80cpu.md.
//
// THE DIRECT ANALOG OF THE 88-CPU (mits-88cpu.h), and deliberately generic: it is
// a `z80` the way that card is an `8080`, with no specific-product claim. The sim
// models no card-specific Z80 features (power-on-jump ROM, 4 MHz, an MMU) -- a
// real named board that had them would be a separate, sourced effort -- so calling
// it `z80` avoids inventing fidelity we cannot source ([[altairsim-no-invented-
// hardware]]).
//
// Everything the 88-CPU says applies here unchanged: it is a Board AND a
// BusMaster, it decodes NOTHING (it originates cycles, it does not answer them),
// the crystal and the idle policy live on the card, and the core is a UNIT. The
// ONLY differences from Cpu8080Board are the core type and the strings.

#include "core/board.h"
#include "cpu/cpu.h"
#include "cpu/cpuZ80.h"

#include <memory>

namespace altair {

class CpuZ80Board : public Board, public BusMaster, public CpuCard {
public:
    CpuZ80Board() : core_(std::make_unique<CpuZ80>()) {}

    std::string type() const override { return "z80"; }

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

private:
    void publishPolicy();

    std::unique_ptr<CpuZ80> core_;

    // The same three policies as the 88-CPU, with the same defaults and for the
    // same reasons (mits-88cpu.h): flat out, idle-at-a-prompt, and a read-only
    // achieved-crystal companion. There is no shared base -- the two cards are a
    // dozen lines each -- so these are copied, which the plan calls out explicitly.
    long long clockHz_ = 0;   // 0 = flat out, the default
    bool idle_ = true;        // stand down on an empty poll loop
    long long achievedHz_ = 0;
};

} // namespace altair
