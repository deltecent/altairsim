#include "platform/home.h"

#include <cstdlib>

namespace altair::platform {

std::string homeDir() {
    const char* h = std::getenv("HOME");  // POSIX: the home directory lives in $HOME
    return (h && *h) ? std::string(h) : std::string();
}

}  // namespace altair::platform
