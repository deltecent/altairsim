#pragma once
//
// 88-SIO -- MITS Serial I/O Board. ONE serial port, one COM2502 UART.
// See docs/boards/mits-88sio.md.
//
// THE CARD THAT INVERTS ITS STATUS BITS. The 88-2SIO's status register reads
// TRUE (bit set = condition true); the 88-SIO's ready bits read INVERTED (bit
// CLEAR = ready). A machine can have both cards in it, both conventions live at
// once, and a driver written for one silently misbehaves on the other. They share
// no code, and that is on purpose -- see the comment over statusByte().
//
// It is not a 6850 in any respect. There is no control register in the 6850 sense:
// the only thing software can write to the control channel is the two INTERRUPT
// ENABLES. Word format and baud are PADS ON THE PCB -- soldered, not programmable
// -- which is why they are board properties here and control-register bits on the
// 2SIO.
//
// Two ports: a control/status channel at an EVEN address and a data channel at the
// odd address above it. Any even address from 000 to 376 octal.

#include "core/board.h"
#include "host/filter.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

// The two board revisions (Patrick, 2026-07-12).
//
// Rev 1 IS the errata modification done at the factory. The errata sheet in the
// manual -- "MODIFICATION FOR INTERNAL HARDWARE INTERRUPT (for devices with no
// external handshake capability) ... THIS MODIFICATION APPLIES TO REVISION 0
// BOARDS ONLY" -- is what a Rev 1 board already has, and it redefines the status
// word. That is the whole of the difference we model. See statusByte().
enum class SioRev { Rev0, Rev1 };

// Word format. PADS, not registers: NDB1/NDB2 select 5-8 data bits, NSB selects
// 1 or 2 stop bits, NPB/POE select no/odd/even parity (assembly manual, "Hardwire
// Connections"). They exist here for exactly one reason -- they set how long a
// character occupies the line, and therefore every deadline this card sets.
enum class Parity { None, Odd, Even };

class SioBoard : public Board {
public:
    SioBoard();
    ~SioBoard() override;

    std::string type() const override { return "sio"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    // PIN 73, combinational and pure (DESIGN.md 4.4.1). What the UART is asking
    // for, ANDed with what software enabled, filtered by what is soldered to the
    // wire.
    bool assertsInt() const override;

    void reset(Reset) override;
    void power() override;
    void pump() override;
    void configChanged() override;

    std::vector<Property> properties() override;
    std::vector<UnitDef>  units() const override;
    std::vector<MapEntry> ioMap() const override;

    bool connect(const std::string& unit, const std::string& endpoint,
                 std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;

    // The monitor resolves an endpoint string to a stream; the BOARD is not allowed
    // to know what a socket is (DESIGN.md 7.7).
    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;
    static void setResolver(EndpointResolver r);

    // PLUG A LINE INTO THE CONNECTOR. Every endpoint gets the transform chain
    // (DESIGN.md 7.2) -- including the null one, so `filter_` is never dangling and
    // the filter properties never vanish from SHOW just because nothing is plugged
    // in. This is the one place the stream is ever replaced.
    void attachStream(std::unique_ptr<ByteStream> s);
    std::string endpoint() const { return stream_->describe(); }

    // ---- The pins, so a test can look at them without going through the bus. ----
    uint8_t statusByte() const;
    bool    dataAvailable() const { return dav_; }
    bool    txBufferEmpty() const;

private:
    // Everything that could have moved pin 73 has just happened: advance the
    // receiver, re-drive the pin, and set the alarm clock for the next moment the
    // UART could move it with nobody touching the card (DESIGN.md 7.5).
    void refresh();

    // Take a character off the line if one has finished arriving and the receive
    // register is free to hold it. Same contract as the 2SIO's Acia::poll(), and
    // the same hard-won rule behind it: a ByteStream is NOT a serial line, so this
    // never synthesizes an overrun.
    void pollRx();

    // The next T-state at which the UART's interrupt request could move on its own.
    // Zero means never. ALWAYS STRICTLY IN THE FUTURE (see Acia::nextEdge).
    uint64_t nextEdge() const;

    int      bitsPerChar() const;
    uint64_t charTStates() const;

    // The two conditions, before the interrupt enables and before the straps.
    bool rxReady() const { return dav_; }
    bool txReady() const { return txBufferEmpty(); }

    std::unique_ptr<ByteStream> stream_;
    FilterStream*               filter_ = nullptr;  // borrowed; owned by stream_

    // ---- Jumpers. Every one of these is a soldered pad on the real card. ----
    uint8_t  base_   = 0x00;   // even. Control at base_, data at base_+1.
    SioRev   rev_    = SioRev::Rev1;
    long long baud_  = 9600;
    int      dataBits_ = 8;    // NDB1/NDB2
    int      stopBits_ = 1;    // NSB
    Parity   parity_ = Parity::None;  // NPB/POE

    // The "IN" and "OUT" pads. TWO straps, not one mode -- the assembly manual is
    // explicit that the input device and the output device may be jumpered to
    // DIFFERENT VI priorities. Strapping both to the same place is what the "BH"
    // (both) pad is for, and it is electrically identical.
    IrqJumper inIrq_  = IrqJumper::None;
    IrqJumper outIrq_ = IrqJumper::None;

    // ---- Software state: the ONE thing an OUT to the control channel can set. ----
    bool inIntEnabled_  = false;  // control D0
    bool outIntEnabled_ = false;  // control D1

    // ---- The UART. ----
    uint8_t  rxData_ = 0;
    bool     dav_    = false;  // Data Available (the RDAV flip-flop)
    uint64_t txFreeAt_ = 0;    // TBMT is a DEADLINE, not a flag (DESIGN.md 7.5)
    uint64_t rxNextAt_ = 0;

    Clock::Handle wake_ = Clock::kNone;
};

} // namespace altair
