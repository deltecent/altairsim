// POSIX serial port -- termios, TIOCMGET/TIOCMSET. See src/platform/serial.h.
//
// This file is ALLOWED to know it is on a POSIX system. Nothing else in the tree
// is (DESIGN.md 2.1), which is why there is not one #ifdef in it: there is nothing
// to be conditional ABOUT. The file simply is the POSIX answer.

#include "platform/serial.h"

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>

namespace altair::platform {
namespace {

// termios speaks in speed_t constants, not numbers, and the set is finite. A rate
// the host cannot do is an ERROR with the list attached -- not a silent fallback to
// 9600, which would leave you staring at garbage wondering which end was wrong.
speed_t toSpeed(long long baud) {
    switch (baud) {
    case 50: return B50;
    case 75: return B75;
    case 110: return B110;
    case 134: return B134;
    case 150: return B150;
    case 200: return B200;
    case 300: return B300;
    case 600: return B600;
    case 1200: return B1200;
    case 1800: return B1800;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    default: return 0;
    }
}

class PosixSerial : public SerialPort {
public:
    PosixSerial(int fd, std::string path) : fd_(fd), path_(std::move(path)) {}

    ~PosixSerial() override {
        if (fd_ >= 0) {
            // Drop RTS and DTR on the way out. A card being unplugged stops driving
            // its pins, and the far end should see that -- it is how a modem knows
            // the terminal has gone away.
            setControl(false, false);
            ::close(fd_);
        }
    }

    size_t read(uint8_t* buf, size_t n) override {
        ssize_t r = ::read(fd_, buf, n);
        return r > 0 ? (size_t)r : 0;  // EAGAIN is a quiet line, not an error
    }

    size_t write(const uint8_t* buf, size_t n) override {
        ssize_t w = ::write(fd_, buf, n);
        return w > 0 ? (size_t)w : 0;  // full driver buffer: the caller keeps the rest
    }

    bool configure(const SerialConfig& c, std::string& err) override {
        speed_t sp = toSpeed(c.baud);
        if (!sp) {
            err = "the host serial driver cannot do " + std::to_string(c.baud) +
                  " baud (50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800, "
                  "9600, 19200, 38400, 57600, 115200, 230400)";
            return false;
        }

        termios t{};
        if (tcgetattr(fd_, &t) != 0) {
            err = std::string("tcgetattr: ") + std::strerror(errno);
            return false;
        }

        cfmakeraw(&t);  // no line discipline: the GUEST owns these bytes, not the tty layer
        cfsetispeed(&t, sp);
        cfsetospeed(&t, sp);

        t.c_cflag &= ~CSIZE;
        switch (c.dataBits) {
        case 5: t.c_cflag |= CS5; break;
        case 6: t.c_cflag |= CS6; break;
        case 7: t.c_cflag |= CS7; break;
        default: t.c_cflag |= CS8; break;
        }

        if (c.stopBits >= 2) t.c_cflag |= CSTOPB;
        else t.c_cflag &= ~CSTOPB;

        if (c.parity == Parity::None) {
            t.c_cflag &= ~PARENB;
        } else {
            t.c_cflag |= PARENB;
            if (c.parity == Parity::Odd) t.c_cflag |= PARODD;
            else t.c_cflag &= ~PARODD;
        }

        // CLOCAL: do not let the DRIVER own carrier. We read DCD ourselves and give
        // it to the 6850, which has its own opinions about what a carrier loss means
        // (it latches it, and it inhibits the receiver). A driver that hung up the
        // fd on carrier loss would take that decision away from the chip.
        t.c_cflag |= CLOCAL | CREAD;

        // NO CRTSCTS. The 6850 owns RTS and CTS -- see serial.h. Handing them to the
        // driver as well is two owners for one pin.
        t.c_cflag &= ~CRTSCTS;

        t.c_cc[VMIN]  = 0;  // never wait for a byte
        t.c_cc[VTIME] = 0;

        if (tcsetattr(fd_, TCSANOW, &t) != 0) {
            err = std::string("tcsetattr: ") + std::strerror(errno);
            return false;
        }
        return true;
    }

    // THROW AWAY WHAT WAS THERE BEFORE US.
    //
    // The OS holds a receive buffer for this port, and it does NOT belong to us: it can
    // hold bytes that arrived while nothing had the port open, or -- the case that
    // actually bit -- bytes the PREVIOUS run of this program sent and never read. Open
    // the port again and the guest is handed characters from a session that ended
    // before it was powered on.
    //
    // A real 6850 comes up with an empty receive register. It does not inherit the last
    // program's characters, and neither does this. Found by an intermittent failure in
    // tests/serialtest.cpp that appeared exactly once, on the run straight after the
    // previous one -- which is how these always announce themselves.
    void discardStaleInput() { tcflush(fd_, TCIFLUSH); }

    ModemLines lines() const override {
        ModemLines m;
        int bits = 0;
        if (ioctl(fd_, TIOCMGET, &bits) != 0) return m;  // all negated: a dead line
        m.cts     = (bits & TIOCM_CTS) != 0;
        m.dsr     = (bits & TIOCM_DSR) != 0;
        m.carrier = (bits & TIOCM_CAR) != 0;
        m.ring    = (bits & TIOCM_RI) != 0;
        return m;
    }

    void setControl(bool rts, bool dtr) override {
        int set = 0, clr = 0;
        (rts ? set : clr) |= TIOCM_RTS;
        (dtr ? set : clr) |= TIOCM_DTR;
        if (set) ioctl(fd_, TIOCMBIS, &set);
        if (clr) ioctl(fd_, TIOCMBIC, &clr);
    }

    void setBreak(bool on) override {
        ioctl(fd_, on ? TIOCSBRK : TIOCCBRK, nullptr);
    }

    void flush() override { /* the write() already reached the driver */ }

    const std::string& path() const override { return path_; }

private:
    int         fd_ = -1;
    std::string path_;
};

} // namespace

std::unique_ptr<SerialPort> openSerialPort(const std::string& path, const SerialConfig& c,
                                           std::string& err) {
    // O_NONBLOCK on the OPEN, not just on the fd: opening a /dev/tty.* on macOS
    // BLOCKS until carrier appears, and with nothing plugged in that is a simulator
    // that hangs at CONNECT with no message. O_NOCTTY: this is a wire, not our
    // controlling terminal -- without it, a Ctrl-C on the far end could signal us.
    int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        err = "cannot open " + path + ": " + std::strerror(errno);
        return nullptr;
    }

    // It opened -- but is it a serial port? A path that is a plain file would sail
    // through open() and then fail every ioctl in silence.
    if (!isatty(fd)) {
        ::close(fd);
        err = path + " is not a serial device";
        return nullptr;
    }

    auto p = std::make_unique<PosixSerial>(fd, path);
    if (!p->configure(c, err)) return nullptr;

    // The port opens EMPTY. Whatever was in the driver's buffer arrived before this
    // card existed. See discardStaleInput().
    p->discardStaleInput();
    return p;
}

std::vector<std::string> listSerialPorts() {
    std::vector<std::string> out;
    DIR* d = opendir("/dev");
    if (!d) return out;

    // The prefixes that are a serial port on the systems this file compiles for:
    // macOS calls them cu.*/tty.*, Linux ttyUSB*/ttyACM*/ttyS*. Listing them is a
    // convenience for the error message and for tab completion, so a prefix this
    // misses costs a suggestion, not a capability -- the path still opens.
    static const char* kPrefixes[] = {"cu.", "ttyUSB", "ttyACM", "ttyS"};
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        for (const char* p : kPrefixes) {
            if (n.rfind(p, 0) == 0) {
                out.push_back("/dev/" + n);
                break;
            }
        }
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace altair::platform
