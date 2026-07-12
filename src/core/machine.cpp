#include "core/machine.h"

#include "boards/memory.h"
#include "boards/registry.h"

#include <algorithm>

namespace altair {

Board* Machine::find(const std::string& id) {
    for (auto& b : boards_)
        if (b->id == id) return b.get();
    return nullptr;
}

Board* Machine::add(const std::string& type, const std::string& id, std::string& err) {
    if (find(id)) {
        err = "a board with id '" + id + "' already exists";
        return nullptr;
    }
    auto b = makeBoard(type);
    if (!b) {
        err = "no board type '" + type + "'. BOARD TYPES lists them.";
        return nullptr;
    }
    b->id = id;
    Board* raw = b.get();
    // Into the backplane, and onto the clock. A card that has nothing
    // time-dependent on it never looks at the clock; a UART cannot work without
    // it (DESIGN.md 7.5).
    raw->attachClock(&clock);
    boards_.push_back(std::move(b));
    bus.attach(raw);
    return raw;
}

void Machine::pump() {
    for (auto& b : boards_) b->pump();
}

bool Machine::remove(const std::string& id, std::string& err) {
    Board* b = find(id);
    if (!b) {
        err = "no board '" + id + "'";
        return false;
    }
    bus.detach(b);
    boards_.erase(std::remove_if(boards_.begin(), boards_.end(),
                                 [&](const std::unique_ptr<Board>& p) { return p.get() == b; }),
                  boards_.end());
    return true;
}

void Machine::reset(Reset r) {
    // The bus carries the signal to every card, and each card decides what it
    // means -- POC* is board-dependent, and a board just needs to know it
    // happened. Guest code cannot see it at all.
    for (auto& b : boards_) b->reset(r);
}

void Machine::power() {
    // Time starts now. THE ONLY thing that resets the clock is power -- a front
    // panel RESET does not un-elapse the hours the machine has been on, and a
    // board timing a disk's rotation would be very surprised if it did.
    clock.power();
    for (auto& b : boards_) b->power();
    for (auto& b : boards_) b->reset(Reset::PowerOn);
}

// ---------------------------------------------------------------------------
// Who is driving the bus, and which chip is doing it.
//
// Note what these DON'T do: they do not look for a board called "cpu0", and they
// do not look for a board whose type() is "8080". They ask what the board IS --
// can it master the bus, does it carry a core -- so a Z80 card, or a card with an
// 8080 and an 8085 on it, or a DMA controller that takes the bus away, all work
// here without this file learning their names.
// ---------------------------------------------------------------------------
std::vector<Board*> Machine::masters() {
    std::vector<Board*> out;
    for (auto& b : boards_)
        if (dynamic_cast<BusMaster*>(b.get())) out.push_back(b.get());
    return out;
}

BusMaster* Machine::master() {
    for (auto& b : boards_)
        if (auto* m = dynamic_cast<BusMaster*>(b.get())) return m;
    return nullptr;  // no processor. A REAL machine, and the one 1a ran.
}

CpuCore* Machine::cpu() {
    for (auto& b : boards_)
        if (auto* c = dynamic_cast<CpuCard*>(b.get())) return c->activeCore();
    return nullptr;
}

std::string Machine::isa() {
    CpuCore* c = cpu();
    return c ? c->isa() : "";
}

std::vector<std::string> Machine::drainBoardLog() {
    std::vector<std::string> out;
    for (auto& b : boards_) {
        if (auto* m = dynamic_cast<MemoryBoard*>(b.get())) {
            for (auto& s : m->takeLog()) out.push_back(s);
        }
    }
    return out;
}

} // namespace altair
