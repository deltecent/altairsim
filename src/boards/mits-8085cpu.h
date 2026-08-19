#pragma once
//
// A generic 8085 CPU card -- the 88-CPU's twin, one core over (mits-88cpu.h).
//
// THE DIRECT ANALOG OF THE 88-CPU and the Z80 card (mits-z80cpu.h), and
// deliberately generic: it is an `8085` the way those are an `8080`/`z80`, with no
// specific-product claim. The sim models no card-specific 8085 features -- a real
// named board that had them would be a separate, sourced effort -- so calling it
// `8085` avoids inventing fidelity we cannot source ([[altairsim-no-invented-
// hardware]]).
//
// FOR NOW THIS CARD IS GATE PLUMBING. The CPU exercisers drive the machine's bus
// master (tests/cputest.cpp), so validating the 8085 core headless needs an 8085
// card to carry it. It is registered for that reason; the user-facing `8085`
// machine, its docs, and the `8085` disassembler are the follow-up, landing only
// after the exerciser is green (issue #233).
//
// Everything the 88-CPU says applies unchanged: it is a Board AND a BusMaster, it
// decodes NOTHING, the crystal and idle policy live on the card, and the core is a
// UNIT. The ONLY differences from Cpu8080Board are the core type and the strings.

#include "core/board.h"
#include "cpu/cpu.h"
#include "cpu/cpu8085.h"

#include <memory>

namespace altair {

class Cpu8085Board : public Board, public BusMaster, public CpuCard {
public:
    Cpu8085Board() : core_(std::make_unique<Cpu8085>()) {}

    std::string type() const override { return "8085"; }

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

    void clockAttached() override { publishPolicy(); }

    std::vector<Property> properties() override;

    long long clockHz() const { return clockHz_; }

    std::vector<UnitDef> units() const override;

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

private:
    void publishPolicy();

    std::unique_ptr<Cpu8085> core_;

    // The same three policies as the 88-CPU and Z80 cards, with the same defaults
    // and for the same reasons (mits-88cpu.h): flat out, idle-at-a-prompt, and a
    // read-only achieved-crystal companion. There is no shared base -- the cards are
    // a dozen lines each -- so these are copied, which the plan calls out.
    long long clockHz_ = 0;   // 0 = flat out, the default
    bool idle_ = true;        // stand down on an empty poll loop
    long long achievedHz_ = 0;
};

} // namespace altair
