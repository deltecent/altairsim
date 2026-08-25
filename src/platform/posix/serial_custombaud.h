// A baud rate the standard termios speed_t table has no constant for -- 76800 the most
// wanted of them, an S-100 staple that glibc's <termios.h> simply never named. The OS
// can still be told the number directly, but the mechanism is OS-specific: Linux uses
// the kernel's BOTHER/termios2 ioctl, macOS uses IOSSIOSPEED. That difference is a
// per-OS FILE, never an #ifdef in the shared serial_posix.cpp (DESIGN.md 2.1) -- so the
// two implementations live in posix/linux/ and posix/macos/ and this header is their
// one contract.
#pragma once

#include <string>

namespace altair::platform {

// `fd` is an already-open, already-framed serial port (data bits, parity and stop bits
// set); this sets ONLY its speed, to `baud` exactly. Returns false and fills `err` --
// with a reason a person can act on -- when the host genuinely cannot do that rate.
bool setCustomBaud(int fd, long long baud, std::string& err);

} // namespace altair::platform
