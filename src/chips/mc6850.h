#pragma once
//
// MC6850 ACIA -- Motorola's Asynchronous Communications Interface Adapter.
//
// A CHIP, NOT A CARD. The 88-2SIO has two of these on it (src/boards/mits-2sio.h);
// the next card to turn up with one gets it for free, and gets it with the DCD
// latch and the CTS-inhibits-TDRE rule already right. Modeled from the MC6850 data
// sheet (reference/6850.pdf) -- NOT from the one BIOS that happens to drive it,
// which is how you end up implementing the subset that BIOS uses and quietly
// getting the rest wrong.
//
// It knows nothing about S-100. It has a clock, some pins, and a ByteStream.

#include "core/board.h"     // IrqJumper, irqJumperProperty, Property -- pin 73's vocabulary
#include "host/filter.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

// TURNING `socket:2323` INTO A STREAM IS THE MONITOR'S JOB, NOT THE CHIP'S
// (DESIGN.md 7.7). The chip cannot resolve an endpoint and is not allowed to
// learn how: the program installs the resolver on the BOARD (see
// Sio2Board::setResolver, wired up in src/main.cpp), and the board hands it down
// here so that `SET sio0:a connect=socket:2323` works. What travels is a
// FUNCTION, never the grammar.
using EndpointResolver =
    std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;

// ---------------------------------------------------------------------------
// WHERE A MODEM PIN GOES, AND IT IS A FACT ABOUT THE CARD.
//
// This is the PHANTOM* lesson again: the read/write distinction lived on the
// HONORING board. The 2SIO manual's hardwire table gives CTS and DCD their own
// jumper pads -- whether the 6850's pin reaches the connector or is strapped to
// ground is a property of the CARD, not of whatever is plugged into it, and period
// installers grounded them constantly.
//
// Default GROUND, which is both the period default and the reason every existing
// config keeps working untouched: a grounded pin never asks the far end anything,
// so a card on the console transmits forever and receives forever, exactly as it
// does today.
//
// And it dissolves the "what if there is no real serial port" question entirely.
// There is no unconnected case to handle: an unplugged unit is a NullStream, which
// asserts everything, and a card strapped to `ground` never even looks.
// ---------------------------------------------------------------------------
enum class PinStrap {
    Ground,  // the pin is grounded ON THE CARD: permanently asserted, far end ignored
    Wired,   // the pin reaches the connector: believe what is on the other end
};

// ---------------------------------------------------------------------------
// One 6850. The 2SIO card has two, and they share NOTHING -- separate baud jumpers,
// separate endpoints, separate interrupt straps. Modeling them as one object
// with an index would be modeling the PCB, not the chips.
//
// NAMED FOR THE PART, NOT THE FUNCTION (Patrick, 2026-07-12). It was `Acia`, and
// "ACIA" is a role that half a dozen incompatible chips have filled -- the moment a
// card turns up with a different one, `Acia` is a lie with no room left to tell the
// truth in. This is an MC6850, its behaviour comes from the MC6850 data sheet, and
// the class says so.
// ---------------------------------------------------------------------------
class Mc6850 {
public:
    explicit Mc6850(std::string name) : name_(std::move(name)) {}

    const std::string& name() const { return name_; }

    uint8_t readStatus(const Clock& clk);
    uint8_t readData(const Clock& clk);
    void    writeControl(uint8_t v, const Clock& clk);
    void    writeData(uint8_t v, const Clock& clk);

    // POWER-ON-CLEAR, AND IT IS THE ONLY RESET THAT REACHES THIS CHIP FROM OUTSIDE.
    //
    // THE MC6850 HAS NO RESET PIN. Its 24 pins are Vss, RxD, RxCLK, TxCLK, RTS, TxD,
    // IRQ, CS0-CS2, RS, Vcc, R/W, E, D0-D7, /DCD and /CTS -- that is the whole list.
    // So the S-100 BUS RESET (RESET*, the front-panel switch) has nowhere to land on
    // this chip, and the card must NOT touch it on a bus reset: the control register,
    // the word format, RTS, the interrupt enables and RDRF all survive one, exactly as
    // they do on the bench. See Sio2Board::reset(), which does nothing for Reset::Bus.
    //
    // Do not confuse this with the MASTER RESET, which is the GUEST's -- 11 in the
    // divide field, via writeControl(). That one does NOT clear the control register,
    // it latches the byte it rode in on, and it HOLDS the chip until a second control
    // write (see inReset(), below). It is the only way anything resets a 6850, and it
    // is why every driver ever written does two OUTs.
    //
    // What powerOn() models is the machine being SWITCHED ON. The real chip has an
    // internal power-on reset that holds it until the guest's first master reset
    // releases it; we do not model the holding, because nothing can observe it that
    // does not also program the chip. We just put it in a known good state at once --
    // control register zeroed, receiver empty, transmitter ready, endpoint still
    // connected -- so the card is usable the instant the machine comes up. DESIGN.md 6.1.
    void powerOn(const Clock& clk);

    bool irq(const Clock& clk) const;   // the chip's IRQ pin, jumper or no jumper

    // HELD IN RESET, WHICH IS A STATE AND NOT AN EVENT (MC6850 data sheet).
    //
    // The divide field IS the reset: while CR1:CR0 read 11 the chip is "maintained
    // in the Reset condition", and it STAYS there until the guest writes a second
    // control byte that selects a real divide ratio. That is why every 6850 driver
    // ever written does two OUTs, not one -- ALTMON's `MVI A,3 / OUT 10h` is only
    // half of the sequence, and the machine does not come up until the other half
    // lands.
    //
    // While it holds, the transmitter is inhibited: "The TDRE status bit indicates
    // the current status of the Transmit Data Register except when inhibited by
    // Clear-to-Send being high OR THE ACIA BEING MAINTAINED IN THE RESET CONDITION."
    // So a guest that master-resets and then polls for TDRE without programming the
    // format waits forever, on the real chip and now on this one.
    bool inReset() const;

    IrqJumper jumper = IrqJumper::None;

    // The card's straps for the two modem INPUTS the 6850 actually has pins for.
    // (It has /CTS, /DCD and RTS. It has NO DTR pin and no RI pin -- so this card
    // cannot hang up a phone and cannot hear one ring, and no amount of wanting it
    // to changes what is soldered to the chip. The PMMI is the card with those pins.)
    PinStrap dcdStrap = PinStrap::Ground;
    PinStrap ctsStrap = PinStrap::Ground;

    void connect(std::unique_ptr<ByteStream> s);
    void disconnect();
    ByteStream&  stream()  { return *stream_; }
    std::string  endpoint() const { return stream_->describe(); }

    // `resolve` is how the `connect` property turns an endpoint string into a
    // stream. It comes in from the card because the chip is not allowed to know
    // the grammar -- see EndpointResolver, above.
    std::vector<Property> properties(const EndpointResolver& resolve);
    void pump() { stream_->pump(); }

    long long baud() const { return baud_; }

    // THE CARD PROGRAMS THE WIRE. Push the strapped baud and the guest's word format
    // at the endpoint -- which matters to exactly one endpoint, a real serial port,
    // and is ignored by every other. Called on connect, on a baud restrap, and on
    // every control-register write, because those bits ARE the frame on the wire.
    //
    // Anything the host could not do comes back as a sentence, and the CARD says it
    // out loud (Board::drainLog()). A cable that cannot do 76800 baud is a fact about
    // the world; a card that ran at the wrong speed without mentioning it would be a
    // bug you find with an oscilloscope.
    void programLine();

    // What the pins say, strap applied. For SHOW, and for the board.
    //
    // carrier() is the LIVE pin -- sampleDcd() is what turns it into a latched flag.
    // clearToSend() is the SAMPLE, because assertsInt() reads it and assertsInt() must
    // be pure (see ctsPin_, below).
    bool carrier() const;
    bool clearToSend() const;

    // Drain what the chip has to say (a rate the host refused). Cleared by draining.
    std::vector<std::string> drainLog();

    // ADVANCE THE RECEIVER. Take a character off the line if one has finished
    // arriving and the register is free to hold it.
    //
    // Public, because the CARD has to be able to call it: an interrupt-driven
    // driver never touches a register, so if this only ran on a register read,
    // such a driver would never receive anything at all. The receive shift
    // register fills on the 6850's own clock and owes the CPU nothing, and the
    // interface has to be able to say that.
    void poll(const Clock& clk);

    // THE NEXT MOMENT THIS CHIP'S IRQ PIN COULD MOVE WITH NOBODY TOUCHING IT.
    // Zero means never: nothing will happen here until the guest reads a register
    // or the host puts a character on the line.
    //
    // This is the whole of what the event queue needs to know, and it is the line
    // between a deadline and a poll. We are not asking "has it happened yet?" sixty
    // million times a second. We are saying, once, "wake me AT this T-state" -- and
    // the answer is usually "there is nothing to wake up for", which is the case
    // the poll was paying full price for.
    //
    // ALWAYS STRICTLY IN THE FUTURE. A deadline already past is already showing in
    // irq(); there is nothing left to wake up for, and arming a timer for now()
    // would fire inside the drain loop that is running us, and arm it again, and
    // never stop.
    uint64_t nextEdge(const Clock& clk) const;

private:
    // How long one character occupies the line, in T-states. Falls out of the
    // word-select bits the guest wrote to the control register -- so a guest that
    // configures 8N2 gets an 11-bit character time and one that configures 7E1
    // gets 10, without either of them being special-cased anywhere.
    uint64_t charTStates(const Clock& clk) const;
    int      bitsPerChar() const;

    // The same three word-select bits, read as a FRAME rather than as a bit count --
    // which is what a real serial port needs to be told.
    LineParams params() const;

    // Is the transmit register empty? NOT just "has the character had time to leave":
    //
    //   - /CTS negated INHIBITS TDRE. The data sheet is explicit -- "In the high
    //     state, the Transmit Data Register Empty bit is inhibited" -- and since the
    //     transmit interrupt is derived from TDRE, it inhibits that too. This is real
    //     flow control, and it is the whole reason `cts=wired` is worth having.
    //   - ...and the endpoint has to have somewhere to PUT the byte. A full TCP send
    //     buffer is the same physical situation as a modem holding CTS low, so it
    //     lands in the same bit and the guest simply waits.
    bool tdre(const Clock& clk) const;

    // Sample /DCD and do what the data sheet says a 6850 does with it -- which is a
    // great deal more than set a status bit. See the .cpp.
    void sampleDcd();

    // WHAT A RESET ACTUALLY DOES, minus the control register -- because a master
    // reset does NOT clear the control register (data sheet: "Master reset does not
    // affect other Control Register bits"). The reset is REQUESTED by a control
    // write, and the rest of the bits in that same write latch normally. Two callers,
    // and they disagree about the control register, which is the whole reason this is
    // its own function: writeControl() keeps the byte the guest wrote; powerOn() zeroes
    // it, because the machine was switched on and there is no byte.
    void resetAction(const Clock& clk);

    // The LIVE /CTS pin, strap applied. Only poll() may call it; everything else reads
    // the sample. See ctsPin_.
    bool ctsNow() const;

    // Drive RTS (control bits 5-6) and BREAK at the endpoint. There is nowhere for
    // DTR to come from: the chip has no such pin.
    void driveControl();

    std::string name_;

    // THE LINE, RAW. No transform chain: the chip puts the guest's byte on the wire
    // and takes the wire's byte to the guest, unaltered. The only thing that may
    // mangle a byte is the CONSOLE (host/console.h), because the only thing that has
    // any business mangling one is a terminal. What this chip DOES impose on the wire
    // is the FRAME the guest programmed -- see programLine().
    std::unique_ptr<ByteStream> stream_;

    long long baud_ = 9600;  // a JUMPER on the real card. Software cannot change it.

    uint8_t control_ = 0;
    uint8_t rxData_  = 0;

    bool rdrf_ = false;
    bool ovrn_ = false;

    // ---- /DCD: A LATCHED EDGE WITH A TWO-STEP CLEAR (MC6850 data sheet) ----
    //
    // "It remains high after the DCD input is returned low until cleared by first
    // reading the Status Register and then the Data Register, or until a master reset
    // occurs. If the DCD input remains high after read status and read data ... the
    // interrupt is cleared, the DCD status bit remains high and will follow the DCD
    // input."
    //
    // So a carrier drop is not a level the status register reports. It is an EVENT
    // the chip REMEMBERS -- and a guest that was not looking when the line dropped
    // still finds out. Model it as a bare level and the one program that cares (any
    // modem software ever written) silently never notices the call ended.
    bool dcdFlag_       = false;  // the latched status bit
    bool dcdIrq_        = false;  // ...and the interrupt it raised, cleared separately
    bool dcdStatusRead_ = false;  // step 1 of the two-step clear has happened
    bool dcdPinLost_    = false;  // the pin's last sampled state, for edge detection

    // AND THEN IT FOLLOWS THE PIN AGAIN. "If the DCD input remains high after read
    // status and read data ... the interrupt is cleared, the DCD status bit remains
    // high and will follow the DCD input." So the bit has two modes -- LATCHED (it
    // remembers an edge you have not acknowledged) and FOLLOWING (you acknowledged
    // it; now it is just a level) -- and a model with only the first can never let
    // go of a carrier loss the guest has already dealt with.
    bool dcdFollow_ = true;

    // ---- THE PINS ARE SAMPLED, NOT PEEKED AT (and assertsInt() is why) ----
    //
    // Board::assertsInt() is documented COMBINATIONAL AND PURE: it reports the settled
    // state of a wire, computed from the state of the chip and NOTHING ELSE. Reading
    // the host's CTS line from inside it would break that in two ways, and the second
    // is the one that ruins a week:
    //
    //   1. Bus::setVerify(true) re-derives the interrupt wire on every instruction and
    //      aborts if a board disagrees with it. A real /CTS pin dropping between two
    //      pump()s would move irq() with no intChanged() behind it -- and the abort
    //      would blame the board, which was innocent.
    //
    //   2. RECORD/REPLAY would be DEAD. An interrupt whose timing depends on when the
    //      host scheduler happened to move a pin is an interrupt that lands on a
    //      different T-state on every replay.
    //
    // So the chip SAMPLES its input pins -- in poll(), which runs on the card's own
    // schedule (pump(), a deadline, a register access) -- and everything downstream
    // reads the sample. That is also what the hardware does: a 6850 sees a pin when
    // its clock looks at the pin. The one door the outside world comes through is
    // still pump() (DESIGN.md 7.1), here as everywhere.
    bool ctsPin_ = true;   // /CTS, strap applied
    bool txRoom_ = true;   // ...and whether the endpoint can even take a byte

    std::vector<std::string> log_;

    // TDRE IS A DEADLINE, NOT A FLAG (DESIGN.md 7.5, and the reason Clock exists).
    // The transmit register is empty once the character has had time to leave. A
    // guest that reads the status port before then sees TDRE clear, which is the
    // whole point -- the Mike Douglas CP/M BIOS TIMES how long it stays clear to
    // work out the line speed, and a hardwired TDRE=1 silently changes what that
    // BIOS decides to do.
    uint64_t txFreeAt_ = 0;

    // Receive is paced too, or a byte could never arrive "while the last one was
    // still sitting there" -- which is precisely what an overrun IS.
    uint64_t rxNextAt_ = 0;
};

} // namespace altair
