#include "chips/sio2port.h"

#include "core/statefile.h"
#include "host/stream.h"

#include <utility>

namespace altair {

// WHERE THE ENDPOINT GRAMMAR STOPS. The program installs this (src/main.cpp); every
// card that embeds a section shares it. Neither the section nor the 6850 knows what a
// socket is, and this is the whole of the arrangement that keeps it that way.
namespace {
EndpointResolver g_resolver;
} // namespace

void Sio2Port::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

// A card in a backplane always has a clock, but a section can be built on the bench
// with none. An unclocked 6850 is a chip with no crystal: it cannot receive and it
// cannot time a character, so it reads as a dead card rather than dereferencing null.
static Clock& deadCard() {
    static Clock stopped;
    return stopped;
}

Sio2Port::Sio2Port(std::vector<ChannelDef> channels, std::function<void()> onIntChanged)
    : defs_(std::move(channels)), onIntChanged_(std::move(onIntChanged)) {
    chans_.reserve(defs_.size());  // reserve first: the wake_ lambda holds `this` and
    for (const auto& d : defs_)    // reads chans_ by index, so it must not reallocate
        chans_.emplace_back(d.name);
    for (auto& ch : chans_) ch.disconnect();  // -> NullStream, no null in the stream path
}

Sio2Port::~Sio2Port() {
    // The queue is holding a lambda with `this` inside it. A card can be pulled out of a
    // RUNNING machine, and a deadline that fires into a destroyed section is a
    // use-after-free with a two-week fuse on it.
    if (clock_) clock_->cancel(wake_);
}

// Channel n owns BASE+offset (control/status, even) and BASE+offset+1 (data, odd).
int Sio2Port::chanIndexForPort(uint8_t port, bool& isData) const {
    if (port < base_) return -1;
    uint8_t rel   = (uint8_t)(port - base_);
    isData        = rel & 1u;
    uint8_t chOff = (uint8_t)(rel & ~1u);
    for (size_t i = 0; i < defs_.size(); ++i)
        if (defs_[i].offset == chOff) return (int)i;
    return -1;
}

bool Sio2Port::decodesPort(uint8_t port) const {
    bool isData;
    return chanIndexForPort(port, isData) >= 0;
}

uint8_t Sio2Port::read(uint8_t port) {
    bool isData;
    int  i = chanIndexForPort(port, isData);
    if (i < 0) return 0xFF;
    Clock&  clk = clock_ ? *clock_ : deadCard();
    Mc6850& ch  = chans_[(size_t)i];
    uint8_t v   = isData ? ch.readData(clk) : ch.readStatus(clk);
    refresh();  // reading the data register CLEARS RDRF -- and with it, the interrupt
    return v;
}

void Sio2Port::write(uint8_t port, uint8_t data) {
    bool isData;
    int  i = chanIndexForPort(port, isData);
    if (i < 0) return;
    Clock&  clk = clock_ ? *clock_ : deadCard();
    Mc6850& ch  = chans_[(size_t)i];
    if (isData) ch.writeData(data, clk);
    else ch.writeControl(data, clk);
    refresh();  // a character went out (TDRE falls), or the interrupt enables moved
}

// PIN 73, COMBINATIONAL AND PURE. Pulled if any chip is asking AND that chip's jumper
// actually goes to pin 73. A `vi*` jumper goes to a vectored interrupt line instead.
bool Sio2Port::assertsInt() const {
    if (!clock_) return false;  // no crystal: the chips are not running at all
    for (const auto& ch : chans_)
        if (ch.jumper == IrqJumper::Int && ch.irq(*clock_)) return true;
    return false;
}

// VI0-VI7 as a bitmask: independent chips can sit on different lines and ask at once.
uint8_t Sio2Port::assertsVi() const {
    if (!clock_) return 0;
    uint8_t m = 0;
    for (const auto& ch : chans_)
        if (ch.irq(*clock_)) m |= viBit(ch.jumper);  // viBit() is 0 for none and for int
    return m;
}

// THE SECTION'S OWN CLOCK. The receivers are advanced here, on a deadline the section
// sets for itself -- an interrupt-driven driver never reads the status port, so a chip
// that only ingested a character when the guest looked at a register would never ingest
// one. Re-arming from scratch every time is deliberate: exactly one outstanding timer
// per section, always the earliest edge of any chip, and a scheme that cannot leak a
// timer is worth more than a heap push per character. (mits-2sio.cpp:refresh.)
void Sio2Port::refresh() {
    if (!clock_) return;

    for (auto& ch : chans_) ch.poll(*clock_);

    if (onIntChanged_) onIntChanged_();  // drive pin 73 -- the bus is not going to ask

    clock_->cancel(wake_);
    wake_ = Clock::kNone;

    uint64_t next = 0;
    for (const auto& ch : chans_) {
        uint64_t e = ch.nextEdge(*clock_);
        if (e && (!next || e < next)) next = e;
    }
    if (next) wake_ = clock_->at(next, [this] { refresh(); });
}

// A BUS RESET DOES NOTHING TO A 6850 -- IT HAS NO RESET PIN (mc6850.h). RESET* reaches
// the card's address decoding and nothing else. POWER-ON-CLEAR is different: the machine
// was switched on, and the chips get put in a known good state. refresh() runs either
// way, because the backplane's interrupt wire must be re-driven regardless.
void Sio2Port::reset(Reset r) {
    if (!clock_) return;
    if (r == Reset::PowerOn)
        for (auto& ch : chans_) ch.powerOn(*clock_);
    refresh();  // cancels the outstanding deadline before re-arming; see mits-2sio.cpp
}

// THE ONE DOOR THE OUTSIDE WORLD COMES THROUGH (DESIGN.md 7.1). A character arriving
// from the host is not a deadline, so no timer could have been set for it.
void Sio2Port::pump() {
    for (auto& ch : chans_) ch.pump();
    refresh();
}

void Sio2Port::serialize(StateWriter& w) const {
    // NO Board::serialize here -- the owning card writes that (and its own fields)
    // around this call, so the chips land in the same order a bare 2SIO wrote them.
    for (const auto& ch : chans_) ch.serialize(w);
}

void Sio2Port::deserialize(StateReader& r) {
    for (auto& ch : chans_) ch.deserialize(r);
    refresh();  // re-drive pin 73 and re-arm the deadline from the restored chip state
}

std::vector<std::string> Sio2Port::drainLog() {
    std::vector<std::string> out;
    for (auto& ch : chans_)
        for (auto& s : ch.drainLog()) out.push_back(std::move(s));
    return out;  // the card prefixes each with its id -- the section has none
}

uint64_t Sio2Port::rxBytes() const {
    uint64_t n = 0;
    for (const auto& ch : chans_) n += ch.rxBytes();
    return n;
}

Mc6850* Sio2Port::channel(const std::string& name) {
    for (size_t i = 0; i < defs_.size(); ++i)
        if (defs_[i].name == name) return &chans_[i];
    return nullptr;
}

std::vector<UnitDef> Sio2Port::units() const {
    std::vector<UnitDef> u;
    for (size_t i = 0; i < defs_.size(); ++i)
        u.push_back({defs_[i].name, UnitKind::Serial, chans_[i].endpoint()});
    return u;
}

std::vector<Property> Sio2Port::unitProperties(const std::string& unit) {
    if (Mc6850* ch = channel(unit)) return ch->properties(g_resolver);
    return {};
}

bool Sio2Port::connect(const std::string& unit, const std::string& endpoint, std::string& err) {
    Mc6850* ch = channel(unit);
    if (!ch) {
        err = "no serial unit '" + unit + "'";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    auto s = g_resolver(endpoint, err);
    if (!s) return false;
    ch->connect(std::move(s));
    refresh();  // a new line, and it may already have something waiting on it
    return true;
}

bool Sio2Port::disconnect(const std::string& unit, std::string& err) {
    Mc6850* ch = channel(unit);
    if (!ch) {
        err = "no serial unit '" + unit + "'";
        return false;
    }
    ch->disconnect();
    refresh();  // the line went dead: no more characters are coming off it
    return true;
}

ByteStream* Sio2Port::unitStream(const std::string& unit) {
    Mc6850* ch = channel(unit);
    return ch ? &ch->stream() : nullptr;
}

} // namespace altair
