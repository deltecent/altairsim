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
// One 6850. The card has two, and they share NOTHING -- separate baud jumpers,
// separate endpoints, separate interrupt straps. Modeling them as one object
// with an index would be modeling the PCB, not the chips.
// ---------------------------------------------------------------------------
class Acia {
public:
    explicit Acia(std::string name) : name_(std::move(name)) {}

    const std::string& name() const { return name_; }

    uint8_t readStatus(const Clock& clk);
    uint8_t readData(const Clock& clk);
    void    writeControl(uint8_t v, const Clock& clk);
    void    writeData(uint8_t v, const Clock& clk);

    void masterReset(const Clock& clk);
    bool irq(const Clock& clk) const;   // the chip's IRQ pin, jumper or no jumper

    IrqJumper jumper = IrqJumper::None;

    void connect(std::unique_ptr<ByteStream> s);
    void disconnect();
    ByteStream&  stream()  { return *stream_; }
    FilterStream* filter() { return filter_; }
    std::string  endpoint() const { return stream_->describe(); }

    std::vector<Property> properties();
    void pump() { stream_->pump(); }

    long long baud() const { return baud_; }

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

    std::string name_;

    std::unique_ptr<ByteStream> stream_;
    FilterStream*               filter_ = nullptr;  // borrowed; owned by stream_

    long long baud_ = 9600;  // a JUMPER on the real card. Software cannot change it.

    uint8_t control_ = 0;
    uint8_t rxData_  = 0;

    bool rdrf_ = false;
    bool ovrn_ = false;

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

    Acia* channel(const std::string& name);

private:
    // EVERYTHING THAT COULD HAVE MOVED PIN 73 HAS JUST HAPPENED. Advance the
    // receivers, re-drive the pin, and set the alarm clock for the next moment
    // either chip could move it with nobody touching it.
    //
    // Called after every register access, on pump(), on reset, when a jumper moves
    // -- and from the alarm clock itself, which is what lets the card act while the
    // CPU is halted waiting for it to.
    void refresh();

    Acia a_{"a"};
    Acia b_{"b"};
    uint8_t base_ = 0x10;

    // The one outstanding deadline, for whichever chip's edge comes first. ONE, not
    // one per chip: a card has a state, and re-deriving the earliest edge from
    // scratch on every refresh is both simpler and impossible to leak.
    Clock::Handle wake_ = Clock::kNone;
};

} // namespace altair
