// macOS: a baud with no B-constant, set the Darwin way -- IOSSIOSPEED, which takes the
// literal integer speed. See serial_custombaud.h.
//
// The common Altair/S-100 rate 76800 HAS a B-constant on BSD (B76800) and is handled by
// the standard path in serial_posix.cpp; it never reaches here. This is for the rates
// with no constant at all, so macOS is no more limited than Linux.

#include "platform/posix/serial_custombaud.h"

#include <IOKit/serial/ioss.h>   // IOSSIOSPEED -- header-only, no IOKit link needed
#include <sys/ioctl.h>           // ioctl
#include <termios.h>             // speed_t

#include <cerrno>
#include <cstring>

namespace altair::platform {

bool setCustomBaud(int fd, long long baud, std::string& err) {
    speed_t s = (speed_t)baud;
    if (ioctl(fd, IOSSIOSPEED, &s) == 0) return true;
    err = "the host serial driver rejected " + std::to_string(baud) +
          " baud: " + std::strerror(errno);
    return false;
}

} // namespace altair::platform
