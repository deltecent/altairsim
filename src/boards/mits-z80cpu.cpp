#include "boards/mits-z80cpu.h"

namespace altair {

// A near-verbatim copy of Cpu8080Board::properties() -- the crystal, the idle nap,
// and the read-only achieved-crystal companion, identical in every respect but the
// core they pace. See mits-88cpu.cpp for the full argument behind each one; there
// is no shared base because two dozen-line cards do not need one.
std::vector<Property> CpuZ80Board::properties() {
    std::vector<Property> p;

    {
        Property x;
        x.name = "clock_hz";
        x.help = "Crystal on the board. 0 runs flat out -- as fast as the host can.";
        x.kind = Kind::Int;
        x.radix = 10;
        x.unit = "Hz";
        x.min = 0;
        x.max = 100000000;
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

    {
        Property x;
        x.name  = "achieved_hz";
        x.help  = "LIVE: T-states per real second the run loop last reached -- the crystal "
                  "you got, beside the one you asked for. Read-only; 0 until it has run.";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.unit  = "Hz";
        x.get   = [this] { return Value::ofInt(achievedHz_); };
        p.push_back(std::move(x));
    }

    return p;
}

void CpuZ80Board::publishPolicy() {
    if (!clock_) return;
    clock_->setHz(clockHz_);
    clock_->setIdle(idle_);
}

std::vector<UnitDef> CpuZ80Board::units() const {
    return {{"z80", UnitKind::Cpu, "active"}};
}

} // namespace altair
