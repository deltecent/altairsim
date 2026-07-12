#include "host/hostserial.h"

namespace altair {

// ---------------------------------------------------------------------------
// READS COME OUT OF THE BUFFER, NOT OFF THE PORT.
//
// readable() has to be able to answer WITHOUT touching the host -- the 6850 asks it
// from inside a bus cycle, and 7.1 says all real I/O happens in pump(), once per
// time slice, at a known point in emulated time. So pump() drains the driver into
// rx_ and this hands it out. It is the same shape the Console already has, and for
// the same reason: a receive register fills whether or not anybody asks it to.
// ---------------------------------------------------------------------------
size_t HostSerialStream::read(uint8_t* buf, size_t n) {
    size_t k = rx_.size() < n ? rx_.size() : n;
    for (size_t i = 0; i < k; ++i) buf[i] = (uint8_t)rx_[i];
    rx_.erase(0, k);
    return k;
}

size_t HostSerialStream::write(const uint8_t* buf, size_t n) {
    tx_.append((const char*)buf, n);
    flush();
    return n;  // ALWAYS all of it: what the driver would not take is still ours to send
}

void HostSerialStream::flush() {
    while (!tx_.empty()) {
        size_t w = port_->write((const uint8_t*)tx_.data(), tx_.size());
        if (w == 0) break;  // driver buffer full -- pump() will try again
        tx_.erase(0, w);
    }
    port_->flush();
}

void HostSerialStream::pump() {
    uint8_t buf[256];
    for (;;) {
        size_t r = port_->read(buf, sizeof buf);
        if (r == 0) break;
        rx_.append((const char*)buf, r);
    }
    flush();
}

// The real modem-status register, unretouched. THE PINS ARE ACTIVE-LOW ON THE WIRE
// and the OS has already un-inverted them for us -- TIOCM_CTS set means CTS is
// asserted -- so what comes back here is already in this program's convention
// (true = asserted) and the 6850 re-inverts it for its own status bits. Each layer
// inverts exactly once, in the place that owns the polarity.
LineStatus HostSerialStream::status() const {
    platform::ModemLines m = port_->lines();
    LineStatus s;
    s.cts     = m.cts;
    s.dsr     = m.dsr;
    s.carrier = m.carrier;
    s.ring    = m.ring;
    return s;
}

void HostSerialStream::setControl(const LineControl& c) {
    port_->setControl(c.rts, c.dtr);
    port_->setBreak(c.brk);
}

// THE CARD PROGRAMS THE WIRE. `SET sio0:a BAUD=300` restraps the 6850's clock, and
// this is where that reaches the FTDI cable; a guest that writes 7E1 into the
// control register reconfigures the port for 7E1, because those bits ARE the frame.
//
// A RATE THE HOST CANNOT DO IS NOT A FAILED CONNECT. The card is a jumper: it is
// strapped to 76800 whether or not this cable can be, and it goes on pacing the
// guest at 76800 either way -- which is the half the guest can actually measure. So
// the port keeps its last good settings, we return false, and the CARD SAYS SO, in
// a sentence, through drainLog(). What must never happen is the silent version.
bool HostSerialStream::setParams(const LineParams& p, std::string& err) {
    platform::SerialConfig c;
    c.baud     = p.baud;
    c.dataBits = p.dataBits;
    c.stopBits = p.stopBits;
    c.parity   = p.parity == LineParity::Even  ? platform::Parity::Even
                 : p.parity == LineParity::Odd ? platform::Parity::Odd
                                               : platform::Parity::None;
    return port_->configure(c, err);
}

} // namespace altair
