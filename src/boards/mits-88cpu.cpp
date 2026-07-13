#include "boards/mits-88cpu.h"

namespace altair {

std::vector<Property> Cpu8080Board::properties() {
    std::vector<Property> p;

    {
        Property x;
        x.name = "clock_hz";
        x.help = "Crystal on the card. 0 runs flat out -- as fast as the host can.";
        x.kind = Kind::Int;
        x.radix = 10;   // A clock rate never goes on the wire, so it is DECIMAL
                        // (DESIGN.md 10.0.1). `SET cpu0 clock_hz=2000000` is two
                        // million, not 0x2000000, and nobody has to think about it.
        x.unit = "Hz";
        x.min = 0;
        x.max = 100000000;
                            // watch it, and it is the first thing you want when a
                            // guest is misbehaving.
        x.get = [this] { return Value::ofInt(clockHz_); };
        x.set = [this](const Value& v, std::string&) {
            clockHz_ = v.i();
            publishPolicy();
            return true;
        };
        p.push_back(std::move(x));
    }

    {
        Property x;
        x.name = "idle";
        x.help = "Stand down when the guest is only polling an empty keyboard. On by "
                 "default -- the guest cannot tell, and a prompt stops burning a core.";
        x.kind = Kind::Bool;
        x.get  = [this] { return Value::ofBool(idle_); };
        x.set  = [this](const Value& v, std::string&) {
            idle_ = v.b();
            publishPolicy();
            return true;
        };
        p.push_back(std::move(x));
    }

    return p;
}

// THE CRYSTAL IS ON THIS CARD, and the machine's Clock is where everyone else
// looks for it (DESIGN.md 3, 7.5, 8). A 6850 converting a baud rate into T-states
// must not have to go hunting through the backplane for whichever board happens
// to hold the oscillator -- it asks the clock, and the clock was told by us.
//
// THE RUN LOOP'S TWO SLEEPING POLICIES BOTH LIVE ON THIS CARD, and they both arrive
// at the Clock by this one path: the crystal (`clock_hz` -> setHz, which also decides
// free()) and the idle nap (`idle` -> setIdle). They are separate questions -- keep
// time / stand down when there is nothing to do -- and either can be set without the
// other, which is why setHz() must never touch idle_.
//
// Published on power AND on every SET, because both are runtime properties: slowing
// the machine down to watch it is the first thing an operator reaches for, and the
// UART's timing has to slow down with it or the baud rate would silently double.
void Cpu8080Board::publishPolicy() {
    if (!clock_) return;
    clock_->setHz(clockHz_);
    clock_->setIdle(idle_);
}

std::vector<UnitDef> Cpu8080Board::units() const {
    // One core, and it is always the active one. The list exists anyway, because
    // SHOW, the MCP schema and tab completion all read Board::units() and there is
    // no second list for them to disagree with -- and because the day a dual-CPU
    // card lands, it is this list that grows a second row and nothing else changes.
    return {{"8080", UnitKind::Cpu, "active"}};
}

} // namespace altair
