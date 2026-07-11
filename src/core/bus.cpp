#include "core/bus.h"

#include "core/board.h"

#include <algorithm>
#include <cstdio>

namespace altair {

void Bus::attach(Board* b) { boards_.push_back(b); }

void Bus::detach(Board* b) {
    boards_.erase(std::remove(boards_.begin(), boards_.end(), b), boards_.end());
}

bool Bus::anyAssertsPhantom(const BusCycle& c) const {
    for (Board* b : boards_)
        if (b->enabled() && b->assertsPhantom(c)) return true;
    return false;
}

std::vector<Board*> Bus::decoders(const BusCycle& c) const {
    std::vector<Board*> out;
    for (Board* b : boards_)
        if (b->enabled() && b->decodes(c)) out.push_back(b);
    return out;
}

std::vector<Board*> Bus::respondersTo(const BusCycle& in) const {
    BusCycle c = in;
    c.phantom = anyAssertsPhantom(c);
    return decoders(c);
}

static const char* cycleName(Cycle t) {
    switch (t) {
    case Cycle::MemRead: return "read";
    case Cycle::MemWrite: return "write";
    case Cycle::IoRead: return "IN";
    case Cycle::IoWrite: return "OUT";
    case Cycle::IntAck: return "INTA";
    }
    return "?";
}

void Bus::reportContention(const BusCycle& c, const std::vector<Board*>& who) {
    if (policy_ == Contention::Silent) return;

    char buf[160];
    bool io = (c.type == Cycle::IoRead || c.type == Cycle::IoWrite);
    if (io)
        std::snprintf(buf, sizeof buf, "CONTENTION: port 0x%02X (%s) driven by", c.port(),
                      cycleName(c.type));
    else
        std::snprintf(buf, sizeof buf, "CONTENTION: 0x%04X (%s) driven by", c.addr,
                      cycleName(c.type));

    std::string m = buf;
    for (Board* b : who) m += " " + b->id;
    // Two boards both actually driving is a real electrical fault, and a real
    // backplane would hand you exactly this bug. We report it; we do not pick a
    // winner. Picking a winner is how a simulator lies to you.
    m += "  -- both boards drive. The bus does not arbitrate (DESIGN.md 4.6).";
    log_.push_back(m);
}

void Bus::settle(const BusCycle& c) {
    for (Board* b : boards_)
        if (b->enabled()) b->snoop(c);
}

// ---------------------------------------------------------------------------
// The cycle. This is the whole bus.
//
//   pass 1  resolve PHANTOM* -- ask every board whether it pulls the pin
//   pass 2  decode          -- ask every board whether it drives; move the byte
//   pass 3  settle          -- show the finished cycle to every board, so any
//                             card watching the address bus can latch what it saw
//
// Pass 3 exists because a real card can watch a cycle it does not answer, and
// the Tarbell does exactly that. It is not a callback and it is not the bus
// telling anyone anything: the cycle was on the backplane, in front of every
// card, the entire time.
// ---------------------------------------------------------------------------

uint8_t Bus::memRead(uint16_t addr) {
    BusCycle c{Cycle::MemRead, addr, 0, false};
    c.phantom = anyAssertsPhantom(c);

    auto who = decoders(c);
    if (who.size() > 1) reportContention(c, who);

    unclaimed_ = who.empty();
    uint8_t v = who.empty() ? 0xFF  // floating bus (DESIGN.md 4.6.1)
                            : who.front()->read(c);
    settle(c);
    return v;
}

void Bus::memWrite(uint16_t addr, uint8_t data) {
    BusCycle c{Cycle::MemWrite, addr, data, false};
    c.phantom = anyAssertsPhantom(c);

    auto who = decoders(c);
    if (who.size() > 1) reportContention(c, who);

    unclaimed_ = who.empty();
    // Nobody latched it. The byte is simply gone -- the write half of the
    // floating bus. This is what a guest write to ROM does, and the bus needed
    // no rule about ROM to make it happen.
    for (Board* b : who) b->write(c);
    settle(c);
}

uint8_t Bus::ioRead(uint8_t port) {
    BusCycle c{Cycle::IoRead, port, 0, false};
    auto who = decoders(c);
    if (who.size() > 1) reportContention(c, who);
    unclaimed_ = who.empty();
    uint8_t v = who.empty() ? 0xFF : who.front()->read(c);
    settle(c);
    return v;
}

void Bus::ioWrite(uint8_t port, uint8_t data) {
    BusCycle c{Cycle::IoWrite, port, data, false};
    auto who = decoders(c);
    if (who.size() > 1) reportContention(c, who);
    unclaimed_ = who.empty();
    for (Board* b : who) b->write(c);
    settle(c);
}

uint8_t Bus::intAck() {
    BusCycle c{Cycle::IntAck, 0, 0, false};
    auto who = decoders(c);
    unclaimed_ = who.empty();
    // No vector-interrupt board answered, so nothing drives the bus and it
    // floats high: 0xFF, which the 8080 executes as RST 7. That is the real
    // Altair's behavior, and we get it for free from the same rule that makes
    // unmapped memory read 0xFF. The bus does not know what a vector IS.
    uint8_t v = who.empty() ? 0xFF : who.front()->read(c);
    settle(c);
    return v;
}

} // namespace altair
