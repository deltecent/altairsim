#pragma once
//
// A real serial port on the host (DESIGN.md 2.1, 7.1).
//
// THE CONTRACT. Pure declarations, zero conditionals, no OS type anywhere in the
// signature -- no `int fd`, no `HANDLE`. That is the rule that makes 2.1 hold:
// the moment an OS type appears in an interface, every caller needs an #ifdef to
// name it, and you are SIMH again.
//
// One implementation file per OS, chosen by CMake:
//   src/platform/posix/serial_posix.cpp   -- termios, TIOCMGET/TIOCMSET
//   src/platform/win32/serial_win32.cpp   -- SetCommState, GetCommModemStatus
//
// NON-BLOCKING, ALWAYS, AND THAT IS NOT AN OPTION. A board asks readable() and
// moves on (7.1); a read that waited for a byte would stop emulated time to wait
// for a human. read() returning 0 is a quiet line, not an error and not an EOF.
//
// THERE IS NO THREAD BEHIND THIS. 7.5 permits one, and we do not need one: a
// serial port is a file descriptor that can be asked, without waiting, whether it
// has anything. A thread would buy nothing and would cost the determinism that
// RECORD/REPLAY is built on -- the bytes would arrive at whatever T-state the host
// scheduler felt like. They arrive in pump(), at a known point in emulated time.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair::platform {

enum class Parity { None, Even, Odd };

// HOW THE CARD IS PROGRAMMED, NOT WHAT THE OPERATOR WANTS (Patrick, 2026-07-12).
//
// There is exactly ONE line rate in an 88-2SIO and it is the 6850's clock -- a
// jumper on the card. The frame format is whatever the guest wrote into the
// control register, because those bits ARE what goes on the wire. So this struct
// is filled in from the chip, not from a property: `SET sio0:a BAUD=300` restraps
// the card, and the host UART follows it.
//
// A card strapped for 300 talking to a terminal set to 9600 does not give you a
// fast link on real hardware. It gives you garbage. There is no second baud rate
// to configure, and inventing one would only let you configure the garbage.
struct SerialConfig {
    long long baud     = 9600;
    int       dataBits = 8;     // 5..8
    int       stopBits = 1;     // 1 or 2
    Parity    parity   = Parity::None;

    // NO FLOW-CONTROL FIELD, DELIBERATELY. Hardware flow control in termios means
    // the OS DRIVER owns RTS and CTS -- and on this card the 6850 owns them: RTS is
    // control-register bits 5-6, and CTS gates TDRE (MC6850 data sheet, and see
    // mits-2sio.cpp). Two owners of one pin is not a feature, it is a bug that only
    // shows up under load. The port is opened with flow control OFF and the chip
    // drives the pins itself, which is the only arrangement in which `SET sio0:a
    // cts=wired` can mean anything at all.
};

// The four inputs, as the CARD sees them: TRUE means ASSERTED. The pin-level
// inversions (/DCD, /CTS are active-low at the chip) stay inside the chip that has
// those pins, which is where the datasheet puts them.
struct ModemLines {
    bool cts     = false;
    bool dsr     = false;
    bool carrier = false;  // DCD
    bool ring    = false;  // RI
};

class SerialPort {
public:
    virtual ~SerialPort() = default;

    // Non-blocking. A short count is normal and is not an error.
    virtual size_t read(uint8_t* buf, size_t n) = 0;
    virtual size_t write(const uint8_t* buf, size_t n) = 0;

    // Re-program the port. Called when the card is restrapped or when the guest
    // rewrites the 6850's control register -- both of which change what is on the
    // wire, on real hardware too.
    virtual bool configure(const SerialConfig& c, std::string& err) = 0;

    virtual ModemLines lines() const = 0;

    // The two outputs a UART drives. The 6850 has an RTS pin and no DTR pin; the
    // interface carries both because the PMMI modem card has both, and DTR is how
    // that card hangs up the phone.
    virtual void setControl(bool rts, bool dtr) = 0;
    virtual void setBreak(bool on) = 0;

    // Push what we have written at the OS. Not a wait for the far end.
    virtual void flush() = 0;

    virtual const std::string& path() const = 0;
};

// Null and `err` set on anything that will not open -- never a silent dead port.
// An operator who mistypes a device name has a port that is MISSING, and a stream
// that quietly went to /dev/null would look exactly like a dead cable.
std::unique_ptr<SerialPort> openSerialPort(const std::string& path, const SerialConfig& c,
                                           std::string& err);

// What is plugged in right now, for the error message and for tab completion. An
// empty list is a legitimate answer on a machine with no serial ports.
std::vector<std::string> listSerialPorts();

} // namespace altair::platform
