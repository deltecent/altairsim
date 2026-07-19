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

namespace altair::platform {

void allowForegroundActivation() {
    @autoreleasepool {
        // sharedApplication first: it is idempotent, and it means this is still correct
        // if it is ever called before the video backend has made an NSApp.
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    }
}

}  // namespace altair::platform
