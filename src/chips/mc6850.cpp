#include "chips/mc6850.h"

#include "host/stream.h"

#include <utility>

namespace altair {

// ---------------------------------------------------------------------------
// 6850 status register -- TRUE SENSE.
//
// TRAP, and it is written down in the board's .md because it has bitten people
// for forty years: the 88-SIO's status bits are INVERTED and the 88-2SIO's are
// not. A machine can have both cards in it, both conventions live at once. Do
// not share code between them without thinking. (We don't: they will be two
// files.)
// ---------------------------------------------------------------------------
namespace {
constexpr uint8_t kRdrf = 0x01;  // receive data register full
constexpr uint8_t kTdre = 0x02;  // transmit data register empty
constexpr uint8_t kDcd  = 0x04;  // set = carrier LOST (the pin is /DCD)
constexpr uint8_t kCts  = 0x08;  // set = NOT clear to send (the pin is /CTS)
constexpr uint8_t kOvrn = 0x20;
constexpr uint8_t kIrq  = 0x80;

// Bit 4 (FE, framing error) and bit 6 (PE, parity error) EXIST and are always
// ZERO, deliberately. They report line noise, and there is no line: a ByteStream
// delivers the byte that was sent or it delivers nothing. Synthesizing them would
// mean inventing a noise model, which would mean inventing a probability, which
// is exactly the kind of number DESIGN.md 0.1 says to ask about rather than make
// up. If a guest ever needs to SEE a framing error, that is a feature request
// with a real use case attached, not a gap to quietly paper over now.

// Control register (write only).
constexpr uint8_t kDivideMask = 0x03;
constexpr uint8_t kMasterReset = 0x03;  // divide field == 11
constexpr uint8_t kWordMask   = 0x1C;   // bits 2-4
constexpr uint8_t kTxCtlMask  = 0x60;   // bits 5-6
constexpr uint8_t kRie        = 0x80;   // bit 7: receive interrupt enable

// THE DIVIDE RATIO IS NOT MODELED, AND HERE IS THE HONEST ACCOUNT OF IT.
//
// CR1:CR0 select the counter divide -- 00 is /1, 01 is /16, 10 is /64, and 11 is
// the master reset. We decode 11 and IGNORE the other three: `baud` is the rate on
// the wire, full stop, and a guest that reprograms /16 to /64 gets no change where
// a real card would slow down by a factor of four.
//
// That is a divergence, not an oversight, and the reason is that fixing it needs a
// fact this data sheet does not have. The 6850 divides a clock it is GIVEN, and what
// the 88-2SIO gives it is a jumper on the card -- so to honour the divide ratio,
// `baud` would have to stop meaning "the line rate" and start meaning "the crystal",
// and the conversion between them is in the 2SIO manual's strapping table, not here.
// Guessing at it would be exactly the kind of invented number DESIGN.md 0.1 forbids.
//
// Nothing in period software has been observed to care: a driver picks a divide ratio
// once, at init, and the jumper was cut to match. It is written down in
// docs/boards/mits-2sio.md as a known limitation rather than left to be discovered.

// Transmit control field (bits 5-6), from the datasheet:
//   00  RTS low,  transmit interrupt DISABLED
//   01  RTS low,  transmit interrupt ENABLED
//   10  RTS high, transmit interrupt disabled
//   11  RTS low,  transmit a BREAK
constexpr uint8_t kTxIrqEnabled = 0x20;  // the 01 case

} // namespace

// ---------------------------------------------------------------------------
// Mc6850
// ---------------------------------------------------------------------------

// THE WORD-SELECT BITS ARE THE FRAME ON THE WIRE. One table, read two ways: as a
// BIT COUNT (how long a character occupies the line, which is what paces the
// emulation) and as a FRAME (data bits, parity, stop bits -- which is what a real
// host UART has to be programmed with). They cannot be allowed to disagree, so they
// are not allowed to be two tables.
namespace {
struct WordFormat {
    int        dataBits;
    LineParity parity;
    int        stopBits;
};

// MC6850 data sheet, Word Select Bits (CR2, CR3, CR4).
constexpr WordFormat kWordFormats[8] = {
    {7, LineParity::Even, 2},
    {7, LineParity::Odd,  2},
    {7, LineParity::Even, 1},
    {7, LineParity::Odd,  1},
    {8, LineParity::None, 2},  // <- ALTMON writes this
    {8, LineParity::None, 1},
    {8, LineParity::Even, 1},
    {8, LineParity::Odd,  1},
};
} // namespace

int Mc6850::bitsPerChar() const {
    const WordFormat& w = kWordFormats[(control_ & kWordMask) >> 2];
    return 1 + w.dataBits + (w.parity == LineParity::None ? 0 : 1) + w.stopBits;
}

LineParams Mc6850::params() const {
    const WordFormat& w = kWordFormats[(control_ & kWordMask) >> 2];
    LineParams p;
    p.baud     = baud_;
    p.dataBits = w.dataBits;
    p.parity   = w.parity;
    p.stopBits = w.stopBits;
    return p;
}

uint64_t Mc6850::charTStates(const Clock& clk) const {
    if (baud_ <= 0) return 0;
    return (uint64_t)(clk.hz() * (long long)bitsPerChar() / baud_);
}

// PUSH THE CARD'S STRAP AND THE GUEST'S FRAME AT THE WIRE. Ignored by every
// endpoint but a real serial port, which is the only one that HAS a baud rate.
void Mc6850::programLine() {
    std::string err;
    if (stream_->setParams(params(), err)) return;

    // The host refused. Say it once, in a sentence, and go on running at the strap --
    // because the strap is what the guest can measure, and it is still what the card
    // is jumpered to. Silence here would be a wire running at a speed nobody chose.
    log_.push_back(name_ + ": " + err);
}

void Mc6850::connect(std::unique_ptr<ByteStream> s) {
    // EVERY endpoint gets the transform chain, whatever it is (DESIGN.md 7.2).
    // The filter is not a console feature that a socket has to do without.
    auto f   = std::make_unique<FilterStream>(std::move(s));
    filter_  = f.get();
    stream_  = std::move(f);

    // A NEW LINE STARTS WHERE THE CARD ALREADY IS. The 6850 does not reset because
    // you plugged something into it: RTS is still whatever bits 5-6 say, the baud is
    // still the jumper, and the frame is still whatever the guest programmed. So the
    // wire is brought up to the chip's state, not the other way round.
    programLine();
    driveControl();

    // The pin state is whatever THIS endpoint says -- and it may already differ. Take
    // it as the new baseline WITHOUT raising a carrier-loss interrupt: plugging a
    // cable in is not a modem dropping the call, and a spurious DCD interrupt at
    // CONNECT would be a phantom hangup on a line that was never up.
    dcdPinLost_ = !carrier();
    ctsPin_     = ctsNow();
    txRoom_     = stream_->writable();
}

void Mc6850::disconnect() { connect(std::make_unique<NullStream>()); }

std::vector<std::string> Mc6850::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    return out;
}

// ---------------------------------------------------------------------------
// THE PINS, WITH THE STRAP APPLIED. This is the only place the strap is consulted,
// and everything downstream -- the status bits, the interrupt, the receiver, the
// transmitter -- reads the pin through it.
//
// `ground` means the pin never reaches the connector: it is tied to its ASSERTED
// state on the card, and the far end is not asked. That is why a 2SIO on the console
// works today, why it goes on working, and why no board has a "what if nothing is
// plugged in" branch.
// ---------------------------------------------------------------------------
bool Mc6850::carrier() const {
    if (dcdStrap == PinStrap::Ground) return true;
    return stream_->status().carrier;
}

// THE SAMPLE, not the pin. See the header: assertsInt() must be pure, and it reads
// this. The sample is refreshed in poll(), which is the card's own clock.
bool Mc6850::clearToSend() const { return ctsPin_; }

// The live pin, for poll() to sample and for nothing else.
bool Mc6850::ctsNow() const {
    if (ctsStrap == PinStrap::Ground) return true;
    return stream_->status().cts;
}

// ---------------------------------------------------------------------------
// /DCD, AND IT IS NOT A STATUS BIT. The data sheet:
//
//   "The DCD input inhibits and initializes the receiver section of the ACIA when
//    high. A low-to-high transition of DCD initiates an interrupt to the MPU to
//    indicate the occurrence of a loss of carrier when the Receive Interrupt Enable
//    bit is set." ... "It remains high after the DCD input is returned low until
//    cleared by first reading the Status Register and then the Data Register, or
//    until a master reset occurs."
//
// THREE distinct behaviours, and only the first is obvious:
//
//   1. the status bit is LATCHED on the edge, and survives the pin coming back;
//   2. it raises an INTERRUPT (a carrier drop wakes a program that was not looking);
//   3. while the pin is high the RECEIVER IS DEAD -- inhibited and initialized, and
//      "Data Carrier Detect being high also causes RDRF to indicate empty."
//
// Number 3 is the one that would never have been guessed. A modem program that
// checked RDRF after the call dropped would, on a card that only set a bit, go on
// happily reading garbage out of the receive register.
//
// Sampled here, on the card's own schedule (pump() and the deadline), NOT when the
// guest happens to read a register -- a carrier drop is an event in the outside
// world, and a guest sitting in a HLT waiting for the interrupt would otherwise wait
// forever for a pin nobody was watching.
// ---------------------------------------------------------------------------
void Mc6850::sampleDcd() {
    bool lost = !carrier();

    if (lost && !dcdPinLost_) {  // the low-to-high transition: THE CALL JUST DROPPED
        dcdFlag_       = true;
        dcdIrq_        = true;
        dcdFollow_     = false;  // LATCHED now: it will outlive the pin
        dcdStatusRead_ = false;
        rdrf_          = false;  // "inhibits and initializes the receiver section"
        ovrn_          = false;
    }
    dcdPinLost_ = lost;

    // Acknowledged: the bit is a level again, and tracks the pin until the next edge
    // latches it afresh.
    if (dcdFollow_) dcdFlag_ = lost;
}

// The divide field IS the reset, and it LATCHES. See the header: this is a state the
// chip sits in until a second control write lets it out.
bool Mc6850::inReset() const { return (control_ & kDivideMask) == kMasterReset; }

// TDRE -- and the three things that inhibit it. See the header.
bool Mc6850::tdre(const Clock& clk) const {
    if (inReset()) return false;              // "...or the ACIA being maintained in the
                                              // Reset condition" -- data sheet
    if (clk.now() < txFreeAt_) return false;  // the character has not finished leaving
    if (!ctsPin_) return false;               // "the TDRE bit is inhibited" -- data sheet
    if (!txRoom_) return false;               // ...and the far end has nowhere to put it
    return true;
}

// RTS, and BREAK, out to the wire. Transmit control field (bits 5-6), data sheet:
//
//   00  RTS low  (asserted), transmit interrupt disabled
//   01  RTS low  (asserted), transmit interrupt ENABLED
//   10  RTS HIGH (negated),  transmit interrupt disabled
//   11  RTS low  (asserted), transmit a BREAK, interrupt disabled
//
// This is where RTS finally GOES somewhere. It was decoded here before and dropped
// on the floor, because ByteStream had no output path at all.
//
// There is no DTR. The 6850 has three modem pins -- /CTS, /DCD, RTS -- and DTR is
// not among them, so this card cannot hang up a phone. (The data sheet notes RTS
// "can also be used for Data Terminal Ready", i.e. a card MAY wire it that way; the
// 88-2SIO's hardwire table is not in front of us, and DESIGN.md 0.1 says ask rather
// than reason. So: not modeled, and the PMMI is the card that needs it.)
void Mc6850::driveControl() {
    uint8_t txctl = (control_ & kTxCtlMask) >> 5;
    LineControl c;
    c.rts = (txctl != 0b10);
    c.brk = (txctl == 0b11);
    c.dtr = false;
    stream_->setControl(c);
}

// Pull a byte off the line, if the line has had time to deliver one AND the
// receive register is free to take it.
//
// ---------------------------------------------------------------------------
// WHY THIS DOES NOT SYNTHESIZE OVRN, having tried to.
//
// The first version pulled a byte every character-time whether or not the guest
// had read the last one, and raised OVRN and DISCARDED the byte when it hadn't.
// That is what a real 6850 does, and it is wrong here -- it lost data
// immediately and visibly. ALTMON echoes the full command name as you type
// ("D" -> "DUMP "), and while it was busy transmitting those five characters it
// was not reading the receiver, so the address you typed after the D was thrown
// on the floor. The dump command silently did nothing.
//
// The bug was not the pacing. It was believing a ByteStream is a serial LINE.
//
// A real line is free-running: the sender shoves bits down the wire on its own
// clock and the receiver keeps up or loses them, which is what an overrun IS. A
// ByteStream is nothing like that. It is a buffered, flow-controlled source -- a
// pipe, a socket, an OS keyboard queue -- and it is perfectly happy to hold the
// byte until we take it. Inventing an overrun from it does not reproduce a
// hardware behaviour; it MANUFACTURES data loss that the host transport does not
// have, and it breaks transfers that would have worked.
//
// So: pace the arrivals at the baud rate (that part is real, it is what stops
// the guest reading faster than the line allows, and it is the same clock TDRE
// is timed against) -- but only ever take a byte when the register is free.
//
// The honest consequence is that status bit 5 is always zero, and that is
// written down in docs/boards/mits-2sio.md as a limitation rather than left for
// someone to discover. If a HOST SERIAL PORT endpoint ever lands, an overrun
// there is a genuine hardware event and the stream can report it -- from the
// place that actually knows, which is not here.
// ---------------------------------------------------------------------------
void Mc6850::poll(const Clock& clk) {
    // SAMPLE THE INPUT PINS. They may have moved while nobody was looking -- a real
    // /CTS is a voltage on a cable and owes the CPU nothing. Everything downstream
    // (the status bits, TDRE, the interrupt) reads the SAMPLE, which is what keeps
    // assertsInt() pure and replay deterministic. See the header.
    ctsPin_ = ctsNow();
    txRoom_ = stream_->writable();

    sampleDcd();  // ...and /DCD, which is a great deal more than a level. See below.

    // THE RECEIVER IS DEAD WITH NO CARRIER. Not "delivers a bad byte" -- dead: the
    // data sheet says /DCD high "inhibits and initializes the receiver section".
    // The bytes stay on the line, and the guest sees an empty receive register,
    // which is what it should see when the call has dropped.
    if (!carrier()) return;

    // ...and it is dead while the chip is held in reset, for the same reason and by
    // the same words: the master reset "initializes both the receiver and
    // transmitter". A chip sitting in the reset condition is not listening to the
    // line, so the bytes wait on it. (The PINS above are sampled anyway: /CTS and
    // /DCD are external conditions, and a reset does not reach across a cable.)
    if (inReset()) return;

    if (rdrf_) return;                  // the register is still full: the line waits
    if (clk.now() < rxNextAt_) return;  // the character has not finished arriving
    if (!stream_->readable()) return;

    uint8_t b = 0;
    if (stream_->read(&b, 1) != 1) return;

    rxData_   = b;
    rdrf_     = true;
    rxNextAt_ = clk.now() + charTStates(clk);
}

uint8_t Mc6850::readStatus(const Clock& clk) {
    poll(clk);

    uint8_t s = 0;
    if (rdrf_) s |= kRdrf;
    if (tdre(clk)) s |= kTdre;
    if (ovrn_) s |= kOvrn;

    // /DCD and /CTS are INPUTS, and the status bit is SET when the line is NEGATED --
    // the pins are active-low, and this is the one place in the program that knows
    // it. Everything upstream of here speaks in "asserted = true", so each layer
    // inverts exactly once, in the place that owns the polarity.
    //
    // DCD reads the LATCH, not the pin: a carrier loss the guest has not acknowledged
    // is still reported after the carrier comes back. See sampleDcd().
    if (dcdFlag_) s |= kDcd;
    if (!clearToSend()) s |= kCts;

    if (irq(clk)) s |= kIrq;

    // STEP ONE OF THE TWO-STEP CLEAR. Reading the status register ARMS the DCD flag's
    // release; reading the data register is what actually springs it. The guest has
    // to look at the bit before it is allowed to forget it, which is the whole point
    // of the sequence -- an interrupt that could be cleared without being read would
    // be an interrupt you could miss.
    if (dcdFlag_) dcdStatusRead_ = true;

    return s;
}

uint8_t Mc6850::readData(const Clock& clk) {
    poll(clk);

    // Reading the data register clears RDRF and OVRN. Note it clears them EVEN IF
    // RDRF was not set: a read of an empty receive register on a real 6850 gives
    // you whatever was last there, and does not error.
    rdrf_ = false;
    ovrn_ = false;

    // ...AND STEP TWO. Status-then-data: the interrupt goes, and the latch lets go
    // and becomes a level again. If the pin is STILL high -- the call is still down --
    // the bit stays set, because it now follows the pin. The guest is not told the
    // carrier is back merely because it acknowledged that it went.
    if (dcdStatusRead_ && dcdFlag_) {
        dcdIrq_        = false;
        dcdStatusRead_ = false;
        dcdFollow_     = true;
        dcdFlag_       = dcdPinLost_;
    }

    return rxData_;
}

// EVERY BIT OF THE BYTE LATCHES, INCLUDING ON A MASTER RESET. The data sheet is flat
// about it -- "Master reset does not affect other Control Register bits" -- and it
// used to be got wrong here: a master-reset write was swallowed whole and the control
// register zeroed, so a guest that wrote 0x83 (reset AND arm the receive interrupt, in
// one OUT, which is legal and which some drivers do) got its interrupt enable thrown
// away. The reset is a thing the write REQUESTS, not a thing the write IS.
//
// And the reset LASTS. The divide field stays 11 until a second control write clears
// it, and until then the chip is held in reset -- see inReset(), and note that this is
// why nothing works until ALTMON's second OUT.
void Mc6850::writeControl(uint8_t v, const Clock& clk) {
    control_ = v;
    if (inReset()) resetAction(clk);

    // THE CONTROL REGISTER IS THE WIRE. Bits 5-6 are RTS and BREAK -- real pins, and
    // they go out now. Bits 2-4 are the word format, which IS the frame a real serial
    // port has to be programmed with: a guest that reconfigures the chip for 7E1
    // reconfigures the cable for 7E1, exactly as it would on the bench.
    driveControl();
    programLine();
}

void Mc6850::writeData(uint8_t v, const Clock& clk) {
    stream_->write(&v, 1);
    stream_->flush();

    // The character is now on the wire, and the transmit register is BUSY until
    // it has had time to get out. This is the line the Mike Douglas BIOS is
    // timing.
    txFreeAt_ = clk.now() + charTStates(clk);
}

// The reset itself, with the control register left alone. Called two ways -- by a
// guest writing 11 into the divide field, and by the card at power-on -- and they
// disagree about the control register, so it is not this function's business.
void Mc6850::resetAction(const Clock& clk) {
    rdrf_     = false;
    ovrn_     = false;
    rxData_   = 0;
    txFreeAt_ = clk.now();   // "initializes both the receiver and transmitter"
    rxNextAt_ = clk.now();

    // "Master reset ... clears the Status Register (EXCEPT for external conditions on
    // CTS and DCD)" -- data sheet. Those two are PINS: a reset button on the front
    // panel does not put a carrier back on a line that has dropped, and a chip that
    // pretended otherwise would let a guest clear a hangup by resetting itself. So
    // the latch is released and the bit goes back to following the pin, which may
    // very well still be high.
    dcdIrq_        = false;
    dcdStatusRead_ = false;
    dcdFollow_     = true;
    dcdPinLost_    = !carrier();
    dcdFlag_       = dcdPinLost_;

    // Re-sample the input pins. "Master reset does not affect the Clear-to-Send status
    // bit" (data sheet) -- it is a PIN, and a reset does not reach across the cable.
    ctsPin_ = ctsNow();
    txRoom_ = stream_->writable();
}

// POWER-ON-CLEAR. NOT the master reset, and NOT a bus reset -- it used to be called
// masterReset() and the name was a lie that pointed straight at the wrong data sheet
// page. Three different things, and only two of them can touch this chip:
//
//   - THE MASTER RESET is the GUEST's: 11 in the divide field. It does NOT clear the
//     control register -- "Master reset does not affect other Control Register bits" --
//     it latches the whole byte it arrived in, and it HOLDS the chip down until a
//     second control write. That is writeControl(), modeled to the letter, because
//     guest software can see, time and depend on every part of it.
//
//   - THE BUS RESET (RESET*, the front-panel switch) DOES NOT REACH THIS CHIP AT ALL.
//     The 6850 has no reset pin. Nothing here happens on one, and the card must not
//     invent it -- see Sio2Board::reset().
//
//   - THIS is the machine being switched on. It zeroes the control register, which no
//     external pin on the real chip can do, because the real chip HAS NO SUCH PIN: at
//     power-up an internal reset holds the ACIA until the guest's first master reset
//     releases it. We do not model the holding (nothing can observe it that does not
//     also program the chip) -- we simply come up in a known good state, at once, so
//     that a machine is usable the moment it is switched on. DESIGN.md 6.1.
void Mc6850::powerOn(const Clock& clk) {
    control_ = 0;
    resetAction(clk);

    // The endpoint STAYS CONNECTED. Switching the machine on does not unplug the
    // terminal -- and a guest that found the console gone would be a baffling thing
    // to debug.
    //
    // But it DOES REACH THE WIRE: control_ is now 0, so RTS is asserted and BREAK is
    // off, and those are pins the far end can see.
    driveControl();
    programLine();
}

// The chip's IRQ pin. Note this is asked WITHOUT reference to the jumper: the
// 6850 raises IRQ because of what is happening inside it, and whether that goes
// anywhere is a question about the WIRE, which is the board's business, not the
// chip's. Keeping the two separate is why the status register's IRQ bit reads
// correctly even on a channel whose interrupt is not jumpered at all.
//
// THE RECEIVE INTERRUPT ENABLE ARMS THREE THINGS, NOT ONE. The data sheet: an
// interrupt occurs when "the Receiver Interrupt Enable is set and the Receive Data
// Register Full bit is high, an Overrun has occurred, or Data Carrier Detect (/DCD)
// has gone high." A CARRIER DROP IS A RECEIVE INTERRUPT -- which is exactly how a
// modem program sitting in a HLT finds out that the call ended.
bool Mc6850::irq(const Clock& clk) const {
    if (control_ & kRie) {
        if (rdrf_) return true;
        if (ovrn_) return true;
        if (dcdIrq_) return true;
    }
    // The transmit interrupt is DERIVED from TDRE -- so /CTS inhibiting TDRE inhibits
    // this too. A card whose far end is not clear-to-send does not spin the guest
    // through an interrupt handler it has nowhere to transmit into.
    if ((control_ & kTxCtlMask) == kTxIrqEnabled && tdre(clk)) return true;
    return false;
}

// WHEN COULD THIS PIN MOVE ON ITS OWN? Zero means never.
uint64_t Mc6850::nextEdge(const Clock& clk) const {
    uint64_t best = 0;
    auto consider = [&](uint64_t when) {
        if (when <= clk.now()) return;  // already past: irq() is showing it already
        if (!best || when < best) best = when;
    };

    // A chip held in reset has no edges of its own AT ALL: the receiver is initialized
    // and TDRE is inhibited, so no amount of waiting moves the pin. The only thing that
    // gets it out of this state is the guest, writing a divide ratio -- and a register
    // write is not a deadline.
    if (inReset()) return 0;

    // The transmitter drains on its own clock. TDRE goes true at txFreeAt_, and
    // with the transmit interrupt jumpered on, IRQ rises with it -- WITH NOBODY
    // TOUCHING THE CHIP. That is the case the old poll-every-instruction model was
    // quietly covering for, and it is exactly the case a guest sitting in a HLT is
    // waiting on.
    //
    // Only if the line will actually TAKE the character, though. With /CTS negated,
    // TDRE is inhibited and no amount of waiting produces it: that edge is not coming
    // from the clock, it is coming from the far end raising a pin, and that arrives
    // through pump() like every other event in the outside world.
    if ((control_ & kTxCtlMask) == kTxIrqEnabled && ctsPin_ && txRoom_) consider(txFreeAt_);

    // The receiver fills on its own too -- but only if there is actually a
    // character on the line to fill it with. If the host has sent nothing, there is
    // no edge coming and no timer to set: a byte appearing out of nowhere is not a
    // deadline, it is an event in the OUTSIDE WORLD, and pump() is the one door the
    // outside world comes through (DESIGN.md 7.1).
    //
    // This is the asymmetry that makes both mechanisms necessary, and it is why the
    // answer to "event queue or periodic timer?" is "both, and you already had the
    // second one."
    //
    // ...and only while there is a carrier. With /DCD high the receiver is inhibited,
    // so no character can arrive to raise RDRF however long we wait.
    if ((control_ & kRie) && !rdrf_ && carrier() && stream_->readable()) consider(rxNextAt_);

    return best;
}

// The pin strap, once, for both pins that have one. `SET sio0:a dcd=wired`.
static Property pinStrapProperty(std::string name, std::string help, PinStrap& s) {
    Property x;
    x.name    = std::move(name);
    x.help    = std::move(help);
    x.kind    = Kind::Enum;
    x.choices = {"ground", "wired"};
    x.get     = [&s] { return Value::ofStr(s == PinStrap::Ground ? "ground" : "wired"); };
    x.set     = [&s](const Value& v, std::string&) {
        s = (v.s() == "wired") ? PinStrap::Wired : PinStrap::Ground;
        return true;
    };
    return x;
}

std::vector<Property> Mc6850::properties(const EndpointResolver& resolve) {
    std::vector<Property> p;

    {
        Property x;
        x.name    = "baud";
        x.help    = "Line rate. A JUMPER on the real card -- software cannot change it";
        x.kind    = Kind::Int;
        x.radix   = 10;  // never on the wire: decimal (DESIGN.md 10.0.1)
        x.min     = 50;
        x.max     = 76800;
        x.unit    = "baud";
        x.get     = [this] { return Value::ofInt(baud_); };
        x.set     = [this](const Value& v, std::string&) {
            baud_ = v.i();
            programLine();  // the jumper moved, so the WIRE moves: see programLine()
            return true;
        };
        p.push_back(std::move(x));
    }
    p.push_back(irqJumperProperty(
        "interrupt", "Where this channel's IRQ is jumpered: none | int | vi0..vi7", jumper));

    p.push_back(pinStrapProperty(
        "dcd", "/DCD pin: grounded on the card, or wired to the connector", dcdStrap));
    p.push_back(pinStrapProperty(
        "cts", "/CTS pin: grounded on the card, or wired -- and then it gates the transmitter",
        ctsStrap));

    // THE PINS THEMSELVES, READ-ONLY, because a pin is not a jumper. You want this
    // within an hour of plugging in the first real cable: it is the whole difference
    // between "the guest has stopped transmitting" and "the far end is not asserting
    // CTS, so the chip has not raised TDRE, and it is RIGHT not to."
    {
        Property x;
        x.name = "lines";
        x.help = "Live pin state (read-only). CAPITALS = asserted. in: DCD CTS, out: RTS BRK";
        x.kind = Kind::Str;
        x.get  = [this] {
            uint8_t     txctl = (control_ & kTxCtlMask) >> 5;
            std::string s;
            s += carrier() ? "DCD " : "dcd ";
            s += clearToSend() ? "CTS " : "cts ";
            s += (txctl != 0b10) ? "RTS " : "rts ";
            s += (txctl == 0b11) ? "BRK" : "brk";
            return Value::ofStr(s);
        };
        // NO SETTER, and that is the point: you cannot SET what the far end is doing.
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "connect";
        x.help    = "The endpoint on the other end of the line (CONNECT sets this)";
        x.kind    = Kind::Str;
        x.get     = [this] { return Value::ofStr(stream_->describe()); };
        // BY VALUE, and it matters: this lambda outlives properties() by a long way --
        // the monitor holds the Property and calls set() minutes later. A reference to
        // the parameter would be a dangling one the first time a caller passes a
        // temporary. A std::function copy is a pointer and a refcount.
        x.set     = [this, resolve](const Value& v, std::string& err) {
            if (!resolve) {
                err = "no endpoint resolver installed";
                return false;
            }
            auto s = resolve(v.s(), err);
            if (!s) return false;
            connect(std::move(s));
            return true;
        };
        p.push_back(std::move(x));
    }

    // The transform chain, verbatim -- upper, strip7in, crlf, bsdel and the rest
    // (DESIGN.md 7.2). They are the FILTER's properties, not the board's, and the
    // board simply passes them through. Which is why `SET sio2a:b UPPER=ON` works
    // on a socket exactly as it does on the console, with no code here.
    for (Property& f : filter_->properties()) p.push_back(std::move(f));

    return p;
}

} // namespace altair
