// Linux: a baud with no B-constant -- 76800 among them, an S-100 staple glibc never
// named -- set through the kernel's BOTHER/termios2 ioctl, which takes the literal
// integer in c_ispeed/c_ospeed. See serial_custombaud.h.
//
// This is the ONE translation unit that speaks the kernel's <asm/termios.h> ABI. That
// header defines its OWN `struct termios`, incompatible with the one glibc's <termios.h>
// defines, and the two cannot coexist in a single translation unit. So this file
// includes neither <termios.h> nor <sys/ioctl.h> (which drags in the glibc one), and
// declares ioctl() by hand -- exactly the arrangement the kernel's own userspace
// documentation prescribes for setting a custom line speed.

#include "platform/posix/serial_custombaud.h"

#include <asm/termios.h>   // struct termios2, BOTHER, CBAUD, TCGETS2, TCSETS2

#include <cerrno>
#include <cstring>

// The glibc prototype lives in <sys/ioctl.h>, which we must not include here. C linkage,
// so this resolves to the same symbol regardless of the exact prototype.
extern "C" int ioctl(int fd, unsigned long request, ...);

namespace altair::platform {

bool setCustomBaud(int fd, long long baud, std::string& err) {
    // Read the port's current line settings -- serial_posix.cpp has already framed it
    // (data bits, parity, stop bits) via tcsetattr; we change only the speed on top.
    termios2 t{};
    if (ioctl(fd, TCGETS2, &t) != 0) {
        err = std::string("TCGETS2: ") + std::strerror(errno);
        return false;
    }

    // Replace the coded rate in CBAUD with BOTHER -- "the speed is not one of the named
    // constants, read it from c_ispeed/c_ospeed as a literal number".
    t.c_cflag &= ~CBAUD;
    t.c_cflag |= BOTHER;
    t.c_ispeed = (speed_t)baud;
    t.c_ospeed = (speed_t)baud;

    if (ioctl(fd, TCSETS2, &t) != 0) {
        err = "the host serial driver rejected " + std::to_string(baud) +
              " baud: " + std::strerror(errno);
        return false;
    }
    return true;
}

} // namespace altair::platform
