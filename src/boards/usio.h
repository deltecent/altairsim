#pragma once
//
// USIO -- the Universal Serial board. See docs/devguide/serial-io.md.
//
// A serial card that emulates NO PARTICULAR UART. Every other serial board in the
// machine is a chip (the 88-2SIO/Turnkey are a 6850, the 88-SIO/ACR/UIO a COM2502,
// the SBC an 8251, the PMMI an MM-103) with that chip's register map, polarity and
// quirks baked in. The USIO is the opposite: it is DESCRIBED, not emulated. The
// operator says where the status/control port lives, which status bit means
// "receive data ready" and which means "transmit data empty" (and whether each is
// active low), and where the data port lives -- and that IS the card.
//
// It exists because "read a status bit, then read/write a data byte" is the shape of
// nearly every polled UART ever put on the S-100 bus, and a card that lets you strap
// that shape covers a long tail of boards nobody will ever write a dedicated model
// for. The two built-in PROFILES (Cromemco TU-ART, IMSAI SIO-2) are just named
// bundles of those straps -- adding a third is one struct in usioBuiltins().
//
// ---------------------------------------------------------------------------
// WHAT THIS MODELS, AND WHAT IT DELIBERATELY DOES NOT.
//
// Two ports, polled. A STATUS/CONTROL port (read synthesizes the status byte from
// the line's readable()/writable(); write is accepted and DISCARDED -- there is no
// control register to program because there is no chip to program it into) and a
// DATA port (read takes a byte off the line, write puts one on it). Transmit is
// immediate -- the emulated line is never baud-gated; `baud` programs a real serial
// port only (see below). Everything moves over a ByteStream, so file / socket /
// serial / null / loopback / in: / out: all come for free (host/stream.h).
//
// NO INTERRUPTS. The card never drives pin 73 or a VI line -- it is polled-only.
// This is deliberate and not merely unfinished: without a control register there is
// no interrupt-enable to gate a transmit interrupt, and a strapped TX-empty interrupt
// would storm (TDRE is asserted whenever the line is idle, which is almost always).
// Interrupts are an explicit later phase; when they land they bring an enable strap
// with them.
// ---------------------------------------------------------------------------

#include "core/board.h"
#include "host/stream.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

// A strap bundle -- pure data, the whole of what a "board profile" is. Where the two
// ports sit, which status bits carry RDR/TDRE, and whether each bit is active low.
struct UsioProfile {
    uint8_t statusPort = 0x00;   // status(read) / control(write, discarded)
    uint8_t dataPort   = 0x01;   // rx(read) / tx(write)
    uint8_t rdrBit     = 0;      // status bit: receive data ready
    uint8_t tdreBit    = 1;      // status bit: transmit data empty
    bool    rdrActiveLow  = false;
    bool    tdreActiveLow = false;
};

// A named profile. Its `name` becomes a `profile` property choice automatically, so a
// new built-in is exactly one entry in usioBuiltins() -- no other file to touch.
struct UsioBuiltin {
    std::string name;
    std::string help;
    UsioProfile profile;
};

// THE ONE PLACE BUILT-INS LIVE (usio.cpp). List/extend from here.
const std::vector<UsioBuiltin>& usioBuiltins();

class UsioBoard : public Board {
public:
    UsioBoard();

    std::string type() const override { return "usio"; }

    // ---- bus: two ports, polled ----
    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    // ---- lifecycle ----
    void pump() override { stream_->pump(); }
    void configChanged() override;

    // ---- reflection ----
    std::vector<Property> properties() override;
    std::vector<UnitDef>  units() const override;
    std::vector<MapEntry> ioMap() const override;

    // ---- the one serial unit, `serial` ----
    bool connect(const std::string& unit, const std::string& endpoint, std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;
    ByteStream* unitStream(const std::string& unit) override {
        return unit == "serial" ? stream_.get() : nullptr;
    }
    uint64_t rxBytes() const override { return rxBytes_; }
    std::vector<std::string> drainLog() override;

    // ---- SNAPSHOT / RESTORE (DESIGN.md 13): only the byte counter is runtime state;
    // the straps are config and the stream is a host handle re-resolved from `connect`. ----
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // The endpoint grammar travels as a function; the board never learns it (DESIGN.md 7.7).
    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;
    static void setResolver(EndpointResolver r);

private:
    // Synthesize the status byte from the line's pins at the strapped bit positions,
    // applying each active-low inversion (host/stream.h: readable()->RDR, writable()->TDRE).
    uint8_t statusByte() const;

    // Push `baud`/8N1 at the wire. Ignored by every endpoint but a real serial port,
    // which is the only one that HAS a baud rate -- and the only one that can refuse
    // (the err is surfaced through drainLog(), the 6850's discipline).
    void programLine();

    // Copy a profile's straps into the live fields (leaves `baud` alone -- it is not a
    // property of the card being emulated, but of the cable you plug into it).
    void applyProfile(const UsioProfile& p);

    // ---- config / straps (rebuilt from TOML) ----
    std::string profile_ = "custom";  // the selected built-in, or "custom"
    UsioProfile straps_;              // the live status/data/bit/polarity straps
    long long   baud_ = 9600;         // programmed onto a real serial port only

    // ---- the line ----
    std::unique_ptr<ByteStream> stream_;  // never null -- a NullStream when nothing is plugged in
    uint64_t                    rxBytes_ = 0;

    std::vector<std::string> log_;
};

} // namespace altair
