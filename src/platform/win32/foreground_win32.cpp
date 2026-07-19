// The Windows half of platform/foreground.h.
//
// Windows has no application-level activation policy to restore: a window shown with
// SDL's SDL_WINDOW_ACTIVATE_WHEN_SHOWN hint set to "0" comes up without taking focus,
// and clicking it activates it, which is the behavior we are after. Nothing to do.

#include "platform/foreground.h"

namespace altair::platform {

void allowForegroundActivation() {}

}  // namespace altair::platform
