#pragma once
//
// GSIO -- a Generic Serial I/O board: two chip-less, strap-configurable serial channels.
// See docs/devguide/serial-io.md.
//
// This board makes NO claim to be a particular piece of period hardware. It is a generic
// dual-channel serial card: each channel is a "read a status bit, read/write a data byte"
// polled port with a bank of straps that let it imitate most polled status+data serial
// cards on the S-100 bus. That is deliberately ALL it does -- basic transmit/receive. A
// specific documented board with a fuller feature set (programmable word length, parity,
// stop bits, framing/overrun error status, current-loop straps) is a separate, fully
// emulated board, not a strap on this one. (The real SSM IO-4 -- issue #403 -- is the
// first such board on the list; its datasheet lives at reference/SSM IO-4 2P+2S IO Board.md.)
//
// This is NOT a new engine. It is StrapSerialBoard (src/boards/strapserial.h) with TWO
// channels: "a" strapped as MITS SIO Rev 0 at ports 0/1 (the default the SSM 8080 monitor
// expects), and "b" the same shape relocated to ports 2/3. Every strap on either channel
// (profile / status_port / data_port / dav / tbmt / inverter_gate / baud / connect) is
// overridable. Each channel is configured under its own unit in the machine file:
// `[board.unit.a]` and `[board.unit.b]`.
//
#include "boards/strapserial.h"

#include <string>

namespace altair {

class GsioBoard : public StrapSerialBoard {
public:
    GsioBoard();

    std::string type() const override { return "gsio"; }
};

} // namespace altair
