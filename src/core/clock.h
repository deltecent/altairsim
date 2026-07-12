#pragma once
//
// Clock -- the single source of emulated time (DESIGN.md 7.5).
//
// Time is measured in T-STATES, never in milliseconds, and it advances only
// when the CPU retires an instruction. Nothing in the simulator may ask the host
// what time it is; if a board could, replay would be dead the first time one
// did.
//
// The crystal is on the CPU CARD (DESIGN.md 3, 8) -- there is no machine clock
// rate. The card publishes its `clock_hz` here, which is how a board that needs
// to convert real units (a baud rate, a disk RPM) into T-states finds the
// conversion factor without knowing which card holds the crystal, or whether
// there is one at all.
//
// ---------------------------------------------------------------------------
// AMENDMENT TO DESIGN.md 7.5 -- FLAGGED FOR PATRICK, NOT SLIPPED IN
//
// The design specifies an EventQueue: `schedule(delta, fn)` / `cancel(h)`, with
// boards registering CALLBACKS for future work. This class implements `now()`
// and `advance()` and DOES NOT implement `schedule()`. That is deliberate, it is
// a simplification of the design, and it should be argued with rather than
// assumed correct.
//
// The reason: in this architecture a board is already POLLED for everything the
// bus can observe about it. `decodes()` is asked on every cycle; `assertsInt()`
// is asked on every instruction boundary. So a board never needs to be WOKEN --
// it needs to be able to answer "what time is it?" when someone finally asks.
//
// And that is what the hardware is actually like. When a 6850's transmit shift
// register drains, NOTHING HAPPENS in the world: no wire moves, nobody is
// notified. TDRE is simply true the next time the guest reads the status port.
// It is a DEADLINE, not an EVENT -- and modeling it as a callback would fire one
// per character even when nobody ever looks, which is both slower and less true.
// The disk's index pulse and sector timing are deadlines by the same argument,
// and an interrupt is a LEVEL that `assertsInt()` already polls.
//
// So `schedule()` has no user, and unused API is a liability -- it gets designed
// against a board that does not exist yet, and it is always wrong when the board
// arrives. If a real one turns up that genuinely cannot be expressed as a
// deadline, add it THEN, with that board as the proof it was needed.
// ---------------------------------------------------------------------------

#include <cstdint>

namespace altair {

class Clock {
public:
    // T-states since power was applied. Monotonic; only POWER resets it.
    uint64_t now() const { return t_; }

    // One instruction retired. Called by the run loop with StepResult::tStates,
    // and by nobody else -- which is what makes emulated time a pure function of
    // the instruction stream.
    void advance(uint64_t dt) { t_ += dt; }

    // The crystal on the CPU card. Defaults to the 88-CPU's 2 MHz so that a
    // machine with no processor in it still gives a board a sane number to divide
    // by instead of a zero to divide by.
    long long hz() const { return hz_; }
    void setHz(long long hz) { hz_ = hz > 0 ? hz : 2000000; }

    // Convert a rate in things-per-second into T-states-per-thing. THE ONE PLACE
    // that division is written: a baud rate, a disk RPM, and a UART's character
    // time are all the same arithmetic, and doing it in each board is how they
    // come to disagree.
    uint64_t tStatesPer(long long perSecond) const {
        if (perSecond <= 0) return 0;
        return (uint64_t)(hz_ / perSecond);
    }

    void power() { t_ = 0; }

private:
    uint64_t t_ = 0;
    long long hz_ = 2000000;
};

} // namespace altair
