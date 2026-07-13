#include "boards/mits-frontpanel.h"

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
    {
        // THE OTHER HALF OF THE SAME ROW. SA0..SA7 are the DATA switches, and the
        // guest cannot read them -- no port is wired to them (schematic 880-106:
        // that bank goes to the bus only for DEPOSIT and for the low byte of
        // EXAMINE's JMP). So why is it here at all?
        //
        // Because it is A SWITCH ON THE PANEL, and this card is the panel. The
        // monitor's DEPOSIT and EXAMINE take their operand from the command line
        // instead of from these (docs/cli-commands.md, and that is the right call
        // for a terminal) -- but a graphical panel has to put the toggle somewhere,
        // and the somewhere is here, next to the eight switches it shares a row
        // with. Splitting them across two owners is how they would drift.
        Property x;
        x.name  = "data";
        x.help  = "The DATA switches, SA0..SA7. Not readable by the guest -- see the .md";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xFF;
        x.get   = [this] { return Value::ofInt((uint8_t)(sw_ & 0xFF)); };
        x.set   = [this](const Value& v, std::string&) {
            sw_ = (uint16_t)((sw_ & 0xFF00) | (uint8_t)v.i());
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<MapEntry> FrontPanelBoard::ioMap() const {
    // READ ONLY, and SHOW BUS IO says so. An OUT 0FFH is not this card's: the
    // buffer bank's enable is gated with sINP, and there is no sOUT in the gate.
    return {{0xFF, 0xFF, "read", "SENSE switches SA8..SA15 -> D0..D7"}};
}

} // namespace altair
