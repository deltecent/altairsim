// The Clock, and its event queue (DESIGN.md 7.5).
//
// The queue exists for ONE reason and it is worth stating before the tests: a
// board must be able to act at a moment when NOBODY IS ASKING IT ANYTHING. A 6850
// whose transmit register drains while the guest sits in a HLT waiting for exactly
// that interrupt is the case, and if the only thing that could wake the card is
// the CPU that is halted waiting for it, the machine is dead.
//
// So what is tested here is not "a callback fires". It is the three properties a
// board's correctness rests on:
//
//   1. ORDER IS TOTAL AND DETERMINISTIC. Same schedule -> same firing order, on
//      every host, in every replay. Ties break by scheduling order, not by address
//      and not by luck.
//   2. A CANCELLED EVENT NEVER FIRES, and cancelling a dead handle is legal --
//      boards re-arm on every character and cannot be asked to track which.
//   3. NOTHING FIRES EARLY, and everything due inside one instruction fires at
//      that instruction's boundary. Emulated time is the only clock there is.

#include "core/clock.h"
#include "test.h"

#include <string>
#include <vector>

using namespace altair;

void test_clock() {
    SECTION("Clock -- time, and the queue of things that will happen in it");

    {
        Clock c;
        CHECK(c.now() == 0, "time starts at zero");
        c.advance(7);
        CHECK(c.now() == 7, "and advances by exactly what the CPU said it took");
        CHECK(c.queued() == 0, "nothing scheduled, nothing queued");
    }

    // ---- Nothing fires early; everything due fires at the boundary ----
    {
        Clock c;
        std::string log;
        c.at(100, [&] { log += "a"; });

        c.advance(99);
        CHECK(log.empty(), "99 < 100: it has not happened yet, and must not");
        CHECK(c.queued() == 1, "still waiting");

        c.advance(1);
        CHECK(log == "a", "and at exactly 100 it fires");
        CHECK(c.queued() == 0, "a fired event is gone -- the queue does not leak");
    }

    // A single instruction can be 17 T-states long, so a deadline INSIDE it fires
    // at the boundary. That is not a compromise: the 8080 samples INT at an
    // instruction boundary too, so there is no finer grain to be observed at.
    {
        Clock c;
        std::string log;
        c.at(5, [&] { log += "x"; });
        c.advance(17);
        CHECK(log == "x", "a deadline passed mid-instruction fires at the boundary");
        CHECK(c.now() == 17, "and time is where the CPU left it, not where the event was");
    }

    // ---- Order is total, and ties break by SCHEDULING order ----
    //
    // THIS IS A REPLAY PROPERTY, not a nicety. Two boards with deadlines on the
    // same T-state must fire in the same order every single time or a recorded
    // session diverges from its replay -- once a month, unreproducibly, for reasons
    // nobody will ever find. A heap without an explicit tiebreak does not give you
    // this, and the tiebreak is the handle: a monotone counter.
    {
        Clock c;
        std::string log;
        c.at(50, [&] { log += "3"; });
        c.at(10, [&] { log += "1"; });
        c.at(50, [&] { log += "4"; });  // same T-state as "3", scheduled AFTER it
        c.at(20, [&] { log += "2"; });

        c.advance(100);
        CHECK(log == "1234", "time order first, then the order they were scheduled in");
    }

    // ---- Cancel ----
    {
        Clock c;
        std::string log;
        Clock::Handle h = c.at(10, [&] { log += "boom"; });
        CHECK(c.pending(h), "it is on the books");

        c.cancel(h);
        CHECK(!c.pending(h), "and then it is not");

        c.advance(100);
        CHECK(log.empty(), "a cancelled event does not fire");

        // Boards re-arm on every character and cannot be expected to track which of
        // their handles are still live. All three of these are no-ops, on purpose.
        c.cancel(h);              // already cancelled
        c.cancel(Clock::kNone);   // never existed
        c.cancel(999999);         // never issued
        CHECK(true, "cancelling a dead, absent or never-issued handle is legal");
    }

    // Cancelling one of several leaves the others exactly where they were.
    {
        Clock c;
        std::string log;
        c.at(10, [&] { log += "a"; });
        Clock::Handle h = c.at(20, [&] { log += "b"; });
        c.at(30, [&] { log += "c"; });
        c.cancel(h);
        c.advance(100);
        CHECK(log == "ac", "the cancelled one is gone; its neighbours are untouched");
    }

    // ---- INSIDE A CALLBACK, now() IS WHEN THE THING HAPPENED ----
    //
    // Not where the instruction ended. An instruction is up to 17 T-states long and
    // a deadline lands wherever it lands inside that, so a board told "it is now
    // 1000" when its event was due at 10 would be told a lie -- and a board that
    // re-arms with `now() + charTime` would drift a little further with every
    // character it ever sent. This is the assertion that stops that.
    {
        Clock c;
        std::vector<uint64_t> seen;
        c.at(10, [&] { seen.push_back(c.now()); });
        c.at(20, [&] { seen.push_back(c.now()); });

        c.advance(1000);  // ONE long instruction, straight over both of them
        CHECK(seen.size() == 2, "both fired");
        CHECK(seen[0] == 10 && seen[1] == 20, "and each was told the time IT was due, not 1000");
        CHECK(c.now() == 1000, "...while the instruction still ends where the CPU said it did");
    }

    // ---- A callback may schedule. This is the UART's whole life. ----
    //
    // The 2SIO re-arms itself from inside its own timer -- character after character
    // -- and if that leaked a heap entry per character, a machine left running
    // overnight would end up with a queue of millions of dead events. This is also
    // the test that would catch now() lying: `now() + 10` has to mean ten T-states
    // after the event, or the chain fires once and stops.
    {
        Clock c;
        int fired = 0;
        std::function<void()> tick = [&] {
            if (++fired < 5) c.at(c.now() + 10, tick);
        };
        c.at(10, tick);

        c.advance(1000);
        CHECK(fired == 5, "a callback can re-arm itself, and the chain runs to its end");
        CHECK(c.queued() == 0, "and the queue is EMPTY -- re-arming does not leak");
    }

    // An event scheduled for a time ALREADY PAST fires in the same drain, not never.
    // A board should not schedule one (Mc6850::nextEdge is written to guarantee it --
    // it is how you write an infinite loop), but if a board does, it must not
    // silently vanish. Losing an interrupt is worse than being late with one.
    {
        Clock c;
        std::string log;
        c.advance(100);
        c.at(50, [&] { log += "late"; });
        CHECK(log.empty(), "scheduling alone does not fire it");
        c.advance(0);
        CHECK(log == "late", "an overdue event fires at the next advance, not never");
    }

    // ---- Power ----
    {
        Clock c;
        std::string log;
        c.advance(500);
        Clock::Handle h = c.at(600, [&] { log += "ghost"; });

        c.power();
        CHECK(c.now() == 0, "power restarts time");
        CHECK(c.queued() == 0, "and the machine that scheduled those deadlines is gone");

        c.advance(10000);
        CHECK(log.empty(), "nothing survives a power cycle");

        // THE HANDLE COLLISION THAT ISN'T. A board still holding a handle from the
        // machine's last life must not be able to cancel an innocent event in this
        // one by number. Handles keep counting up across power; they are never
        // reused, so this cancel hits nothing at all.
        Clock::Handle fresh = c.at(20, [&] { log += "real"; });
        CHECK(fresh != h, "a fresh handle cannot collide with one from before power");
        c.cancel(h);
        c.advance(100);
        CHECK(log == "real", "the stale handle cancelled nothing it should not have");
    }

    // ---- The crystal is on the CPU card ----
    {
        Clock c;
        CHECK(c.hz() == 2000000, "an 88-CPU's 2 MHz by default");
        CHECK(c.tStatesPer(9600) == 208, "2000000/9600 -- the ONE place that division lives");
        c.setHz(4000000);
        CHECK(c.tStatesPer(9600) == 416, "a 4 MHz card doubles it");
        c.setHz(0);
        CHECK(c.hz() == 2000000, "a machine with no crystal still gives a board a sane divisor");
    }
}
