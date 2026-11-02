#pragma once
//
// CompuPro Interfacer 1 (Godbout 133H, 1980-81) -- two serial channels on two 1602-family
// UARTs. See docs/boards/compupro-interfacer1.md.
//
// THIS IS NOT THE gsio STRAP BOARD. `gsio` with `profile = "compupro-if2"` describes the
// console the CompuPro BIOS polls -- DAV on D1, TBMT on D0 -- and nothing more. This board is
// the real card, modeled from its manual and schematic: two UARTs (U11 = Channel A,
// U18 = Channel B; "1602, AY-5-1013 OR SIMILAR"), each with
//
//   * ITS OWN ADDRESS. DIP switches S2 (A) and S3 (B) decode A7-A1, so each channel is a
//     2-port block at any even address, and switch position 8 takes it off the bus. Data at
//     the base and status at base+1, unless the J14 trace is moved (`swap_ports`), which
//     swaps the pair on BOTH channels at once.
//
//   * A CONTROL PORT THAT FLIPS STRAPS. An OUT to the status address latches a byte into two
//     74LS175s. Each latch bit is wired through a jumper socket (J7/J9 for A, J8/J10 for B) in
//     which the owner cut one of two traces, so the latch output reaches the signal either
//     straight or inverted. Net effect, in the manual's words: "outputting a '1' to any of the
//     control bits, the particular control line's level will be inverted", and a '0' returns
//     it to the power-up level. So the live level of every control line is STRAP XOR LATCH,
//     and POC/pRESET clears the latches back to the straps. The eight lines are the UART's
//     word format (NBI, EPS, NP, TSB), the two RS-232 outputs (CA, CD) and the two interrupt
//     enables (TXINTE, RXINTE).
//
//   * A FIXED STATUS BYTE. D0 TBMT, D1 DAV, D2 OPT, D3 PE, D4 OR, D5 FE, D6 CC, D7 CB --
//     the UART's status pins (SWE grounded, always on) and three RS-232 inputs, through the
//     81LS95 buffers U27/U28, which do not invert. The RS-232 inputs come in through 1489
//     receivers (U2, U6), which DO invert, so an asserted modem line reads 0.
//
//   * FOUR INTERRUPT SOURCES. RxINT = DAV AND RXINTE, TxINT = TBMT AND TXINTE (74LS08 U16),
//     each through an open-collector inverter (U29) to socket J11, which reaches VI0-VI7 and
//     nothing else -- J11 has no pin-73 position. Default: not jumpered.
//
// The UART is the existing Uart1602 (src/chips/uart1602.h) -- A CHIP IS NOT A CARD
// (DESIGN.md 7.8). The board owns the latches, the straps, the status byte, the modem lines,
// the interrupt gates and the decode.

#include "chips/uart1602.h"
#include "core/board.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class InterfacerOneBoard : public Board {
public:
    InterfacerOneBoard();
    ~InterfacerOneBoard() override;

    std::string type() const override { return "interfacer1"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    // J11 reaches VI0-VI7 only, so this board never pulls pin 73.
    bool    assertsInt() const override { return false; }
    uint8_t assertsVi() const override;

    void reset(Reset) override;
    void power() override;
    void pump() override;
    void configChanged() override;

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    uint64_t rxBytes() const override { return a_.uart.rxBytes() + b_.uart.rxBytes(); }
    std::vector<std::string> drainLog() override;

    std::vector<Property> properties() override;
    std::vector<Property> unitProperties(const std::string& unit) override;
    std::vector<UnitDef>  units() const override;
    std::vector<MapEntry> ioMap() const override;

    bool connect(const std::string& unit, const std::string& endpoint, std::string& err) override;
    bool connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                       std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;
    ByteStream* unitStream(const std::string& unit) override;

    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;
    static void setResolver(EndpointResolver r);

    // The eight control lines, in latch-bit order (the manual's Control Port table).
    enum Ctl : uint8_t {
        RxIntE = 0x01,  // D0 receiver interrupt enable
        TxIntE = 0x02,  // D1 transmitter interrupt enable
        CdOut  = 0x04,  // D2 RS-232 CD (DTR) output: 1 = marking (OFF)
        CaOut  = 0x08,  // D3 RS-232 CA (RTS) output: 1 = marking (OFF)
        Tsb    = 0x10,  // D4 1 = two stop bits
        Np     = 0x20,  // D5 1 = no parity
        Eps    = 0x40,  // D6 1 = even parity
        Nbi    = 0x80,  // D7 1 = eight data bits
    };

    // Test access to one channel's live control level (strap XOR latch).
    uint8_t controlLevel(const std::string& unit) const;

    // The four control lines that make the UART's word format; a change reprograms the line.
    static constexpr uint8_t kFormat = Nbi | Eps | Np | Tsb;

private:
    struct Channel {
        explicit Channel(const char* n) : uart(n) {}
        Uart1602  uart;
        uint8_t   base    = 0x00;  // S2/S3 A7-A1: the 2-port block
        bool      enabled = true;  // S2/S3 position 8 OFF
        uint8_t   strap   = 0x00;  // the kept traces in J7/J9 (J8/J10): the power-up levels
        uint8_t   latch   = 0x00;  // the 74LS175s: cleared by POC/pRESET
        bool      optEoc  = false; // J12/J13: B-A = CF input (false), B-C = UART EOC (true)
        IrqJumper rxIrq   = IrqJumper::None;  // J11: RxINT
        IrqJumper txIrq   = IrqJumper::None;  // J11: TxINT
        uint8_t   level() const { return (uint8_t)(strap ^ latch); }
    };

    Channel*       channel(const std::string& unit);
    const Channel* channel(const std::string& unit) const;
    bool           decodePort(uint8_t port, Channel*& ch, bool& isData) const;

    uint8_t status(Channel& ch);
    void    applyLevel(Channel& ch);  // the latch moved: re-pin the UART and the modem lines
    void    driveModemLines(Channel& ch);
    void    programChannel(Channel& ch);

    std::vector<Property> channelProperties(Channel& ch);

    void     refresh();
    uint64_t nextEdge() const;

    Channel a_{"a"};
    Channel b_{"b"};
    bool    swapPorts_     = false;  // J14: status at the base, data at base+1
    bool    overlapWarned_ = false;  // the a/b overlap warning was already logged

    Clock::Handle            wake_ = Clock::kNone;
    std::vector<std::string> log_;
};

} // namespace altair
