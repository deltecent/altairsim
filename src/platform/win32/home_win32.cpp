#include "platform/home.h"

#include <cstdlib>

namespace altair::platform {

std::string homeDir() {
    // Windows has no HOME by convention -- USERPROFILE is the equivalent -- but honor
    // HOME first for the rare shell (MSYS, a dev's own setup) that does export it.
    if (const char* h = std::getenv("HOME"); h && *h) return std::string(h);
    if (const char* u = std::getenv("USERPROFILE"); u && *u) return std::string(u);
    return std::string();
}

}  // namespace altair::platform
