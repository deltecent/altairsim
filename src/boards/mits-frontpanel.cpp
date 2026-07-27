#include "boards/mits-frontpanel.h"

#include "core/statefile.h"
#include "core/value.h"

namespace altair {

// The lamps are wired to the bus, so they show WHATEVER WENT BY LAST -- including a
// cycle this card did not answer, which is every cycle but one. That is the whole
// reason wantsSnoop() is true: a card with a flip-flop on the address bus (board.h).
void FrontPanelBoard::snoop(const BusCycle& c) {
    addrLeds_ = c.addr;
    dataLeds_ = c.data;  // back-filled on reads by Bus::settle()

    uint8_t s = 0;
    switch (c.type) {
        case Cycle::MemRead:  s = LampMemR;            break;
        case Cycle::MemWrite: s = LampWo;              break;  // WO* is active low
        case Cycle::IoRead:   s = LampInp;             break;
        case Cycle::IoWrite:  s = LampOut | LampWo;    break;
        case Cycle::IntAck:   s = LampInt;             break;
    }
    status_ = s;
}

// POWER OFF, LAMPS OUT. The switches do NOT move -- they are toggles, and a toggle
// with no power is still wherever the operator left it. That asymmetry is the
// hardware's, not ours, and it is the one thing power() has to say.
void FrontPanelBoard::power() {
    addrLeds_ = 0;
    dataLeds_ = 0;
    status_   = 0;
}

void FrontPanelBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u16(sw_);
    w.u16(addrLeds_);
    w.u8(dataLeds_);
    w.u8(status_);
}

void FrontPanelBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    sw_       = r.u16();
    addrLeds_ = r.u16();
    dataLeds_ = r.u8();
    status_   = r.u8();
}

std::vector<Property> FrontPanelBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "sense";
        x.help  = "The SENSE switches, SA8..SA15 -- what IN 0FFH reads";
        x.kind  = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xFF;
        x.get   = [this] { return Value::ofInt(sense()); };
        x.set   = [this](const Value& v, std::string&) {
            setSense((uint8_t)v.i());
            return true;
        };
        p.push_back(std::move(x));
    }
    // AND NOTHING ELSE. There used to be a second property, `data`, for the low half of
    // the switch row -- SA0..SA7, the byte DEPOSIT writes on a real panel. It was there
    // for a graphical panel to bind a toggle to, and no such panel exists. Nothing in the
    // machine reads it: no port is wired to those switches (schematic 880-106), and the
    // monitor's DEPOSIT takes its byte from the command line. So it was a knob that
    // changed nothing, sitting in the reference next to one that changes everything.
    //
    // There is no front panel to reach out and flip, so what this board owes an operator
    // is one thing: a way to say what IN 0FFH returns. That is `sense`, and that is all.
    // (The low half of sw_ stays -- it is the same physical row, it travels in the
    // snapshot, and a panel that ever wants it will find it here.)
    return p;
}

std::vector<MapEntry> FrontPanelBoard::ioMap() const {
    // READ ONLY, and SHOW BUS IO says so. An OUT 0FFH is not this card's: the
    // buffer bank's enable is gated with sINP, and there is no sOUT in the gate.
    return {{0xFF, 0xFF, "read", "SENSE switches SA8..SA15 -> D0..D7"}};
}

} // namespace altair
