#pragma once
//
// Clock -- the single source of emulated time, and the queue of things that are
// going to happen in it (DESIGN.md 7.5).
//
// Time is measured in T-STATES, never in milliseconds, and it advances only
// when the CPU retires an instruction. Nothing in the simulator may ask the host
// what time it is; if a board could, replay would be dead the first time one did.
//
// The crystal is on the CPU CARD (DESIGN.md 3, 8) -- there is no machine clock
// rate. The card publishes its `clock_hz` here, which is how a board that needs
// to convert real units (a baud rate, a disk RPM) into T-states finds the
// conversion factor without knowing which card holds the crystal, or whether
// there is one at all.
//
// ---------------------------------------------------------------------------
// THE EVENT QUEUE IS BACK, AND THE BOARD CITED AS PROVING IT UNNECESSARY IS THE
// BOARD THAT PROVED IT NECESSARY (Patrick, 2026-07-12).
//
// A previous version of this file deleted `schedule()` from the design and
// argued the point at length, right here. The argument was: a board is already
// POLLED for everything the bus can observe about it -- `decodes()` on every
// cycle, `assertsInt()` on every instruction boundary -- so a board never needs
// to be WOKEN. It only needs to answer "what time is it?" when someone asks.
//
// THAT ARGUMENT WAS CIRCULAR, and the circle was hiding the bug. The board did
// not need waking BECAUSE WE WERE POLLING IT SIXTY MILLION TIMES A SECOND. The
// poll was not evidence that the queue was unnecessary; the poll WAS the queue,
// run at enormous cost and called something else.
//
// And the poll had to go, because it was never how the machine worked: a bus
// does not interrogate a card for its interrupt status. A card PULLS pin 73 and
// HOLDS it, and the CPU reads the wire (Patrick, 2026-07-12). Take the poll away
// and the board is left holding a deadline it has no way to be present for:
//
//   A 6850 with the transmit interrupt jumpered raises IRQ when its shift
//   register drains. Nobody touches it. No bus cycle happens. And the guest is
//   sitting in a HLT waiting for precisely that interrupt. If the only way the
//   card can act is to BE ASKED, and the only thing that would ask is the CPU
//   that is halted waiting for it, then nothing ever happens again.
//
// So a deadline needs a way to arrive on its own. That is this queue.
//
// What was RIGHT in the old argument, and still is: TDRE is a DEADLINE, not an
// event. A board that can answer "what time is it?" when the guest finally reads
// the status port should do exactly that and schedule nothing -- and the 6850
// still does, for the polled case. You come here only for the state changes that
// must be VISIBLE TO SOMEONE ELSE the instant they happen, and on this bus there
// is exactly one such thing: a wire.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace altair {

class StateWriter;  // core/statefile.h -- SNAPSHOT/RESTORE
class StateReader;

class Clock {
public:
    // A scheduled thing, cancellable. AN INTEGER AND NOT A POINTER, on purpose:
    // a handle that outlives its event is then merely stale, and cancelling it is
    // a no-op instead of a use-after-free. Handles are never reused.
    using Handle = uint64_t;
    static constexpr Handle kNone = 0;

    // T-states since power was applied. Monotonic; only POWER resets it.
    uint64_t now() const { return t_; }

    // ONE INSTRUCTION RETIRED. Called by the run loop with StepResult::tStates,
    // and by nobody else -- which is what makes emulated time a pure function of
    // the instruction stream.
    //
    // Anything that came due during those T-states fires HERE, in time order --
    // and WITH now() SET TO THE MOMENT IT WAS DUE, not to the end of the
    // instruction. An instruction is up to 17 T-states long, and a board that asked
    // the time inside its own callback and was told the wrong one would drift a
    // little further with every character it sent. The clock is the one thing here
    // that must never lie about the time.
    //
    // The BUS, on the other hand, only observes a board at the instruction boundary
    // -- which is not a compromise either: the 8080 samples INT at an instruction
    // boundary too, so there is no finer grain to be seen at.
    void advance(uint64_t dt);

    // "CALL ME AT T." For the state changes a board cannot otherwise be present
    // for -- see the header comment. `at` takes an absolute T-state; `after` a
    // delta from now.
    Handle at(uint64_t when, std::function<void()> fn);
    Handle after(uint64_t dt, std::function<void()> fn) { return at(t_ + dt, std::move(fn)); }

    // Cancelling kNone, or a handle that has already fired, is legal and does
    // nothing. Boards re-arm constantly and should not have to track which.
    void cancel(Handle h);
    bool pending(Handle h) const;

    // The crystal on the CPU card. Defaults to the 88-CPU's 2 MHz so that a
    // machine with no processor in it still gives a board a sane number to divide
    // by instead of a zero to divide by.
    //
    // hz() IS A DIVISOR. free() IS A POLICY. They are not the same question, and
    // fusing them into one number is how `clock_hz = 0` came to be documented as
    // "runs flat out" while quietly doing nothing at all: setHz(0) coerced hz_ back
    // to 2 MHz, the run loop paced against hz_, and the machine crawled.
    //
    //   hz()    what a board divides by to turn 9600 baud into T-states. NEVER 0 --
    //           a zero here is a division by zero in every UART in the backplane.
    //   free()  whether the run loop sleeps to keep emulated time in step with real
    //           time. THIS is what `clock_hz = 0` means, and the default (§10).
    //
    // So a free-running machine still counts a 300-baud cassette byte as 66,666
    // T-states -- the tape's TIMING is intact and the ACR is none the wiser. Those
    // T-states simply elapse as fast as the host can retire them, which is the whole
    // point: the tape is period-correct, the WAIT is not.
    long long hz() const { return hz_; }
    bool      free() const { return free_; }
    void      setHz(long long hz) {
        free_ = (hz <= 0);
        hz_   = hz > 0 ? hz : 2000000;
    }

    // THE SECOND POLICY, AND IT IS ORTHOGONAL TO THE FIRST (Patrick, 2026-07-13).
    //
    // free() says whether the run loop sleeps to KEEP TIME. idle() says whether it
    // sleeps when the guest HAS NOTHING TO DO -- when the only thing it is doing is
    // asking an empty keyboard for a byte, which is what every prompt in every guest
    // ever written spends its life doing. Flat out means "as fast as the host can go
    // WHEN THERE IS ANYTHING TO DO"; it never meant "burn a core on an empty poll
    // loop", and before this it did exactly that: CP/M at `A0>` pinned a CPU.
    //
    // It is a HOST policy, like free(). No hardware behaves differently, emulated time
    // is unchanged, and the guest cannot tell -- the 6850 still sets RDRF when a byte
    // lands, and the byte still lands. Only the host thread sleeps.
    //
    // On by default, and it lives here rather than in the run loop because this is
    // where the loop already comes to ask whether to sleep, and because the crystal it
    // sits beside is published by the same card (boards/mits-88cpu.h, `idle`).
    bool idle() const { return idle_; }
    void setIdle(bool on) { idle_ = on; }

    // Convert a rate in things-per-second into T-states-per-thing. THE ONE PLACE
    // that division is written: a baud rate, a disk RPM, and a UART's character
    // time are all the same arithmetic, and doing it in each board is how they
    // come to disagree.
    uint64_t tStatesPer(long long perSecond) const {
        if (perSecond <= 0) return 0;
        return (uint64_t)(hz_ / perSecond);
    }

    // Power APPLIED. Time restarts, and every pending deadline is gone: the
    // machine that scheduled them does not exist any more.
    //
    // Handles keep counting UP across a power cycle rather than restarting, so a
    // board still holding a handle from the last life cannot cancel some innocent
    // event in this one by number collision.
    void power();

    // How many events are live. For tests, and for the assertion that a board
    // which re-arms on every character is not LEAKING one per character.
    size_t queued() const { return live_.size(); }

    // SNAPSHOT/RESTORE (DESIGN.md 13). Only t_ and next_ travel: t_ is emulated
    // time itself, and next_ keeps handles from a restored board's re-arm from
    // colliding with a stale number. The QUEUE does NOT travel -- its entries are
    // std::function closures (see the header note), and each board re-arms its own
    // deadlines in deserialize() from the state it read. hz_/free_/idle_ are the
    // CPU card's to publish and are already correct in a matching machine, so they
    // are not written here. deserialize() EMPTIES the event queue (see its .cpp) so
    // the boards can re-arm their deadlines into a clean queue against the restored
    // time -- a live machine's stale deadlines would otherwise fire in the past.
    void serialize(StateWriter& w) const;
    void deserialize(StateReader& r);

private:
    // A binary min-heap keyed by (when, handle).
    //
    // THE HANDLE IS THE TIEBREAK, and it is a monotonically increasing counter, so
    // two events due at the same T-state fire IN THE ORDER THEY WERE SCHEDULED --
    // every time, on every host, in every replay. There is no other tiebreak
    // available here, and an unstable one would be a replay divergence that shows
    // up once a month and cannot be reproduced.
    struct Item {
        uint64_t when = 0;
        Handle   h    = 0;
        bool operator>(const Item& o) const { return when != o.when ? when > o.when : h > o.h; }
    };

    void drain();

    std::vector<Item> heap_;
    // The authority on what is still live. Cancelling erases from here and leaves
    // the heap entry behind as a tombstone -- it costs one hash lookup to skip
    // when it surfaces, which is cheaper than finding and removing it now.
    std::unordered_map<Handle, std::function<void()>> live_;

    Handle   next_ = 0;  // ++next_, so kNone (0) is never issued
    uint64_t t_    = 0;
    long long hz_  = 2000000;   // the DIVISOR. Never 0. See setHz().
    bool     free_ = true;      // ...and by default we do not pace against it.
    bool     idle_ = true;      // ...but we DO stand down when the guest is only waiting.
};

} // namespace altair
