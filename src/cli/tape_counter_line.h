#pragma once
//
// The live tape counter's terminal line -- the little state machine behind the
// `tape: 01:44 / 01:47 (98%)` readout the run loop paints while a cassette loads.
//
// It is deliberately pure and tty-free: given only what the deck and the console
// did this window, it decides what to do with our one \r-status line. The run loop
// owns the terminal; this owns the decision, so the decision can be tested without
// a pty (test_cli.cpp).
//
// WHY IT NEEDS A MEMORY AT ALL -- the resurrection bug (issue #165). The counter is
// a host courtesy readout; it counts how far the tape HEAD has moved, and knows
// nothing of loaders. A real loader stops reading the instant it has the program's
// bytes -- SHORT of the tape's trailing tone -- so the head parks before the end and
// the deck keeps offering a label (it still looks mid-load). The moment the loaded
// program prints its first prompt we yield the line, as we always did. But the guest
// then sits at that prompt going quiet between keystrokes, and a naive "quiet console
// + a live label -> paint" would repaint the stale `(98%)` right over the `?`, again
// and again. So once the guest has SPOKEN during a load, that load's counter is done
// for good -- `retired` latches -- and it re-arms only when the deck stops offering a
// label: back at the top of the tape (p == 0 after a rewind or a fresh mount), a stop
// mark, or an unmount -- exactly the boundary at which a genuinely new load begins.
// (Reaching the END is NOT one of those: the deck keeps offering its final 100% frame
// there, so the finished readout stays on screen -- see AcrBoard::activityLabel.)

#include <string>

// One line, one deck-load at a time. `shown`/`label` are the terminal state (is our
// line up, and what does it say); `retired` is the latch above. `update()` is called
// once per counter window with the board's activityLabel (empty when nothing is
// loading) and whether the console has been quiet since the previous window, and
// returns the single action the caller performs on the terminal.
struct TapeCounterLine {
    enum class Act {
        None,       // leave the terminal alone
        Paint,      // write '\r' + label + clear-to-EOL  (label holds the text)
        WipeClean,  // write '\r' + clear-to-EOL: take our line back, nothing under it
        Abandon,    // drop our line WITHOUT wiping -- the guest is printing there now
    };

    bool        shown   = false;
    std::string label;
    bool        retired = false;

    Act update(const std::string& activity, bool consoleQuiet) {
        const bool loadLive = !activity.empty();

        // No load in progress re-arms the latch: the next load gets a fresh counter.
        if (!loadLive) retired = false;

        Act act = Act::None;
        if (loadLive && consoleQuiet && !retired) {
            // A live load, a quiet console, not yet retired: this is the line to paint.
            // Only actually write when the text changed (the sampler fires several times
            // a second; a new second lands about once a second).
            if (label != activity || !shown) {
                label = activity;
                shown = true;
                act   = Act::Paint;
            }
        } else if (shown && consoleQuiet) {
            // Our line is up and the console is quiet, but we are no longer painting --
            // the load ended, or its counter has retired. Take the line back cleanly.
            shown = false;
            label.clear();
            act   = Act::WipeClean;
        } else if (shown) {
            // The guest is talking now. Drop our line but do NOT wipe: its output is
            // already on the terminal where our line was, and a wipe would eat it.
            shown = false;
            label.clear();
            act   = Act::Abandon;
        }

        // Once the guest speaks mid-load, that load's counter never comes back. Set
        // this AFTER the decision above so the window in which it first speaks still
        // yields the line via Abandon rather than being suppressed silently.
        if (loadLive && !consoleQuiet) retired = true;

        return act;
    }
};
