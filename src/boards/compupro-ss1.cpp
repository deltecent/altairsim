#include "boards/compupro-ss1.h"

#include "core/statefile.h"

namespace altair {

// ---------------------------------------------------------------------------
// THE DECODE. Phase 1 answers only the two RTC ports. The clock command register is
// write-only (base+10); the clock data register (base+11) is read AND write.
bool Ss1Board::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type == Cycle::IoWrite)
        return c.port() == clockCmdPort() || c.port() == clockDataPort();
    if (c.type == Cycle::IoRead) return c.port() == clockDataPort();  // command is write-only
    return false;
}

uint8_t Ss1Board::read(const BusCycle& c) {
    if (c.type == Cycle::IoRead && c.port() == clockDataPort()) return rtc_.readData();
    return 0xFF;
}

void Ss1Board::write(const BusCycle& c) {
    if (c.type != Cycle::IoWrite) return;
    if (c.port() == clockCmdPort())
        rtc_.writeCommand(c.data);
    else if (c.port() == clockDataPort())
        rtc_.writeData(c.data);
}

// The MSM5832 is battery-backed: neither the front-panel RESET nor a power cycle
// changes the time it keeps. Both events only clear the board-side command latches.
void Ss1Board::reset(Reset) { rtc_.reset(); }
void Ss1Board::power() { rtc_.reset(); }

void Ss1Board::serialize(StateWriter& w) const {
    Board::serialize(w);
    rtc_.serialize(w);
}

void Ss1Board::deserialize(StateReader& r) {
    Board::deserialize(r);
    rtc_.deserialize(r);
}

std::vector<Property> Ss1Board::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name = "base";
        x.help = "Base port of the 16-port I/O block. CompuPro standard is 50H";
        x.kind = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min = 0;
        x.max = 0xF0;  // the block is 16 ports wide; base+15 must stay under 0x100
        x.get = [this] { return Value::ofInt(base_); };
        x.set = [this](const Value& v, std::string&) {
            base_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        // LIVE, read-only: the time the clock is showing right now (host time plus
        // whatever the guest last set). No setter -- the guest sets the clock by
        // programming the MSM5832, not by a monitor SET, so CONFIG SAVE skips this.
        Property x;
        x.name = "clock";
        x.help = "LIVE: the date/time the MSM5832 is showing, and its offset from host time";
        x.kind = Kind::Str;
        x.get = [this] { return Value::ofStr(rtc_.describe()); };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<MapEntry> Ss1Board::ioMap() const {
    return {
        {clockCmdPort(), clockCmdPort(), "write", "MSM5832 clock -- command"},
        {clockDataPort(), clockDataPort(), "read/write", "MSM5832 clock -- data"},
    };
}

}  // namespace altair
