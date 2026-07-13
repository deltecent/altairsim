#include "chips/uart1602.h"

#include "host/stream.h"

#include <utility>

namespace altair {

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

// A character on the wire is a start bit, the data bits, maybe a parity bit, and the
// stop bits. Every one of those is a PIN on this chip (NDB1/NDB2, NPB/POE, NSB), and
// on both cards that use it, those pins are solder pads. The guest cannot change any
// of it -- which is the deepest difference between this chip and the 6850, where the
// same numbers come out of a control register the guest wrote.
int Uart1602::bitsPerChar() const {
    return 1 + dataBits + (parity == LineParity::None ? 0 : 1) + stopBits;
}

uint64_t Uart1602::charTStates(const Clock& clk) const {
    if (baud <= 0) return 0;
    return (uint64_t)(clk.hz() * (long long)bitsPerChar() / baud);
}

// TBMT -- the transmit buffer is empty once the character has had time to LEAVE. This
// is the bit the Mike Douglas CP/M BIOS times to work out the line speed, and a
// hardwired TBMT=1 silently changes what such a BIOS decides to do.
bool Uart1602::txBufferEmpty(const Clock& clk) const { return clk.now() >= txFreeAt_; }

// ---------------------------------------------------------------------------
// The line
// ---------------------------------------------------------------------------

void Uart1602::connect(std::unique_ptr<ByteStream> s) {
    // EVERY endpoint gets the transform chain, whatever it is (DESIGN.md 7.2) --
    // including the null one, so `filter_` is never dangling and the filter properties
    // never vanish from SHOW just because nothing is plugged in.
    //
    // KEEP the chain across a reconnect -- see FilterStream::reconnect(). Building a
    // new one here silently reset every transform on the line.
    if (filter_) {
        filter_->reconnect(std::move(s));
        return;
    }
    auto f  = std::make_unique<FilterStream>(std::move(s));
    filter_ = f.get();
    stream_ = std::move(f);
}

void Uart1602::disconnect() { connect(std::make_unique<NullStream>()); }

// ---------------------------------------------------------------------------
// The registers
// ---------------------------------------------------------------------------

uint8_t Uart1602::readData() {
    // The read strobe is wired to /RDAR on both cards that use this chip, so reading
    // the holding register clears Data Available -- and clears it whether or not it
    // was set. A read of an empty receive register on a real UART hands you whatever
    // was last in it, and does not error.
    rda_ = false;
    return rxData_;
}

void Uart1602::writeData(uint8_t v, const Clock& clk) {
    stream_->write(&v, 1);
    stream_->flush();
    txFreeAt_ = clk.now() + charTStates(clk);
}

// MR, pin 21. The data sheet: "sets TSO, TEOC and TBMT high, and clears RDA, RPE, RFE
// and ROR." So the transmitter comes up READY (TBMT high) and the receiver comes up
// EMPTY, which is exactly what this does.
//
// The format pins are untouched, and could not be otherwise: they are PINS. A reset
// cannot unsolder anything, and a UART that came out of a reset at some default frame
// would be a chip nobody ever built.
void Uart1602::masterReset(const Clock& clk) {
    rda_      = false;
    rxData_   = 0;
    txFreeAt_ = clk.now();  // TBMT high: the transmitter is immediately ready
    rxNextAt_ = clk.now();

    // The endpoint STAYS CONNECTED. A reset does not unplug the terminal, and a guest
    // that reset its UART and found the console gone would be a baffling thing to debug.
}

// ---------------------------------------------------------------------------
// Pull a byte off the line, if the line has had time to deliver one AND the holding
// register is free to take it.
//
// IT DOES NOT SYNTHESIZE AN OVERRUN, and the reason is worth the paragraph, because
// the first version of the 2SIO did synthesize one and lost typed characters on the
// spot.
//
// A real line is free-running: the sender shoves bits down the wire on its own clock,
// and a receiver that has not been emptied loses the next character. That is what ROR
// IS. But a ByteStream is nothing like a line. It is a buffered, flow-controlled
// source -- a pipe, a socket, an OS keyboard queue -- and it is perfectly happy to
// hold the byte until we take it. Inventing an overrun from it does not reproduce a
// hardware behaviour; it MANUFACTURES data loss that the host transport does not have.
//
// So: pace the arrivals at the line rate (that part is real, and it is the same clock
// TBMT is timed against) -- but only ever take a byte when the register is free.
//
// The honest consequence is that ROR is always false, and it is written down in the
// board's .md as a limitation rather than left for someone to discover.
// ---------------------------------------------------------------------------
void Uart1602::poll(const Clock& clk) {
    if (rda_) return;                    // register still full: the line waits
    if (clk.now() < rxNextAt_) return;   // the character has not finished arriving
    if (!stream_->readable()) return;

    uint8_t b = 0;
    if (stream_->read(&b, 1) != 1) return;

    rxData_   = b;
    rda_      = true;
    rxNextAt_ = clk.now() + charTStates(clk);
}

} // namespace altair
