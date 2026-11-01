#pragma once
//
// The baud switch of the Vector Graphic 8251 boards. The ZCB (reference/ZCB CPU Manual.md 6.2)
// and the Bit Streamer (reference/Bitstreamer I.md) have the same chain: the 2 MHz clock over
// 13, then halved by each rocker. 110 is a divide of 88, from 1200 over 11 on the ZCB's
// drawing and from 2400 over 22 on the Bit Streamer's. The labels are for a clock factor of 16.

#include "chips/intel8251.h"
#include "core/board.h"
#include "core/value.h"

#include <string>

namespace altair {

inline void vgSetBaud(Intel8251& u, long long label) {
    struct Rocker {
        long long label;
        long long divide;
    };
    constexpr Rocker kRockers[] = {{9600, 1}, {4800, 2}, {2400, 4}, {1200, 8},
                                   {600, 16}, {300, 32}, {150, 64}, {110, 88}};
    constexpr long long kBaudClock = 2000000 / 13;

    for (const Rocker& r : kRockers)
        if (r.label == label) {
            u.baud  = label;
            u.txcHz = kBaudClock / r.divide;
        }
}

inline Property vgBaudProperty(Intel8251& u, const char* help) {
    Property x;
    x.name    = "baud";
    x.help    = help;
    x.kind    = Kind::Enum;
    x.choices = {"110", "150", "300", "600", "1200", "2400", "4800", "9600"};
    x.get     = [&u] { return Value::ofStr(std::to_string(u.baud)); };
    x.set     = [&u](const Value& v, std::string&) {
        vgSetBaud(u, std::stoll(v.s()));
        return true;
    };
    return x;
}

} // namespace altair
