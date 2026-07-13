#pragma once
//
// FilterStream -- the CONSOLE's transform chain (DESIGN.md 7.2).
//
// A ByteStream that wraps another ByteStream and mangles the bytes on the way
// through. THERE IS EXACTLY ONE OF THESE IN THE SIMULATOR, and the Console owns
// it (host/console.h). Nothing else may have one.
//
// AN EARLIER VERSION OF THIS FILE ARGUED THE OPPOSITE, at some length: that the
// chain belonged on the LINE, inside every UART, so that `SET sio0 UPPER=ON`
// would work on a socket as well as on the console. It is a seductive argument
// and it is WRONG, because a line is not a terminal:
//
//   A card's connector goes to a modem, a socket, a paper-tape reader or a real
//   /dev/tty.usbserial, and the next thing down it is XMODEM -- 8-BIT BINARY. A
//   `strip7out` on that line masks bit 7 of every byte of the transfer and does
//   it SILENTLY. Set it once for MITS BASIC, forget, and every binary file that
//   ever leaves the machine is quietly corrupt.
//
// The 88-ACR reached this conclusion first and refused the chain outright -- "a
// tape is binary, not text" (tests/test_88acr.cpp) -- and every other endpoint
// deserves the same protection. So: socket, serial port, tape, file and loopback
// are 8-BIT CLEAN, ALWAYS. Only the console transforms, because only the console
// has a human on the end of it, and every one of these transforms is a fact about
// a TERMINAL.
//
// What a LINE has instead is LINE CODING -- baud, data bits, parity, stop bits.
// That is hardware (the 88-SIO's jumpers, the 6850's control register), it lives
// on the card, and on a real serial port it is programmed into the real port.
// A frame is not a filter.
//
// These settings are PROPERTIES, declared through the same Property layer as a
// board's -- so SET, SHOW, TOML, CONFIG SAVE, MCP and tab completion all pick them
// up with no code anywhere. There is one schema.
//
// DIRECTIONS, named from the GUEST's point of view, because the guest is who we
// are lying to:
//   inbound  = keyboard -> guest   (what the human typed)
//   outbound = guest -> screen     (what the program printed)

#include "core/value.h"
#include "host/stream.h"

#include <memory>
#include <string>
#include <vector>

namespace altair {

// Where a rubout key should end up. A perennial CP/M annoyance and worth a
// property of its own: a modern terminal's Backspace sends DEL (0x7F), and a
// great deal of period software only understands BS (0x08) -- so the key does
// nothing at all and you conclude the emulator is broken.
enum class BsMap {
    Off,  // pass whatever the terminal sent
    Bs,   // fold DEL -> BS   (what most period software wants)
    Del,  // fold BS -> DEL   (what a few things want instead)
};

class FilterStream : public ByteStream {
public:
    explicit FilterStream(std::unique_ptr<ByteStream> inner) : inner_(std::move(inner)) {}

    ByteStream* inner() { return inner_.get(); }

    // THERE IS NO reconnect(). There used to be, because the chips each built a
    // FilterStream around whatever was plugged into them and a fresh CONNECT threw
    // the transforms away. The console's keyboard and screen are not plugged in and
    // cannot be unplugged, so the chain it owns is built once and outlives every
    // CONNECT in the machine -- including a board taking the console from another.

    // The chain is transparent about what it wraps: SHOW says `console`, not
    // `filter(console)`. The filter is not a thing the operator plugged in.
    std::string describe() const override { return inner_->describe(); }

    size_t read(uint8_t* buf, size_t n) override;
    size_t write(const uint8_t* buf, size_t n) override;

    bool readable() const override { return inner_->readable(); }
    bool writable() const override { return inner_->writable(); }
    void flush() override { inner_->flush(); }
    void pump() override { inner_->pump(); }

    // The modem pins and the line settings pass STRAIGHT THROUGH, untransformed.
    // The filter chain mangles DATA -- it folds case, it maps a rubout. A pin is not
    // data, and a filter that had an opinion about carrier would be inventing one.
    LineStatus status() const override { return inner_->status(); }
    void setControl(const LineControl& c) override { inner_->setControl(c); }
    bool setParams(const LineParams& p, std::string& err) override {
        return inner_->setParams(p, err);
    }

    // The transform set. Declared once, consumed by everything.
    std::vector<Property> properties();

private:
    std::unique_ptr<ByteStream> inner_;

    bool   upper_     = false;
    bool   strip7in_  = false;
    bool   strip7out_ = false;
    bool   crlf_      = false;
    bool   echo_      = false;
    bool   bell_      = true;
    BsMap  bsmap_     = BsMap::Off;
};

} // namespace altair
