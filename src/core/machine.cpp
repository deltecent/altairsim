#include "core/machine.h"

#include "boards/s100-memory.h"
#include "boards/registry.h"

#include <algorithm>

namespace altair {

// A BOARD ID IS CASE-INSENSITIVE, AND THAT IS AN IDENTITY, NOT A CONVENIENCE.
//
// `ACR0` and `acr0` are ONE CARD. Not "one card and a kindness to whoever typed it
// wrong" -- one card, on every road that reaches a board: the prompt, a machine
// file, the MCP tools. All of them come through here, which is why this is the only
// place it has to be said.
//
// It follows that Machine::add()'s duplicate check refuses `ACR0` when `acr0` is
// already in the backplane. That is the point. Two cards you cannot tell apart at
// the prompt are not two cards; they are a machine file with a bug in it.
//
// The SHORTHANDS -- dropping the trailing index, dropping a lone unit's name -- are
// a different thing entirely, and they live in the monitor. A machine file must say
// what it means (cli/monitor.cpp, Monitor::board()).
Board* Machine::find(const std::string& id) {
    std::string want = lowerAscii(id);
    for (auto& b : boards_)
        if (lowerAscii(b->id) == want) return b.get();
    return nullptr;
}

Board* Machine::add(const std::string& type, const std::string& id, std::string& err) {
    if (find(id)) {
        err = "a board with id '" + id + "' already exists";
        return nullptr;
    }
    auto b = makeBoard(type);
    if (!b) {
        err = "no board type '" + type + "'. BOARDS TYPES lists them.";
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

uint64_t Machine::rxBytes() const {
    uint64_t n = 0;
    for (const auto& b : boards_) n += b->rxBytes();
    return n;
}

bool Machine::burn(uint16_t addr, uint8_t v, std::string& why) {
    // ASK WHO ANSWERS A READ, NOT A WRITE, and that is the whole trick. A ROM region
    // does not decode a write -- that is what makes it a ROM -- so asking the bus who
    // would take a write here gets the answer "nobody", on precisely the chip we are
    // trying to program. But respondersTo() runs the REAL decode, PHANTOM* and all
    // (bus.h), so whoever would hand the CPU a byte at this address is exactly whose
    // chip this is. A shadowed board does not answer, which is right: you cannot burn
    // a chip the machine currently cannot see, any more than the CPU could read it.
    BusCycle probe;
    probe.type = Cycle::MemRead;
    probe.addr = addr;

    std::vector<MemoryBoard*> mem;
    for (Board* b : bus.respondersTo(probe))
        if (auto* mb = dynamic_cast<MemoryBoard*>(b)) mem.push_back(mb);

    if (mem.empty()) {
        why = "no board answers here -- there is no chip to program";
        return false;
    }
    if (mem.size() > 1) {
        // Contention is a bus fact and not ours to resolve (§4.6). Through the bus both
        // boards would take the write; behind it we would have to pick one, and picking
        // silently is how you spend an afternoon wondering which chip you burned.
        why = "more than one board answers here (";
        for (size_t i = 0; i < mem.size(); ++i) why += (i ? ", " : "") + mem[i]->id;
        why += ") -- fix the contention, or phantom one of them out";
        return false;
    }
    if (!mem[0]->poke(addr, v)) {
        why = mem[0]->id + " answers here but has no store at that address";
        return false;
    }
    return true;
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

// THE CARD, not the core -- so the run loop can hand back the achieved crystal
// (CpuCard::reportAchievedHz) without knowing which board holds the processor or
// what chip it is. Same walk as cpu(), same "nobody" answer on an empty backplane.
CpuCard* Machine::cpuCard() {
    for (auto& b : boards_)
        if (auto* c = dynamic_cast<CpuCard*>(b.get())) return c;
    return nullptr;
}

std::string Machine::isa() {
    CpuCore* c = cpu();
    return c ? c->isa() : "";
}

// EVERY board gets to speak, not just the memory card. This was a
// dynamic_cast<MemoryBoard*> and it was a wall: the disk controllers this exists
// for -- a bad checksum, a write to a protected disk -- could not have got a word
// through it. Board::drainLog() is virtual and the default is silence.
std::vector<std::string> Machine::drainBoardLog() {
    std::vector<std::string> out;
    for (auto& b : boards_)
        for (auto& s : b->drainLog()) out.push_back(s);
    return out;
}

} // namespace altair
