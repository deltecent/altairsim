// Win32 serial port -- SetCommState / GetCommModemStatus. See src/platform/serial.h.
//
// ---------------------------------------------------------------------------
// UNBUILT AND UNTESTED. Written 2026-07-12 on macOS, where no compiler has ever
// looked at it and no cable has ever proved it. It is here so that a Windows build
// LINKS and so that the porting work is a debugging job rather than a design job --
// but do not mistake it for working code, and do not mistake its absence of #ifdefs
// for evidence that it runs. The POSIX half was proved against two FTDI cables and
// a null modem (tests/serialtest.cpp); this half has had none of that.
//
// docs/porting-notes.md says the same thing, and the first person to build on
// Windows should expect to spend an afternoon here.
// ---------------------------------------------------------------------------

#include "platform/serial.h"

#include <windows.h>

#include <algorithm>
#include <string>

namespace altair::platform {
namespace {

std::string lastError(const char* what) {
    DWORD e = GetLastError();
    char* msg = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, e, 0, (LPSTR)&msg, 0, nullptr);
    std::string s = std::string(what) + ": " + (msg ? msg : "unknown error");
    if (msg) LocalFree(msg);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

class Win32Serial : public SerialPort {
public:
    Win32Serial(HANDLE h, std::string path) : h_(h), path_(std::move(path)) {}

    ~Win32Serial() override {
        if (h_ != INVALID_HANDLE_VALUE) {
            setControl(false, false);  // a card coming out of the machine stops driving
            CloseHandle(h_);
        }
    }

    size_t read(uint8_t* buf, size_t n) override {
        DWORD got = 0;
        if (!ReadFile(h_, buf, (DWORD)n, &got, nullptr)) return 0;
        return got;  // the timeouts below make this return AT ONCE, with 0 if nothing
    }

    size_t write(const uint8_t* buf, size_t n) override {
        DWORD put = 0;
        if (!WriteFile(h_, buf, (DWORD)n, &put, nullptr)) return 0;
        return put;
    }

    bool configure(const SerialConfig& c, std::string& err) override {
        DCB dcb{};
        dcb.DCBlength = sizeof dcb;
        if (!GetCommState(h_, &dcb)) {
            err = lastError("GetCommState");
            return false;
        }

        dcb.BaudRate = (DWORD)c.baud;   // Win32 takes the NUMBER, not a speed_t constant
        dcb.ByteSize = (BYTE)c.dataBits;
        dcb.StopBits = c.stopBits >= 2 ? TWOSTOPBITS : ONESTOPBIT;

        switch (c.parity) {
        case Parity::Even: dcb.Parity = EVENPARITY; dcb.fParity = TRUE; break;
        case Parity::Odd:  dcb.Parity = ODDPARITY;  dcb.fParity = TRUE; break;
        default:           dcb.Parity = NOPARITY;   dcb.fParity = FALSE; break;
        }

        // THE DRIVER DOES NOT TOUCH THE PINS. The 6850 owns RTS and CTS (serial.h),
        // so the handshake fields are all disabled and RTS/DTR are left under our
        // manual control via EscapeCommFunction. RTS_CONTROL_HANDSHAKE here would put
        // the driver in charge of a pin the chip is already driving.
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDsrSensitivity = FALSE;
        dcb.fRtsControl  = RTS_CONTROL_ENABLE;
        dcb.fDtrControl  = DTR_CONTROL_ENABLE;
        dcb.fOutX = FALSE;   // and NO XON/XOFF: that is the GUEST's business, always
        dcb.fInX  = FALSE;   // (Patrick, 2026-07-12) -- a card that ate a ^S would be
        dcb.fBinary = TRUE;  // eating CP/M's data
        dcb.fAbortOnError = FALSE;

        if (!SetCommState(h_, &dcb)) {
            err = lastError("SetCommState (baud " + std::to_string(c.baud) + ")");
            return false;
        }

        // NON-BLOCKING, the Win32 way: a read returns immediately with whatever is
        // there, including nothing. This is what MAXDWORD/0/0 means, and it is the
        // documented idiom -- the alternative is OVERLAPPED, which buys nothing here
        // because we never want to wait.
        COMMTIMEOUTS t{};
        t.ReadIntervalTimeout        = MAXDWORD;
        t.ReadTotalTimeoutMultiplier = 0;
        t.ReadTotalTimeoutConstant   = 0;
        t.WriteTotalTimeoutMultiplier = 0;
        t.WriteTotalTimeoutConstant   = 0;
        if (!SetCommTimeouts(h_, &t)) {
            err = lastError("SetCommTimeouts");
            return false;
        }
        return true;
    }

    // THROW AWAY WHAT WAS THERE BEFORE US -- see the POSIX file, where an intermittent
    // hardware-test failure found this. The driver's receive buffer can hold bytes from
    // a session that ended before this card was powered on, and a real 6850 comes up
    // with an empty receive register.
    void discardStaleInput() { PurgeComm(h_, PURGE_RXCLEAR | PURGE_TXCLEAR); }

    ModemLines lines() const override {
        ModemLines m;
        DWORD s = 0;
        if (!GetCommModemStatus(h_, &s)) return m;  // all negated: a dead line
        m.cts     = (s & MS_CTS_ON) != 0;
        m.dsr     = (s & MS_DSR_ON) != 0;
        m.carrier = (s & MS_RLSD_ON) != 0;  // "receive line signal detect" IS DCD
        m.ring    = (s & MS_RING_ON) != 0;
        return m;
    }

    void setControl(bool rts, bool dtr) override {
        EscapeCommFunction(h_, rts ? SETRTS : CLRRTS);
        EscapeCommFunction(h_, dtr ? SETDTR : CLRDTR);
    }

    void setBreak(bool on) override { EscapeCommFunction(h_, on ? SETBREAK : CLRBREAK); }

    void flush() override { /* WriteFile already reached the driver */ }

    const std::string& path() const override { return path_; }

private:
    HANDLE      h_ = INVALID_HANDLE_VALUE;
    std::string path_;
};

} // namespace

std::unique_ptr<SerialPort> openSerialPort(const std::string& path, const SerialConfig& c,
                                           std::string& err) {
    // COM10 AND ABOVE NEED THE \\.\ PREFIX. Without it, CreateFile fails with "file
    // not found" on exactly the ports a machine with a USB hub full of adapters will
    // have -- which is to say, the ones this is for. Applying the prefix always is
    // harmless and skips the whole class of bug.
    std::string dev = path;
    if (dev.rfind("\\\\.\\", 0) != 0) dev = "\\\\.\\" + dev;

    HANDLE h = CreateFileA(dev.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err = "cannot open " + path + ": " + lastError("CreateFile");
        return nullptr;
    }

    auto p = std::make_unique<Win32Serial>(h, path);
    if (!p->configure(c, err)) return nullptr;
    p->discardStaleInput();  // the port opens EMPTY
    return p;
}

std::vector<std::string> listSerialPorts() {
    std::vector<std::string> out;
    char buf[4096];
    for (int i = 1; i <= 255; ++i) {
        std::string name = "COM" + std::to_string(i);
        // QueryDosDevice succeeds only for a port that EXISTS -- which is the cheap
        // way to enumerate without dragging in SetupAPI for a help message.
        if (QueryDosDeviceA(name.c_str(), buf, sizeof buf) != 0) out.push_back(name);
    }
    return out;
}

} // namespace altair::platform
