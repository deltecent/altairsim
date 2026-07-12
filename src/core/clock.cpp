#include "core/clock.h"

#include <algorithm>

namespace altair {

Clock::Handle Clock::at(uint64_t when, std::function<void()> fn) {
    Handle h = ++next_;  // never 0, and never reused
    live_.emplace(h, std::move(fn));
    heap_.push_back(Item{when, h});
    std::push_heap(heap_.begin(), heap_.end(), std::greater<Item>());
    return h;
}

void Clock::cancel(Handle h) {
    // The heap entry stays as a tombstone. It is skipped when it surfaces, which
    // costs one hash lookup at a moment we were about to do one anyway -- cheaper
    // than a linear search of the heap right now, and this is called on every
    // character a UART sends.
    live_.erase(h);
}

bool Clock::pending(Handle h) const { return live_.find(h) != live_.end(); }

// One instruction retired. Run time forward to where the CPU left it, stopping at
// each thing that comes due along the way.
//
// TIME IS THE EVENT'S TIME WHILE THE EVENT RUNS. now() inside a callback reports
// WHEN THE THING ACTUALLY HAPPENED, not where the instruction that carried us past
// it happened to end. An instruction is up to 17 T-states long and a board's
// deadline lands wherever it lands inside that; a board that asks the time and is
// told the wrong one -- or that schedules `now() + charTime` and quietly drifts by
// up to 17 T-states per character -- is a bug that would take a very long time to
// see. The clock is the one thing in this simulator that must never lie about the
// time.
//
// A callback MAY schedule more work -- a UART re-arming for the next character does
// exactly that -- and if it schedules something already due, that fires in this same
// drain. Which is correct, and is also precisely how you would write an infinite
// loop: a board that re-arms at now() every time never lets go. So boards schedule
// STRICTLY IN THE FUTURE, and it is the board's job to guarantee it (see
// Acia::nextEdge, which is written to).
void Clock::advance(uint64_t dt) {
    const uint64_t target = t_ + dt;

    while (!heap_.empty() && heap_.front().when <= target) {
        Item top = heap_.front();
        std::pop_heap(heap_.begin(), heap_.end(), std::greater<Item>());
        heap_.pop_back();

        auto it = live_.find(top.h);
        if (it == live_.end()) continue;  // cancelled; the handle outlived the job

        // Move the function out and erase BEFORE calling it. The callback is
        // entitled to schedule, cancel, or destroy things -- including, in the
        // ordinary case, to re-arm itself -- and it must not find its own corpse
        // still in the table when it does.
        auto fn = std::move(it->second);
        live_.erase(it);

        if (top.when > t_) t_ = top.when;  // ...and an overdue event does not rewind it
        fn();
    }

    t_ = target;  // and the instruction ends where the CPU said it ended
}

void Clock::power() {
    t_ = 0;
    heap_.clear();
    live_.clear();
    // next_ is NOT reset. See the header: a board still holding a handle from the
    // machine's last life must not be able to cancel an event in this one.
}

} // namespace altair
