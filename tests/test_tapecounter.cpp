// The live tape counter's line state machine (cli/tape_counter_line.h).
//
// This is the host-side `tape: 01:44 / 01:47 (98%)` readout, reduced to the one
// decision the run loop makes each window: given the deck's activityLabel and
// whether the console has been quiet, what do we do with our \r-status line. The
// run loop that drives it is tty-only and not exercised by the piped test suite,
// so the DECISION lives here where it can be checked without a terminal.
//
// The load-bearing case is issue #165: a loader parks the head SHORT of the end of
// the tape, so the deck keeps offering a label after the program has already taken
// over the console -- and the counter must not resurrect its stale percentage over
// the running program's prompt.

#include "cli/tape_counter_line.h"
#include "test.h"

using Act = TapeCounterLine::Act;

void test_tapecounter() {
    SECTION("tape counter -- paints a live load on a quiet console");
    {
        TapeCounterLine c;
        CHECK(c.update("tape: 00:01 / 01:47 (1%)", true) == Act::Paint,
              "a live label with a quiet console paints the line");
        CHECK(c.shown, "the line is now on the terminal");

        // The sampler fires several times a second; the counter reads a new second
        // about once a second. Same text -> no re-flush.
        CHECK(c.update("tape: 00:01 / 01:47 (1%)", true) == Act::None,
              "the same text again does not re-flush");
        CHECK(c.update("tape: 00:02 / 01:47 (1%)", true) == Act::Paint,
              "a new second repaints");
    }

    SECTION("tape counter -- a finished load that reaches the end takes its line back");
    {
        TapeCounterLine c;
        c.update("tape: 01:46 / 01:47 (99%)", true);   // painted, shown
        // The head reaches the physical end: the deck stops offering a label. Console
        // still quiet (a bare-Altair cassette that printed nothing).
        CHECK(c.update("", true) == Act::WipeClean, "end of tape, quiet console: wipe cleanly");
        CHECK(!c.shown, "the line is gone");
        CHECK(c.update("", true) == Act::None, "and stays gone");
    }

    SECTION("tape counter -- issue #165: the counter never resurrects over the program");
    {
        TapeCounterLine c;
        // A loader that parks short of the end: the label is live at ~98% and STAYS
        // live because the head never reaches the tape's trailing tone.
        const std::string parked = "tape: 01:44 / 01:47 (98%)";
        CHECK(c.update(parked, true) == Act::Paint, "the load paints while the console is quiet");

        // The loaded program starts up and prints its first prompt: the console is no
        // longer quiet. We yield the line WITHOUT wiping -- the program's output is
        // already there.
        CHECK(c.update(parked, false) == Act::Abandon, "the guest speaks: abandon the line, do not wipe");
        CHECK(!c.shown, "our line is no longer claimed");

        // Now the guest sits at its prompt, going quiet between keystrokes -- while the
        // deck STILL offers its parked 98% label. This is the bug: the pre-fix loop
        // repainted the stale counter right over the `?`. It must not.
        CHECK(c.update(parked, true) == Act::None, "quiet again, still parked: the counter stays retired");
        CHECK(c.update(parked, true) == Act::None, "and stays retired however long the guest sits there");
        CHECK(c.update(parked, false) == Act::None, "a keystroke echoes: still nothing painted");
        CHECK(!c.shown, "the line is never reclaimed for this load");
    }

    SECTION("tape counter -- a genuinely new load re-arms the counter");
    {
        TapeCounterLine c;
        const std::string first = "tape: 01:44 / 01:47 (98%)";
        c.update(first, true);          // paint
        c.update(first, false);         // guest speaks -> retired
        CHECK(c.update(first, true) == Act::None, "retired for the first load");

        // The operator UNMOUNTs (or the tape hits a stop mark / its end): the deck stops
        // offering a label. That is the boundary that re-arms the counter.
        CHECK(c.update("", true) == Act::None, "no label, nothing was shown: nothing to do");

        // A fresh MOUNT + RUN: a new label appears on a quiet console -> paint again.
        CHECK(c.update("tape: 00:01 / 01:04 (1%)", true) == Act::Paint,
              "a new load after the deck went idle paints a fresh counter");
    }

    SECTION("tape counter -- a stop mark between files re-arms without an unmount");
    {
        TapeCounterLine c;
        c.update("tape: 00:30 / 02:00 (25%)", true);   // file 1 loading
        c.update("tape: 00:30 / 02:00 (25%)", false);  // file 1 done, program 1 talks -> line abandoned, retired
        // At the stop mark the label clears. The line was already given up when the
        // program spoke, so there is nothing to wipe -- but the empty label re-arms us.
        CHECK(c.update("", true) == Act::None, "at the stop mark: nothing to wipe, but the latch re-arms");
        // The guest advances past the stop mark and reads file 2: a new live label.
        CHECK(c.update("tape: 01:00 / 02:00 (50%)", true) == Act::Paint,
              "file 2 begins loading: the counter is back");
    }

    SECTION("tape counter -- the guest owning stdout from the start keeps its output");
    {
        TapeCounterLine c;
        // A Sol paints its own video window / a guest that prints throughout a load: the
        // console is never quiet while the label is live. We never claim the line.
        CHECK(c.update("tape: 00:05 / 01:00 (8%)", false) == Act::None, "busy console: nothing painted");
        CHECK(!c.shown, "the line was never taken");
        CHECK(c.update("tape: 00:06 / 01:00 (10%)", false) == Act::None, "and stays hands-off");
    }

    SECTION("tape counter -- no tape, no counter: an empty label does nothing");
    {
        TapeCounterLine c;
        CHECK(c.update("", true) == Act::None, "counter off / no deck: nothing to paint");
        CHECK(c.update("", false) == Act::None, "and a talking guest changes nothing");
        CHECK(!c.shown, "the line is never claimed");
    }
}
