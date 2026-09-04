#pragma once
//
// SSM IO-4 (2P + 2S) -- the real, fully-emulated Solid State Music I/O board.
// See docs/manual/boards.md and reference/SSM IO-4 2P+2S IO Board.md.
//
// THIS IS NOT THE gsio STRAP BOARD. `gsio` (src/boards/gsio.h) is the generic
// chip-LESS strap-serial engine -- "describe a status bit, read/write a data byte".
// THIS card is the opposite: a specific 1970s product modeled register- and
// pin-for-pin, built on the real 1602-family UART (src/chips/uart1602.h) the board
// actually carried (U9 = Serial A, U8 = Serial B; a TMS6011 / AY5-1013 / TR-1602).
//
// The IO-4 puts two full-duplex serial channels AND a four-port parallel section on
// one S-100 card. This file is the SERIAL section: two real UART channels with
// programmable word length / parity / stop bits, at a 4-port block set by switch S3,
// plus the full status-word strap-up (headers W1/W2) that lets each channel imitate
// almost any other card's status port. The parallel section (8212 ports, switch S4)
// and the interrupts (header W4) arrive in later phases.
//
// The serial section answers as a 4-PORT BLOCK on a 4-port boundary (S3 decodes
// A7-A2 -- one switch for the whole section, so `port` is a BOARD property, not a
// per-channel one). In the default 0-3 layout: Serial A status/data at 0/1, Serial B
// at 2/3. Each channel's word format (S1 = Serial B, S2 = Serial A on the real card)
// is a per-unit strap: `[board.unit.a]` / `[board.unit.b]`.
//
// THE STATUS PORT IS SHAPED, NOT FIXED. Six UART status signals (DAV, ROR, RPE, RFE,
// TEOC, TBMT) come to a 16-pin header (W2 = Serial A, W1 = Serial B) and can be jumpered
// to ANY data-bus bit, in either polarity (the status buffer is a 74367 for positive
// sense or a 74368 for negative -- U18 = Serial A, U16 = Serial B). The two port
// addresses of a channel can also be swapped (S1/S2-PR). Each channel therefore carries
// a status map, a polarity bit and a port-reversal bit, and a `profile` selector presets
// all three from a documented host personality (the SSM 8080 monitor, an 8251, an Altair
// SIO, a Processor Technology or IMSAI port). The default profile is `altair-rev1` -- the
// strapping the SSM 8080 System Monitor's console expects, which is why a stock io4 boots it.
//
#include "chips/uart1602.h"  // the 1602-family UART -- A CHIP IS NOT A CARD (DESIGN.md 7.8)
#include "core/board.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class Io4Board : public Board {
public:
    Io4Board();
    ~Io4Board() override;

    std::string type() const override { return "io4"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    void reset(Reset) override;
    void power() override;
    void pump() override;
    void configChanged() override;

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // Both channels sum into the run loop's live-traffic signal (Board::rxBytes).
    uint64_t rxBytes() const override { return a_.uart.rxBytes() + b_.uart.rxBytes(); }

    // What either wire said when the card tried to program its straps into it.
    std::vector<std::string> drainLog() override;

    std::vector<Property> properties() override;
    std::vector<Property> unitProperties(const std::string& unit) override;
    std::vector<UnitDef>  units() const override;
    std::vector<MapEntry> ioMap() const override;

    bool connect(const std::string& unit, const std::string& endpoint, std::string& err) override;
    // Install a PRE-BUILT stream (the --mcp console's filtered scripted line) on a named
    // channel. Body out-of-line (.cpp): a by-value unique_ptr<ByteStream> needs the complete
    // type to destroy, and MSVC instantiates the deleter at the declaration otherwise.
    bool connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                       std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;
    ByteStream* unitStream(const std::string& unit) override;

    // The monitor resolves an endpoint string to a stream; the BOARD never learns what a
    // socket is (DESIGN.md 7.7). Installed once in each main.
    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;
    static void setResolver(EndpointResolver r);

    // The six UART status signals brought to the W1/W2 status header, in header-pin order.
    // Each may be jumpered to any data-bus bit or left unconnected. PUBLIC so the profile
    // table in io4.cpp can name them (Io4Board::Dav ...); `kNumStat` is the count and the
    // status-map array size.
    enum StatSig { Dav, Ror, Rpe, Rfe, Teoc, Tbmt, kNumStat };

private:
    // ---- ONE SERIAL CHANNEL: a real UART plus the straps that shape its status port. ----
    // U9 = Serial A, U8 = Serial B. The UART is TRUE SENSE; everything that makes the status
    // byte look like some other card (the map, the polarity, the address order) is the CARD's
    // and lives out here (DESIGN.md 7.8), exactly as the 88-SIO's inversion does.
    struct SerialChannel {
        explicit SerialChannel(const char* n) : uart(n) {}
        Uart1602 uart;
        // W1/W2: each status signal -> a data-bus bit (0-7), or -1 = not jumpered (drives
        // no bit). Indexed by StatSig.
        int  statBit[kNumStat] = {-1, -1, -1, -1, -1, -1};
        // U16/U18: a 74368 (negative sense) inverts every driven status bit; a 74367
        // (positive sense) does not. ONE polarity for the whole channel's status byte.
        bool invert = false;
        // S1/S2-PR: swap the status and data port addresses within this channel.
        bool portReversal = false;
    };

    // The channel a port belongs to, and whether it is the DATA port (vs status/control),
    // honoring that channel's port-reversal strap. False if the port is not ours.
    bool decodePort(uint8_t port, SerialChannel*& ch, bool& isData) const;

    // The channel named by a unit ("a"/"b"), or null.
    SerialChannel*       channel(const std::string& unit);
    const SerialChannel* channel(const std::string& unit) const;

    // The status byte the CPU reads for a channel: compose the mapped signals onto their
    // data bits, each XORed against the channel's polarity (74368 inverts). An unjumpered
    // bit reads 0.
    uint8_t statusByte(const SerialChannel& ch) const;

    // Preset a channel's status map + polarity + port-reversal from io4Profiles()[idx]; and
    // the inverse -- the name of the profile the current straps match, or "custom".
    void        applyProfile(SerialChannel& ch, int idx);
    std::string profileName(const SerialChannel& ch) const;

    // The straps/format/connect properties for ONE channel, captured by pointer (members
    // never move). Shared by unitProperties("a") and ("b").
    std::vector<Property> channelProperties(SerialChannel& ch);

    // Push a channel's word-format straps at a real serial port (ignored by every other
    // endpoint); collect any refusal into the board log.
    void programChannel(SerialChannel& ch);

    // ---- THE TWO CHANNELS. U9 = Serial A, U8 = Serial B. ----
    SerialChannel a_{"a"};
    SerialChannel b_{"b"};

    // ---- Switch S3: the 4-port block base (A7-A2), snapped to a 4-boundary. ----
    uint8_t base_ = 0x00;  // A at base_+0/+1, B at base_+2/+3

    std::vector<std::string> log_;
};

} // namespace altair
