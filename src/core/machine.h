#pragma once
//
// Machine -- the backplane, plus the cards in it.
//
// The monitor and the MCP server both sit on THIS and on Board::properties(),
// which is why they cannot drift (DESIGN.md 11). There is no "CLI model" and no
// "MCP model"; there is a machine, and two ways to talk to it.

#include "core/board.h"
#include "core/bus.h"
#include "core/debug.h"
#include "cpu/cpu.h"

#include <memory>
#include <string>
#include <vector>

namespace altair {

class Machine {
public:
    std::string name = "altair";

    // THERE IS NO MACHINE CLOCK, and there must never be one again. The crystal is
    // on the CPU card, so `clock_hz` is that BOARD's property (DESIGN.md 3, 8).
    // A machine-level copy would be a second place to say one thing, and the day
    // the two disagreed the machine would run at whichever was written last.

    // Port 0xFF, the front-panel sense switches. NOT decorative: the DBL boot
    // PROM does `IN 0FFH` at FF22 and uses bit 4 to pick the 2SIO's stop bits.
    uint8_t sense = 0;

    // Monitor commands run once the backplane exists. This is why there is no
    // BOOT command (DESIGN.md 10.0): a config that should boot says
    // startup = ["GO FF00"], which is the operator's keystroke, written down.
    std::vector<std::string> startup;

    Bus bus;

    // Emulated time (DESIGN.md 7.5). Advanced ONLY by the run loop, by exactly
    // the T-states the CPU reported retiring -- so a board's sense of time and
    // the guest's are the same sense of time, and a session replays identically.
    Clock clock;

    // Give every board's host endpoints a turn: drain a keyboard, accept a
    // socket. Once per time slice, never inside a bus cycle (DESIGN.md 7.7).
    void pump();

    Board* find(const std::string& id);
    Board* add(const std::string& type, const std::string& id, std::string& err);
    bool remove(const std::string& id, std::string& err);
    const std::vector<std::unique_ptr<Board>>& boards() const { return boards_; }

    void reset(Reset r);

    // Power applied. THE ONLY THING THAT LOSES RAM (DESIGN.md 6).
    void power();

    // ---- Who is driving? (DESIGN.md 3) ----
    //
    // Both of these ASK THE BACKPLANE and are allowed to answer "nobody". A
    // machine with no CPU card in it is a real machine you can build -- it is the
    // one milestone 1a ran, with the monitor as bus master -- so every caller has
    // to cope with a null, and the compiler makes sure they do.
    //
    // They are also recomputed on every call rather than cached. That is what lets
    // BOARD REMOVE cpu0 work while the monitor is sitting there, and what will let
    // a dual-processor card switch cores under the debugger's feet without the
    // debugger noticing anything unusual.
    BusMaster* master();
    CpuCore* cpu();

    // The instruction set the machine currently speaks -- the active core's own
    // answer. Empty when there is no CPU, which is why DISASM in a CPU-less
    // machine asks you to say CPU=8080 rather than guessing (DESIGN.md 3.0.2).
    std::string isa();

    // Two boards both claiming to drive the bus is contention, and we say so
    // rather than picking one. Same rule as two boards decoding one address.
    std::vector<Board*> masters();

    // True only while the debugger's run loop is turning. Config-time properties
    // are rejected while it is (DESIGN.md 10.1) -- which is why they were all
    // settable in milestone 1a and start being refused the moment a CPU runs,
    // without one line here changing.
    bool running = false;

    Debugger debug{*this};

    // Collected board chatter (bad bank selects, ROM load failures), drained by
    // whichever front end is listening.
    std::vector<std::string> drainBoardLog();

private:
    std::vector<std::unique_ptr<Board>> boards_;
};

} // namespace altair
