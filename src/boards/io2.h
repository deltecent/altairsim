#pragma once
//
// SSM IO-2 -- a strap-configurable serial board. See docs/devguide/serial-io.md.
//
// The real SSM IO-2 (reference/SSM IO-2 Parallel IO Board.md) is a serial+parallel
// card built around an AY-5-1013 / TMS6011 / IM6402 UART, whose serial personality is
// a MITS-SIO clone: two ports (status/control + data), polled, and a bank of jumpers
// that let it imitate most polled status+data serial cards on the S-100 bus. WE MODEL
// ONLY THAT SINGLE SERIAL PORT -- the parallel port and 1702-PROM socket are out of
// scope, and a user wanting more serial ports adds more `io2` boards.
//
// Like every serial personality of that shape, this board emulates NO PARTICULAR UART
// register-for-register. Other serial boards are a chip (the 88-2SIO/Turnkey a 6850,
// the 88-SIO/ACR a COM2502, the SBC an 8251, the PMMI an MM-103) with that chip's
// register map baked in. The IO-2 is the opposite: it is DESCRIBED, not emulated. The
// operator says where the status/control port lives, which status bit is DAV (data
// available) and which is TBMT (transmit buffer empty), whether the inverter gate is
// engaged, and where the data port lives -- and that IS the card.
//
// The strapped shape -- "read a status bit, then read/write a data byte" -- is nearly
// every polled UART ever put on the S-100 bus, so the built-in PROFILES are named
// bundles of those straps that make the board come up as a specific card. Adding one is
// one struct in io2Builtins(). The default profile is `sior0`, the MITS SIO Rev 0 that
// the SSM 8080 System Monitor expects on its console.
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
// would storm (TBMT is asserted whenever the line is idle, which is almost always).
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
// ports sit, which status bits carry DAV/TBMT, and whether the inverter gate is engaged
// (on the real IO-2 both status bits pass through the SAME inverting buffer, so their
// polarity is a single knob, not two).
struct Io2Profile {
    uint8_t statusPort = 0x00;   // status(read) / control(write, discarded)
    uint8_t dataPort   = 0x01;   // rx(read) / tx(write)
    uint8_t davBit     = 0;      // status bit: DAV, data available (receive)
    uint8_t tbmtBit    = 7;      // status bit: TBMT, transmit buffer empty
    bool    inverterGate = true; // both bits through the inverter gate: asserted reads 0
};

// A named profile. Its `name` becomes a `profile` property choice automatically, so a
// new built-in is exactly one entry in io2Builtins() -- no other file to touch.
struct Io2Builtin {
    std::string name;
    std::string help;
    Io2Profile  profile;
};

// THE ONE PLACE BUILT-INS LIVE (io2.cpp). List/extend from here.
const std::vector<Io2Builtin>& io2Builtins();

class Io2Board : public Board {
public:
    Io2Board();

    std::string type() const override { return "io2"; }

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
    // applying the inverter gate (host/stream.h: readable()->DAV, writable()->TBMT).
    uint8_t statusByte() const;

    // Push `baud`/8N1 at the wire. Ignored by every endpoint but a real serial port,
    // which is the only one that HAS a baud rate -- and the only one that can refuse
    // (the err is surfaced through drainLog(), the 6850's discipline).
    void programLine();

protected:
    // Copy a profile's straps into the live fields (leaves `baud` alone -- it is not a
    // property of the card being emulated, but of the cable you plug into it). Protected so a
    // subtype (e.g. PropIoBoard, the Console IO Board) can preset a real card's straps in its
    // constructor -- the engine is unchanged, only the defaults differ.
    void applyProfile(const Io2Profile& p);

    // Set the reported profile name. Protected so a subtype that presets custom straps can
    // keep `profile` coherent for CONFIG SAVE (the default profile is `sior0`).
    void setProfileName(const std::string& name) { profile_ = name; }

private:

    // ---- config / straps (rebuilt from TOML) ----
    std::string profile_ = "sior0";   // the selected built-in, or "custom"
    Io2Profile  straps_;              // the live status/data/bit/polarity straps
    long long   baud_ = 9600;         // programmed onto a real serial port only

    // ---- the line ----
    std::unique_ptr<ByteStream> stream_;  // never null -- a NullStream when nothing is plugged in
    uint64_t                    rxBytes_ = 0;

    std::vector<std::string> log_;
};

} // namespace altair
