#pragma once

// Whether this process may come to the FOREGROUND, and when (DESIGN.md 2.1: the
// interface carries no OS type, and there is one implementation file per OS).
//
// This exists for one reason. A video board opens its window on the first frame, and
// on macOS a process that opens a window is brought to the front as it launches --
// which takes the keyboard away from whoever is typing at the altairsim> prompt or at
// a guest's console, mid-sentence. SDL suppresses that with SDL_MAC_BACKGROUND_APP,
// but that hint does two things and we only want one of them: it also skips setting
// the regular activation policy, leaving a process that has no Dock presence and that
// macOS will not activate when you click its window. Measured 2026-07-19: the terminal
// keeps focus, and the video window cannot be typed into at all -- it even draws its
// close/minimize/zoom buttons as though it were key, because the window IS key inside
// an application that is not active.
//
// That is the same defect as SDL_WINDOW_NOT_FOCUSABLE, which the video window must
// never have: the key sink feeds the Console, and the Sol-20's keyboard has no other
// home. A window you cannot click into cannot be typed into.
//
// So the two halves are separated: SDL is told not to activate on launch, and then the
// activation policy is put back by hand, which is the call SDL does not wrap.

namespace altair::platform {

// Make this process ELIGIBLE for the foreground -- able to be activated when the user
// clicks one of its windows -- WITHOUT bringing it to the foreground now.
//
// Call it after the video backend has initialised and before the window is shown. It
// does nothing on hosts that have no such concept, which is every host but macOS.
void allowForegroundActivation();

// GIVE THE FOREGROUND BACK: stop being the active application, without closing,
// hiding or moving any window we own.
//
// The way OUT, and it exists because the way in above is only half the story. The
// video window is a real input device, so clicking it makes altairsim the active
// application -- correctly. But the guest then stops (the close box, a breakpoint,
// HLT) and the monitor prints its prompt into a terminal that no longer has the
// keyboard, behind a window that is still open because closing the window was never
// what stopping the guest meant (host/display.h). Typing at that prompt goes into the
// video window instead. Reported on macOS, 2026-07-19.
//
// A no-op unless we are actually active: if the operator is already typing in the
// terminal -- a scripted run, a machine with no window, a window nobody has clicked --
// there is no foreground of ours to give up, and this must not disturb whoever holds it.
//
// Does nothing on hosts with no application-wide activation, which is every host but
// macOS -- the same asymmetry, and for the same reason, as the call above.
void yieldForeground();

}  // namespace altair::platform
