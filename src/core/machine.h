#pragma once
//
// Machine -- the backplane, plus the cards in it.
//
// The monitor and the MCP server both sit on THIS and on Board::properties(),
// which is why they cannot drift (DESIGN.md 11). There is no "CLI model" and no
// "MCP model"; there is a machine, and two ways to talk to it.

#include "core/board.h"
#include "core/bus.h"

#include <memory>
#include <string>
#include <vector>

namespace altair {

class Machine {
public:
    std::string name = "altair";
    long long clockHz = 2000000;

    // Port 0xFF, the front-panel sense switches. NOT decorative: the DBL boot
    // PROM does `IN 0FFH` at FF22 and uses bit 4 to pick the 2SIO's stop bits.
    uint8_t sense = 0;

    // Monitor commands run once the backplane exists. This is why there is no
    // BOOT command (DESIGN.md 10.0): a config that should boot says
    // startup = ["GO FF00"], which is the operator's keystroke, written down.
    std::vector<std::string> startup;

    Bus bus;

    Board* find(const std::string& id);
    Board* add(const std::string& type, const std::string& id, std::string& err);
    bool remove(const std::string& id, std::string& err);
    const std::vector<std::unique_ptr<Board>>& boards() const { return boards_; }

    void reset(Reset r);

    // Power applied. THE ONLY THING THAT LOSES RAM (DESIGN.md 6).
    void power();

    // There is no CPU yet (milestone 1a). `running` is false and stays false, so
    // every property is settable -- and when a CPU lands, config-time properties
    // start being rejected on a running machine without one line changing here.
    bool running = false;

    // Collected board chatter (bad bank selects, ROM load failures), drained by
    // whichever front end is listening.
    std::vector<std::string> drainBoardLog();

private:
    std::vector<std::unique_ptr<Board>> boards_;
};

} // namespace altair
