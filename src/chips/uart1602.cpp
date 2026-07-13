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

// THE STRAPS, AS A FRAME ON THE WIRE.
//
// The format pins are not decoration and they are not only a duration. On a REAL
// serial port they are the frame: a card jumpered for 7 data bits, even parity, 2
// stop bits opens the host port at 7E2, because that is what a COM2502 strapped
// that way actually puts on the line. Every other endpoint ignores this -- a
// socket has no baud rate and a tape has no parity -- which is exactly why it is
// pushed at the stream and not implemented in the chip.
//
// NOTE WHAT THIS IS NOT: it is not a mask. The chip never ANDs the guest's data
// with the word length. If a card is strapped for 7 bits then the eighth bit does
// not travel, because the frame does not carry it -- and if it is strapped for 8,
// all eight arrive, MITS BASIC's terminator included. That is the hardware, and
// the terminal is what decides to ignore it (host/console.h).
LineParams Uart1602::params() const {
    LineParams p;
    p.baud     = baud;
    p.dataBits = dataBits;
    p.parity   = parity;
    p.stopBits = stopBits;
    return p;
}

void Uart1602::programLine() {
    std::string err;
    if (stream_->setParams(params(), err)) return;

    // The host refused. Say it once, in a sentence, and go on running at the strap --
    // the strap is what the guest can measure, and it is still what the card is
    // jumpered to. Silence here would be a wire running at a frame nobody chose.
    log_.push_back(name_ + ": " + err);
}

void Uart1602::connect(std::unique_ptr<ByteStream> s) {
    // THE LINE IS TAKEN AS IT IS -- no transform chain. The chain is the console's
    // and only the console's (host/console.h): this card's connector goes to a modem,
    // a socket or a paper-tape reader, and a filter on it would silently corrupt the
    // first 8-bit binary transfer through the port.
    stream_ = std::move(s);

    // A NEW LINE STARTS WHERE THE CARD ALREADY IS: the straps are soldered, so the
    // wire is brought up to them rather than the other way round.
    programLine();
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
