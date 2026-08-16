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

    SECTION("tape counter -- the head at the end HOLDS its 100% frame");
    {
        TapeCounterLine c;
        // A flat-out bare-Altair load: the deck offers its final frame straight away (the
        // load was over before a paint window could catch it climbing), and the console is
        // quiet. The counter paints that 100% frame and then rests on it -- it does NOT wipe
        // the moment the head is at the end, because that frame is the "tape is done" cue.
        const char* done = "tape: 01:47 / 01:47 (100%)";
        CHECK(c.update(done, true) == Act::Paint, "the finished frame paints on a quiet console");
        CHECK(c.shown, "the 100% line is up");
        CHECK(c.update(done, true) == Act::None, "and it rests there, unchanged, however long you look");
        CHECK(c.update(done, true) == Act::None, "still resting");
    }

    SECTION("tape counter -- rewind/unmount (the label goes empty) takes the line back");
    {
        TapeCounterLine c;
        c.update("tape: 01:47 / 01:47 (100%)", true);   // painted, resting on the finished frame
        // The label finally goes empty -- a REWIND back to the top, or an UNMOUNT. That, not
        // reaching the end, is the boundary that reclaims the line. Console still quiet.
        CHECK(c.update("", true) == Act::WipeClean, "label gone, quiet console: wipe cleanly");
        CHECK(!c.shown, "the line is gone");
        CHECK(c.update("", true) == Act::None, "and stays gone");
    }

    SECTION("tape counter -- a held 100% frame still never resurrects over the program");
    {
        // The end-of-tape frame lingers (the deck keeps offering it), but the guest that was
        // loaded now runs and prints. The line must abandon and retire exactly as a parked
        // mid-load frame does -- otherwise `RUN 0` after a flat-out load would repaint the
        // tape counter over BASIC's first prompt.
        TapeCounterLine c;
        const char* done = "tape: 01:47 / 01:47 (100%)";
        CHECK(c.update(done, true) == Act::Paint, "the finished frame is up");
        CHECK(c.update(done, false) == Act::Abandon, "the guest speaks over it: abandon, do not wipe");
        CHECK(c.update(done, true) == Act::None, "quiet again, frame still offered: stays retired");
        CHECK(c.update(done, true) == Act::None, "and stays retired however long the guest sits there");
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

    SECTION("tape counter -- issue #270: a load that BEGINS on a busy console still paints");
    {
        TapeCounterLine c;
        // The overlay case: EDT/AM2 are started by TYPING at the monitor's `?` prompt, so
        // the command echo lands in the very window the tape read goes live. The console is
        // busy the instant the load begins -- but the counter has shown NOTHING yet, so it
        // must not pre-emptively retire the way an already-painted counter does over a prompt.
        const std::string live = "tape: 00:01 / 01:04 (1%)";
        CHECK(c.update(live, false) == Act::None, "load begins while the echo is still on the line: nothing yet");
        CHECK(!c.retired, "but the never-painted counter is NOT retired");

        // The echo clears and the load reads on: the counter paints as soon as it can.
        CHECK(c.update(live, true) == Act::Paint, "console quiets mid-load: the counter finally paints");
        CHECK(c.shown, "the line is up for the overlay load");
        CHECK(c.update("tape: 00:02 / 01:04 (3%)", true) == Act::Paint, "and it steps as the tape reads");

        // And the #165 guard still holds: once it HAS painted, a talking guest retires it.
        CHECK(c.update("tape: 00:02 / 01:04 (3%)", false) == Act::Abandon, "the loaded program speaks: abandon");
        CHECK(c.update("tape: 00:02 / 01:04 (3%)", true) == Act::None, "quiet again: stays retired, no resurrection");
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
