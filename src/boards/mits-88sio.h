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

#include "chips/uart1602.h"  // the COM2502 itself -- A CHIP IS NOT A CARD (DESIGN.md 7.8)
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
//
// NOTE WHAT THIS IS A PROPERTY OF: the BOARD. Two revisions of a card, with the
// same chip on both. Nothing about the COM2502 changed between them -- what changed
// is which of its pins are wired to which bits of the bus, and whether they pass
// through an inverter on the way. That is why it lives here and not in the chip.
enum class SioRev { Rev0, Rev1 };

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

    // ...and the same for VI0-VI7. Two straps, independently jumpered, so this card
    // can be pulling two lines at once -- which is why the wire is a bitmask.
    uint8_t assertsVi() const override;

    void reset(Reset) override;
    void power() override;
    void pump() override;
    void configChanged() override;

    // SNAPSHOT/RESTORE (DESIGN.md 13). The UART's state plus the two interrupt-enable
    // flip-flops on the card (IC B). The straps and the port are config. The ACR
    // extends this. deserialize() re-arms via refresh() from the restored state.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // One UART. The ACR inherits this board and this override with it -- a cassette
    // arriving is traffic too. (Board::rxBytes.)
    uint64_t rxBytes() const override { return u_.rxBytes(); }

    // What the real serial port said when the card tried to program its straps into
    // it. A cable that cannot do 7E2 is a fact about the world, and it is said out
    // loud rather than swallowed.
    std::vector<std::string> drainLog() override { return u_.drainLog(); }

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

    // PLUG A LINE INTO THE CONNECTOR. The chip owns the stream (and wraps it in the
    // transform chain, DESIGN.md 7.2) -- this is the card's connector, handing it down.
    void attachStream(std::unique_ptr<ByteStream> s) { u_.connect(std::move(s)); }
    std::string endpoint() const { return u_.endpoint(); }

    // The connector, for an operator that owns the endpoint (the MCP console).
    // Non-owning; the UART owns the stream. See Board::unitStream.
    ByteStream* unitStream(const std::string& unit) override {
        return unit == "tty" ? &u_.stream() : nullptr;
    }

    // ---- The pins, so a test can look at them without going through the bus. ----
    uint8_t statusByte() const;
    bool    dataAvailable() const { return u_.dataAvailable(); }
    bool    txBufferEmpty() const;

    // ---- PROTECTED, FOR ONE CARD, AND THAT CARD IS THE 88-ACR ------------------
    //
    // The 88-ACR is not *like* an 88-SIO. It IS one: "The 88-ACR consists of two
    // separate PC boards mated to each other... One of these is the ACR Modem Board
    // and the other is the 88-SIO B, Serial TTL level I/O Board." The ACR manual then
    // REPRINTS this card's documentation as its own assembly chapter, and its Bit
    // Definition table is this card's status word, Rev 1, bit for bit.
    //
    // So AcrBoard derives from this one (see boards/mits-88acr.h), and the status word
    // below is written ONCE. That is not the "shared UART helper with a bool invert"
    // that the .cpp warns about at length -- that trap is about the 88-2SIO, a
    // DIFFERENT card with a DIFFERENT chip whose bits are the other way up. The trap
    // HERE is the opposite one: two copies of the SAME PCB, which drift the day
    // somebody fixes a bug in one status word and not the other, and then the machine
    // has two different 88-SIO Bs in it.
    //
    // The modem changes nothing on this side of the connector. It is an analog FSK
    // pair -- 2400 Hz for a 1, 1850 Hz for a 0 -- hung off the UART's serial pins, and
    // the guest cannot observe one thing about it.
protected:
    // Everything that could have moved pin 73 has just happened: advance the
    // receiver, re-drive the pin, and set the alarm clock for the next moment the
    // UART could move it with nobody touching the card (DESIGN.md 7.5).
    void refresh();

    // The next T-state at which THE CARD's interrupt request could move on its own.
    // Zero means never. ALWAYS STRICTLY IN THE FUTURE (see Mc6850::nextEdge).
    //
    // This is the card's and not the chip's, and the reason is the whole chip/board
    // split in one function: the COM2502 has no interrupt pin and no interrupt
    // enables. It just reports when its registers next change (u_.txFreeAt(),
    // u_.rxNextAt()); the two enable flip-flops that decide whether anybody CARES are
    // a separate IC on this card, and so is the wire to pin 73.
    uint64_t nextEdge() const;

    // The two conditions, before the interrupt enables and before the straps.
    bool rxReady() const { return u_.dataAvailable(); }
    bool txReady() const { return txBufferEmpty(); }

    // ---- THE UART. One COM2502 (src/chips/uart1602.h). ----
    //
    // It owns the line, the receive register, Data Available, the transmit deadline
    // and the word-format pins. What it does NOT own is anything on the following
    // list, and every item on it is a fact about this CARD rather than about the chip:
    // the inverted status bits, where those bits sit in the byte, the Rev0/Rev1
    // difference, the interrupt enables, the strap to pin 73, and the port decode.
    Uart1602 u_{"tty"};

    // ---- Jumpers. Every one of these is a soldered pad on the real card. ----
    //
    // (The word-format pads -- NDB1/NDB2, NSB, NPB/POE -- are ON THE CHIP, as
    // `u_.dataBits` and friends, because they are pins on the chip. The `properties()`
    // below still present them as card jumpers, which is what they are to an operator
    // holding a soldering iron.)
    uint8_t base_ = 0x00;   // even. Control at base_, data at base_+1.
    SioRev  rev_  = SioRev::Rev1;

    // The "IN" and "OUT" pads. TWO straps, not one mode -- the assembly manual is
    // explicit that the input device and the output device may be jumpered to
    // DIFFERENT VI priorities. Strapping both to the same place is what the "BH"
    // (both) pad is for, and it is electrically identical.
    IrqJumper inIrq_  = IrqJumper::None;
    IrqJumper outIrq_ = IrqJumper::None;

    // ---- Software state: the ONE thing an OUT to the control channel can set. ----
    //
    // These are IC B on the card, not anything inside the UART. The COM2502 has no
    // interrupt pin at all -- the card derives its two requests from RDA and TBMT and
    // gates them with these.
    bool inIntEnabled_  = false;  // control D0
    bool outIntEnabled_ = false;  // control D1

    Clock::Handle wake_ = Clock::kNone;
};

} // namespace altair
