// The non-macOS POSIX half of platform/foreground.h.
//
// X11 and Wayland have no application-wide activation to opt out of: focus is the
// window manager's business, and the window half of it is asked for with SDL's
// SDL_WINDOW_ACTIVATE_WHEN_SHOWN hint, which needs nothing from the OS layer. So there
// is nothing to do here, and that is the answer rather than a gap.

#include "platform/foreground.h"

namespace altair::platform {

void allowForegroundActivation() {}

}  // namespace altair::platform
