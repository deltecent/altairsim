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

    // SWAP THE ENDPOINT AND KEEP THE TRANSFORMS. This is the whole claim in the
    // header comment above, and it is only true if the chain SURVIVES a CONNECT.
    //
    // Both chips used to build a fresh FilterStream every time something was plugged
    // in, which quietly reset upper/strip7*/crlf/echo/bell/bsdel to their defaults --
    // so `SET sio0 UPPER=ON` followed by `CONNECT sio0:tty socket` lost the fold, with
    // no message, and a machine file that set a transform BEFORE `connect` (the loader
    // applies keys in file order) had it thrown away before the machine ever started.
    //
    // The transforms belong to the LINE, not to what is on the far end of it. The
    // 6850's own connect() already says so about its pins -- "a new line starts where
    // the card already is" -- and the chain is no different: unplugging a Teletype and
    // plugging in a VT100 does not unsolder anything.
    void reconnect(std::unique_ptr<ByteStream> inner) { inner_ = std::move(inner); }

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
