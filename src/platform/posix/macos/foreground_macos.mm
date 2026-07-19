// The macOS half of platform/foreground.h.
//
// SDL was told not to bring us to the front on launch (SDL_MAC_BACKGROUND_APP), and
// that hint ALSO makes SDL skip the line that would otherwise have run:
//
//     [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
//
// (src/video/cocoa/SDL_cocoaevents.m, Cocoa_RegisterApp). Without it a bare executable
// is left in a policy that has no Dock tile and that the window server will not
// activate, so clicking the video window does nothing. Putting the policy back is the
// whole of this file: it restores click-to-focus without asking to be activated, which
// is the distinction SDL's one hint does not let us draw.
//
// Regular is set rather than restored-to-what-it-was on purpose: it is what SDL itself
// would have set, so this leaves the process exactly where the default path leaves it,
// minus the activation.

#import <AppKit/AppKit.h>

#include "platform/foreground.h"

#include <cstdio>
#include <cstdlib>

namespace altair::platform {

namespace {

// WHO HAD THE FOREGROUND BEFORE WE TOOK IT. Captured once, on the way in, and it is
// the terminal the operator launched us from -- see yieldForeground() for why naming
// it is necessary rather than merely tidy.
NSRunningApplication* g_priorApp = nil;

// One line to stderr per decision, off unless ALTAIRSIM_FOCUS_DEBUG is set. Focus is
// the one thing in this file no test can observe, so the alternative to this is
// guessing at a distance -- which is how the first version of yieldForeground() came
// to ship a call that did nothing on this machine.
void focusLog(const char* what) {
    static const bool on = std::getenv("ALTAIRSIM_FOCUS_DEBUG") != nullptr;
    if (on) std::fprintf(stderr, "[focus] %s\n", what);
}

}  // namespace

void allowForegroundActivation() {
    @autoreleasepool {
        // WHO IS IN FRONT RIGHT NOW, asked before we can possibly be the answer. This
        // runs before SDL_Init and before any window exists, so the frontmost
        // application is whoever launched us -- the terminal. Remembered here because
        // by the time we want to give the foreground back, we hold it, and the question
        // is no longer answerable.
        //
        // Retained deliberately: it must outlive this autorelease pool and the whole
        // run. If we were launched with nothing in front -- from a script, a launch
        // agent -- this is nil, and yieldForeground() falls back.
        if (!g_priorApp) {
            g_priorApp = [[[NSWorkspace sharedWorkspace] frontmostApplication] retain];
            focusLog(g_priorApp ? "remembered the application that launched us"
                                : "nothing was in front at startup");
        }

        // sharedApplication first: it is idempotent, and it means this is still correct
        // if it is ever called before the video backend has made an NSApp.
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    }
}

// The other direction: stop being the active application, without touching a window.
//
// -hide: would also return focus, and is the call people reach for first, but it takes
// the window away with it. Rejected: the machine is still powered when the guest stops,
// RUN resumes into the same screen, and a screen you cannot see is not a screen.
//
// -[NSApp deactivate] IS THE OBVIOUS CALL AND IT DOES NOT WORK. It was what this
// shipped first, and it was measured doing nothing at all on macOS Tahoe (2026-07-19):
// the guest stopped, the window stayed key, and the terminal never came forward.
// Modern macOS does not let an application drop the foreground into the void and
// expect the window server to find the next tenant -- activation is handed FROM one
// application TO another, and an app that merely resigns is liable to be handed
// straight back. Which is why the way in above bothers to remember who we took it from.
//
// So: yield to a named application. -yieldActivationToApplication: (macOS 14+) tells
// the window server that our successor's activation request is expected and should not
// be refused; -activate then makes it stick. Asked for by selector rather than guarded
// on a version, so an older system simply skips the first half -- where the plain
// activate was allowed anyway.
void yieldForeground() {
    @autoreleasepool {
        // No sharedApplication here, unlike above: if no NSApp has been made then no
        // window was ever opened, nothing of ours can be active, and there is nothing
        // to do. Making one just to ask would be building the thing to answer no.
        if (!NSApp || ![NSApp isActive]) {
            focusLog("not active -- nothing of ours to give up");
            return;
        }

        if (g_priorApp) {
            if ([NSApp respondsToSelector:@selector(yieldActivationToApplication:)])
                [NSApp performSelector:@selector(yieldActivationToApplication:)
                            withObject:g_priorApp];
            [g_priorApp activateWithOptions:0];
            focusLog("yielded to the application that launched us");
            return;
        }

        // Nobody to hand it to. Resigning is all that is left, and the comment above
        // says what that is worth -- but a call that usually fails still beats leaving
        // the keyboard pointed at a window the operator has finished with.
        [NSApp deactivate];
        focusLog("no prior application -- fell back to deactivate");
    }
}

}  // namespace altair::platform
