#pragma once
//
// 88-2SIO -- MITS Dual Serial Interface. Two Motorola 6850 ACIAs on one card.
// See docs/boards/mits-2sio.md.
//
// THE PROOF VEHICLE. A fully-modeled 2SIO exercises every interface in the
// design at once -- ByteStream, units, per-unit properties, interrupts, multiple
// instances of one board -- which is why it is the only peripheral in milestone
// 1. If the interfaces are wrong, this is where it shows.
//
// Base port is a jumper: default 0x10, so channel A is 0x10 (control/status) and
// 0x11 (data), channel B is 0x12 and 0x13.

#include "core/board.h"
#include "host/filter.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

// `IrqJumper` -- where a channel's IRQ is soldered -- is a BUS strap, and lives in
// core/board.h with the rest of pin 73's vocabulary (DESIGN.md 4.4).

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
// One 6850. The card has two, and they share NOTHING -- separate baud jumpers,
// separate endpoints, separate interrupt straps. Modeling them as one object
// with an index would be modeling the PCB, not the chips.
//
// NAMED FOR THE PART, NOT THE FUNCTION (Patrick, 2026-07-12). It was `Acia`, and
// "ACIA" is a role that half a dozen incompatible chips have filled -- the moment a
// card turns up with a different one, `Acia` is a lie with no room left to tell the
// truth in. This is an MC6850, its behaviour comes from the MC6850 data sheet, and
// the class says so. (Phase 3 moves it to src/chips/mc6850.h as a pure file move.)
// ---------------------------------------------------------------------------
class Mc6850 {
public:
    explicit Mc6850(std::string name) : name_(std::move(name)) {}

    const std::string& name() const { return name_; }

    uint8_t readStatus(const Clock& clk);
    uint8_t readData(const Clock& clk);
    void    writeControl(uint8_t v, const Clock& clk);
    void    writeData(uint8_t v, const Clock& clk);

    void masterReset(const Clock& clk);
    bool irq(const Clock& clk) const;   // the chip's IRQ pin, jumper or no jumper

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
    FilterStream* filter() { return filter_; }
    std::string  endpoint() const { return stream_->describe(); }

    std::vector<Property> properties();
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

    // The LIVE /CTS pin, strap applied. Only poll() may call it; everything else reads
    // the sample. See ctsPin_.
    bool ctsNow() const;

    // Drive RTS (control bits 5-6) and BREAK at the endpoint. There is nowhere for
    // DTR to come from: the chip has no such pin.
    void driveControl();

    std::string name_;

    std::unique_ptr<ByteStream> stream_;
    FilterStream*               filter_ = nullptr;  // borrowed; owned by stream_

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

class Sio2Board : public Board {
public:
    Sio2Board();
    ~Sio2Board() override;

    std::string type() const override { return "2sio"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    // PIN 73, combinational and pure. What the chips are asking for, filtered by
    // what is actually soldered to the wire.
    bool assertsInt() const override;

    void reset(Reset) override;
    void power() override;
    void pump() override;
    void configChanged() override;

    // What the chips want said out loud -- today, only "the host cannot do that baud
    // rate". Virtual on Board since 59a175b, which is what makes this possible at all.
    std::vector<std::string> drainLog() override;

    std::vector<Property> properties() override;
    std::vector<Property> unitProperties(const std::string& unit) override;

    std::vector<UnitDef> units() const override;
    std::vector<MapEntry> ioMap() const override;

    // `[board.unit.a]` in the config -- baud, interrupt, connect, and every
    // transform, per channel. The TOML loader already splits the dotted name and
    // hands us `unit = "a"`; it does not know what a channel is, and does not
    // need to.
    std::vector<std::string> subUnitTables() const override { return {"unit"}; }
    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;

    bool connect(const std::string& unit, const std::string& endpoint,
                 std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;

    // The monitor resolves an endpoint string to a stream; the BOARD is not
    // allowed to know what a socket is (DESIGN.md 7.7). This is how the one gets
    // handed to the other.
    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;
    static void setResolver(EndpointResolver r);

    Mc6850* channel(const std::string& name);

private:
    // EVERYTHING THAT COULD HAVE MOVED PIN 73 HAS JUST HAPPENED. Advance the
    // receivers, re-drive the pin, and set the alarm clock for the next moment
    // either chip could move it with nobody touching it.
    //
    // Called after every register access, on pump(), on reset, when a jumper moves
    // -- and from the alarm clock itself, which is what lets the card act while the
    // CPU is halted waiting for it to.
    void refresh();

    Mc6850 a_{"a"};
    Mc6850 b_{"b"};
    uint8_t base_ = 0x10;

    // The one outstanding deadline, for whichever chip's edge comes first. ONE, not
    // one per chip: a card has a state, and re-deriving the earliest edge from
    // scratch on every refresh is both simpler and impossible to leak.
    Clock::Handle wake_ = Clock::kNone;
};

} // namespace altair
