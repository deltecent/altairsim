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

}  // namespace altair::platform
