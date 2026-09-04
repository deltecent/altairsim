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
// one S-100 card. This file is PHASE 1: the serial section only -- two real UART
// channels with programmable word length / parity / stop bits, at a 4-port block set
// by switch S3. The status-word strapping (headers W1/W2), the parallel section
// (8212 ports, switch S4) and the interrupts (header W4) arrive in later phases.
//
// The serial section answers as a 4-PORT BLOCK on a 4-port boundary (S3 decodes
// A7-A2 -- one switch for the whole section, so `port` is a BOARD property, not a
// per-channel one). In the default 0-3 layout: Serial A status/data at 0/1, Serial B
// at 2/3. Each channel's word format (S1 = Serial B, S2 = Serial A on the real card)
// is a per-unit strap: `[board.unit.a]` / `[board.unit.b]`.
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
    uint64_t rxBytes() const override { return a_.rxBytes() + b_.rxBytes(); }

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

private:
    // The UART a port belongs to, and whether it is the data port (vs status/control).
    // False if the port is not ours.
    bool decodePort(uint8_t port, Uart1602*& u, bool& isData) const;

    // The channel named by a unit ("a"/"b"), or null.
    Uart1602*       channel(const std::string& unit);
    const Uart1602* channel(const std::string& unit) const;

    // The status byte the CPU reads for a channel. PHASE 1: a fixed MITS-SIO-Rev-0
    // (`sior0`) map -- DAV to bit 0, TBMT to bit 7, both active low -- which is what the
    // SSM 8080 System Monitor expects on its console. The general W1/W2 strap map lands
    // in Phase 2.
    uint8_t statusByte(const Uart1602& u) const;

    // The straps/format/connect properties for ONE channel, captured by pointer (members
    // never move). Shared by unitProperties("a") and ("b").
    std::vector<Property> channelProperties(Uart1602& u);

    // Push a channel's word-format straps at a real serial port (ignored by every other
    // endpoint); collect any refusal into the board log.
    void programChannel(Uart1602& u);

    // ---- THE TWO UARTS. U9 = Serial A, U8 = Serial B. ----
    Uart1602 a_{"a"};
    Uart1602 b_{"b"};

    // ---- Switch S3: the 4-port block base (A7-A2), snapped to a 4-boundary. ----
    uint8_t base_ = 0x00;  // A at base_+0/+1, B at base_+2/+3

    std::vector<std::string> log_;
};

} // namespace altair
