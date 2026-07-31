#pragma once
//
// TMS 5501 -- Texas Instruments' "NMOS I/O Controller": a UART, five interval
// timers, and an eight-source priority interrupt controller on one chip.
//
// A CHIP, NOT A CARD. The Cromemco 4FDC/16FDC/64FDC floppy controllers each carry
// one (wired as a single console channel), and the Cromemco TU-ART carries two.
// Modeled from the reference distilled in reference/Cromemco TU-ART.md and
// reference/Cromemco 4FDC 16FDC 64FDC Floppy Controllers.md -- NOT from the one
// RDOS monitor that happens to drive it, which is how you end up implementing the
// subset that monitor uses and quietly getting the rest wrong.
//
// It knows nothing about S-100. It has a clock, some pins, and a ByteStream.
//
// ---------------------------------------------------------------------------
// PHASE 1 IS SERIAL ONLY, AND SAID OUT LOUD.
//
// A polled RDOS/CDOS console boot needs the UART half and nothing else: status
// (TBE/RDA), the one-hot baud register, the data registers, and a command register
// whose RES bit actually resets the chip. So that half is modeled to the letter.
//
// The other two-thirds of the 5501 -- the five interval timers and the eight-source
// priority interrupt controller (the interrupt-address register at port 03, the mask,
// the RS7/DRQ/RTC routing) -- are present as INERT STUBS: the ports exist and answer,
// but no timer ever counts, no interrupt is ever raised (irq() is always false), and
// the board's assertsInt() returns false. This is a deliberate, documented deferral
// (see the plan and DESIGN.md 0.1), not an oversight -- an interrupt-driven CDOS BIOS
// is a later effort, and inventing timer/interrupt behavior no test exercises would be
// exactly the kind of guess the design forbids.
// ---------------------------------------------------------------------------

#include "core/board.h"     // IrqJumper, irqJumperProperty, Property, PinStrap-free
#include "chips/mc6850.h"   // PinStrap, EndpointResolver -- shared serial vocabulary

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

// ---------------------------------------------------------------------------
// One TMS 5501. The TU-ART has two and they share NOTHING; an FDC board has one,
// hard-wired at base 00. Modeling them as one object with an index would be
// modeling the PCB, not the chips -- so this is the chip, and the card owns as
// many as it has.
// ---------------------------------------------------------------------------
class Tms5501 {
public:
    explicit Tms5501(std::string name) : name_(std::move(name)) {}

    const std::string& name() const { return name_; }

    // ---- THE REGISTER FILE (ports 00-09; the card maps the block) ----
    //
    //   IN  00 status        OUT 00 baud rate
    //   IN  01 rx data       OUT 01 tx data
    //   IN  02 --            OUT 02 command  (RES/BRK/RS7/INE/HBD/TB5)
    //   IN  03 int address   OUT 03 int mask
    //   IN  04 parallel in   OUT 04 parallel out
    //       05-09            OUT 05-09 timers 1-5
    //
    // Note the shape differs from a 6850/8251: read and write at offset 0 are
    // DIFFERENT registers (status vs baud), and the command register is its own
    // write-only port at offset 2 -- not the "one address, read vs write" shape.
    uint8_t readStatus(const Clock& clk);
    uint8_t readData(const Clock& clk);
    void    writeBaud(uint8_t v);
    void    writeData(uint8_t v, const Clock& clk);
    void    writeCommand(uint8_t v, const Clock& clk);

    // ---- INERT STUBS (Phase 1). Present so the port block decodes, but nothing
    // behind them counts or interrupts. See the class note. ----
    uint8_t readIntAddr() const { return 0xFF; }        // "no source pending"
    void    writeMask(uint8_t v) { mask_ = v; }          // stored, does nothing
    uint8_t readParallel() const { return 0xFF; }        // no parallel input strapped
    void    writeParallel(uint8_t v) { parallelOut_ = v; }
    void    writeTimer(int idx, uint8_t v);              // stored, never armed

    // POWER-ON-CLEAR. The 5501 has a RESET* pin (unlike the 6850), but at power-on
    // we just come up in a known-good state at once so the card is usable the instant
    // the machine comes up (DESIGN.md 6.1): baud at the strap, transmitter ready,
    // receiver empty, endpoint still connected.
    void powerOn(const Clock& clk);

    // BUS RESET (RESET*, the front-panel switch). The card decides whether it reaches
    // the chip; the FDC boards wire it so RESET* clears the chip like the command-RES
    // does. Same body as the command register's RES bit.
    void reset(const Clock& clk) { resetAction(clk); }

    // The chip's INT pin. ALWAYS FALSE in Phase 1 -- no timer counts and no receive/
    // transmit interrupt is wired yet. Kept so the board's assertsInt() has one place
    // to ask, and so the interrupt PR has one function to make real.
    bool irq(const Clock& clk) const { (void)clk; return false; }

    // The card's straps for the modem input pins the 5501 has. (It has more pins than
    // a 6850, but a console channel wires only these; the rest are inert in Phase 1.)
    PinStrap dcdStrap = PinStrap::Ground;
    PinStrap ctsStrap = PinStrap::Ground;

    void connect(std::unique_ptr<ByteStream> s);
    void disconnect();
    ByteStream&  stream()  { return *stream_; }
    std::string  endpoint() const { return stream_->describe(); }

    // Bytes handed to the guest since power-on, monotonic -- the run loop's live-traffic
    // signal (the same role Mc6850::rxBytes plays; see board.h rxBytes()).
    uint64_t rxBytes() const { return rxCount_; }

    // The `connect` property turns an endpoint string into a stream. The resolver comes
    // in from the card because the chip is not allowed to know the grammar (DESIGN.md 7.7).
    std::vector<Property> properties(const EndpointResolver& resolve);
    void pump() { stream_->pump(); }

    long long baud() const { return baud_; }

    // Push the strapped/programmed baud and the fixed 8-data-bit frame at the wire.
    // Matters to exactly one endpoint (a real serial port); every other ignores it.
    void programLine();

    // Live pin state, strap applied -- for SHOW and the board.
    bool carrier() const;
    bool clearToSend() const;

    // Drain what the chip has to say (a rate the host refused). Cleared by draining.
    std::vector<std::string> drainLog();

    // ADVANCE THE RECEIVER. Take a character off the line if one has finished arriving
    // and the register is free. Public because the card calls it on its own schedule
    // (pump(), a deadline, a register access) -- see Mc6850::poll.
    void poll(const Clock& clk);

    // The next moment this chip's pins could move with nobody touching it. Zero means
    // never. In Phase 1 there is no interrupt, so this only ever reports the receive/
    // transmit pacing deadlines a poll would resolve anyway -- returns 0.
    uint64_t nextEdge(const Clock& clk) const { (void)clk; return 0; }

    // SNAPSHOT/RESTORE (DESIGN.md 13). The owning card calls these. The live chip
    // state travels; the straps (dcdStrap/ctsStrap) and the stream_ (a host handle,
    // re-CONNECTed) do not.
    void serialize(StateWriter& w) const;
    void deserialize(StateReader& r);

private:
    // How long one character occupies the line, in T-states. 5501 frame: 1 start bit,
    // 8 data bits (fixed), no parity, 1 or 2 stop bits (baud register D7).
    uint64_t charTStates(const Clock& clk) const;
    int      bitsPerChar() const { return 1 + 8 + stopBits_; }
    LineParams params() const;

    // Is the transmit register empty? Not just "has the character had time to leave":
    // /CTS negated inhibits it, and the endpoint must have somewhere to put the byte.
    bool tbe(const Clock& clk) const;

    // The live /CTS pin, strap applied. Only poll() may call it; everything else reads
    // the sample (ctsPin_) so assertsInt() stays pure. See Mc6850::ctsNow.
    bool ctsNow() const;

    // Drive RTS/DTR/BREAK at the endpoint. The 5501's command register has BRK (D1);
    // RTS/DTR are not part of a console channel here.
    void driveControl();

    // What a RES (command D0) / RESET* / power-on actually does: receiver to search
    // mode, TX to mark, RDA/ORE cleared, TBE set, timers cleared. Does NOT touch the
    // baud strap or the command byte's INE/HBD (those are separate from the RES strobe).
    void resetAction(const Clock& clk);

    std::string name_;

    // THE LINE, RAW. No transform chain -- that belongs to the console (DESIGN.md 7.2).
    std::unique_ptr<ByteStream> stream_;

    // The programmed line rate, in bit/s, already including the HBD (command D4) x8
    // multiplier. Seeded from a strap (a machine file may set `baud`), then overwritten
    // whenever the guest writes the one-hot baud register -- which RDOS's "Initialize
    // Baud Rate" does at boot. 0 means the baud register selected no rate (serial
    // disabled); charTStates treats that as instantaneous, and a console left there
    // never paces -- but a guest programs a real rate before using the line.
    long long baud_        = 9600;
    long long baudBase_    = 9600;   // the one-hot rate BEFORE the HBD multiplier
    int       stopBits_    = 1;      // baud register D7: 1 => one stop bit, 0 => two
    bool      hbd_         = false;  // command register D4: octuple the rate

    uint8_t command_    = 0;   // last command byte (INE/HBD/RS7 survive; RES/BRK are strobes)
    uint8_t mask_       = 0;   // interrupt mask (inert)
    uint8_t parallelOut_= 0;   // parallel output latch (inert)
    uint8_t timer_[5]   = {};  // timer load values (inert -- never counted)

    uint8_t rxData_  = 0;
    uint64_t rxCount_ = 0;
    bool     rdrf_   = false;  // RDA: a byte is waiting
    bool     ovrn_   = false;  // ORE: a byte overwrote one not yet read

    // The pins are SAMPLED, not peeked at -- refreshed in poll(), read everywhere
    // downstream, so assertsInt() stays pure and replay deterministic (Mc6850 note).
    bool ctsPin_ = true;
    bool txRoom_ = true;

    std::vector<std::string> log_;

    // TBE/RDA are DEADLINES, not flags (DESIGN.md 7.5). The transmit register is empty
    // once the character has had time to leave; receive is paced too, so a byte cannot
    // arrive "while the last one was still sitting there" (which is what an overrun is).
    uint64_t txFreeAt_ = 0;
    uint64_t rxNextAt_ = 0;
};

} // namespace altair
