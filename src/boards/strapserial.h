#pragma once
//
// StrapSerialBoard -- a chip-LESS, strap-configurable serial engine. One or more
// independent serial channels, each DESCRIBED by a bundle of straps rather than
// emulated register-for-register. See docs/devguide/serial-io.md.
//
// Two boards ride this engine:
//   * Io4Board (src/boards/io4.h)   -- the SSM IO-4, TWO serial channels ("a"/"b").
//   * PropIoBoard (src/boards/propio.h) -- the S100Computers Console IO Board, ONE
//     channel ("serial"), a Parallax-Propeller console.
//
// Unlike the 88-2SIO/Turnkey (a 6850, src/chips/mc6850.h) or the 88-SIO/ACR (a COM2502)
// or the PMMI (an MM-103), NONE of these boards emulates a particular UART. They are the
// OPPOSITE: each channel is DESCRIBED, not emulated. The operator says where the
// status/control port lives, which status bit is DAV (data available) and which is TBMT
// (transmit buffer empty), whether the inverter gate is engaged, and where the data port
// lives -- and that IS the channel.
//
// The strapped shape -- "read a status bit, then read/write a data byte" -- is nearly
// every polled UART ever put on the S-100 bus, so the built-in PROFILES are named
// bundles of those straps that make a channel come up as a specific card. Adding one is
// one struct in serialBuiltins(). The default profile is `sior0`, the MITS SIO Rev 0 that
// the SSM 8080 System Monitor expects on its console.
//
// ---------------------------------------------------------------------------
// WHAT THIS MODELS, AND WHAT IT DELIBERATELY DOES NOT.
//
// Per channel: two ports, polled. A STATUS/CONTROL port (read synthesizes the status
// byte from the line's readable()/writable(); write is accepted and DISCARDED -- there is
// no control register to program because there is no chip to program it into) and a DATA
// port (read takes a byte off the line, write puts one on it). Transmit is immediate --
// the emulated line is never baud-gated; `baud` programs a real serial port only (see
// below). Everything moves over a ByteStream, so file / socket / serial / null / loopback
// / in: / out: all come for free (host/stream.h).
//
// NO INTERRUPTS. These cards never drive pin 73 or a VI line -- they are polled-only.
// This is deliberate and not merely unfinished: without a control register there is no
// interrupt-enable to gate a transmit interrupt, and a strapped TX-empty interrupt would
// storm (TBMT is asserted whenever the line is idle, which is almost always). Interrupts
// are an explicit later phase; when they land they bring an enable strap with them.
//
// The IO-4's PARALLEL ports (two in, two out) are OUT OF SCOPE -- this engine models the
// serial half only.
// ---------------------------------------------------------------------------

#include "core/board.h"
#include "host/stream.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

// A strap bundle -- pure data, the whole of what one channel's "profile" is. Where the two
// ports sit, which status bits carry DAV/TBMT, and whether the inverter gate is engaged
// (both status bits pass through the SAME inverting buffer, so their polarity is a single
// knob, not two).
struct SerialStraps {
    uint8_t statusPort = 0x00;   // status(read) / control(write, discarded)
    uint8_t dataPort   = 0x01;   // rx(read) / tx(write)
    uint8_t davBit     = 0;      // status bit: DAV, data available (receive)
    uint8_t tbmtBit    = 7;      // status bit: TBMT, transmit buffer empty
    bool    inverterGate = true; // both bits through the inverter gate: asserted reads 0
};

// A named profile. Its `name` becomes a `profile` property choice automatically, so a
// new built-in is exactly one entry in serialBuiltins() -- no other file to touch.
struct SerialBuiltin {
    std::string  name;
    std::string  help;
    SerialStraps profile;
};

// THE ONE PLACE BUILT-INS LIVE (strapserial.cpp). List/extend from here.
const std::vector<SerialBuiltin>& serialBuiltins();

// One serial channel's runtime state: its straps, its selected profile name, its line
// rate (programmed onto a real serial port only), the line itself, and the receive
// counter. The engine holds a vector of these -- one for propio, two for io4.
struct StrapSerialChannel {
    std::string                 name;              // "serial" | "a" | "b"
    std::string                 profile = "sior0"; // the selected built-in, or "custom"
    SerialStraps                straps;            // the live status/data/bit/polarity straps
    long long                   baud    = 9600;    // programmed onto a real serial port only
    std::unique_ptr<ByteStream> stream;            // never null -- a NullStream when unplugged
    uint64_t                    rxBytes = 0;
};

class StrapSerialBoard : public Board {
public:
    // ---- bus: each channel owns two ports, polled ----
    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    // ---- lifecycle ----
    void pump() override;
    void configChanged() override;

    // ---- reflection ----
    // A single-channel board (propio) surfaces its straps as BOARD-LEVEL properties (and a
    // unit named "serial"), preserving the historical config. A multi-channel board (io4)
    // surfaces them PER-UNIT (units "a"/"b", the `[board.unit.a]` house pattern). The one
    // knob is boardLevelProps_; everything else is uniform over the channel vector.
    std::vector<Property> properties() override;
    std::vector<Property> unitProperties(const std::string& unit) override;
    std::vector<UnitDef>  units() const override;
    std::vector<MapEntry> ioMap() const override;

    // ---- the serial units ----
    bool connect(const std::string& unit, const std::string& endpoint, std::string& err) override;
    bool connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                       std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;
    ByteStream* unitStream(const std::string& unit) override;
    uint64_t rxBytes() const override;
    std::vector<std::string> drainLog() override;

    // ---- SNAPSHOT / RESTORE (DESIGN.md 13): only the byte counters are runtime state;
    // the straps are config and the streams are host handles re-resolved from `connect`. ----
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // The endpoint grammar travels as a function; the board never learns it (DESIGN.md 7.7).
    // Io4Board and PropIoBoard inherit this static and share the one resolver.
    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;
    static void setResolver(EndpointResolver r);

protected:
    // Subclass ctor wiring: add a channel (with its preset straps + reported profile name),
    // and choose where straps surface (board-level for one channel, per-unit for many).
    void addChannel(const std::string& name, const SerialStraps& straps,
                    const std::string& profileName);
    void setBoardLevelProperties(bool b) { boardLevelProps_ = b; }

    // Copy a profile's straps into a channel's live fields (leaves `baud` alone -- it is not
    // a property of the card being emulated, but of the cable you plug into it).
    void applyProfile(StrapSerialChannel& ch, const SerialStraps& p);

    // Build the profile/status_port/data_port/dav/tbmt/inverter_gate/baud/connect properties
    // for ONE channel -- profile FIRST (CONFIG SAVE ordering). Used board-level (idx 0) or
    // per-unit. Protected so a subtype could extend the set if it ever needed to.
    std::vector<Property> channelProperties(size_t idx);

private:
    int     channelIndex(const std::string& name) const;  // -1 if none
    int     channelForPort(uint8_t port) const;           // first channel owning the port, else -1

    // Synthesize a channel's status byte from its line's pins at the strapped bit positions,
    // applying the inverter gate (host/stream.h: readable()->DAV, writable()->TBMT).
    uint8_t statusByte(const StrapSerialChannel& ch) const;

    // Push a channel's `baud`/8N1 at the wire. Ignored by every endpoint but a real serial
    // port, which is the only one that HAS a baud rate -- and the only one that can refuse.
    void programLine(StrapSerialChannel& ch);

    std::vector<StrapSerialChannel> chans_;
    bool                            boardLevelProps_ = true;
    std::vector<std::string>        log_;
};

} // namespace altair
