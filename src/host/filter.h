#pragma once
//
// FilterStream -- the serial transform chain (DESIGN.md 7.2).
//
// A ByteStream that wraps another ByteStream and mangles the bytes on the way
// through. Every serial unit gets one, whatever it is connected to.
//
// THIS IS NOT CONSOLE CODE, and that is the entire point. The design says it
// plainly: "a real terminal on a real host serial port wants the same uppercase
// folding, so `SET sio2b UPPER=ON` on a socket-connected line works for free."
// Put the uppercase fold in the console and it works for the console; put it
// here and it works for the console, the socket, the modem and the VT100 on
// /dev/tty.usbserial that Patrick actually owns.
//
// It also means these settings are PROPERTIES, declared through the same
// Property layer as a board's -- so SET, SHOW, TOML, CONFIG SAVE, MCP and tab
// completion all pick them up with no code anywhere. There is one schema.
//
// DIRECTIONS, named from the GUEST's point of view, because the guest is who we
// are lying to:
//   inbound  = endpoint -> guest   (what the human typed)
//   outbound = guest -> endpoint   (what the program printed)

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

    // The chain is transparent about what it wraps: SHOW says `console`, not
    // `filter(console)`. The filter is not a thing the operator plugged in.
    std::string describe() const override { return inner_->describe(); }

    size_t read(uint8_t* buf, size_t n) override;
    size_t write(const uint8_t* buf, size_t n) override;

    bool readable() const override { return inner_->readable(); }
    bool writable() const override { return inner_->writable(); }
    void flush() override { inner_->flush(); }
    LineStatus status() const override { return inner_->status(); }
    void pump() override { inner_->pump(); }

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
