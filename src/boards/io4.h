#pragma once
//
// SSM IO-4 (2P + 2S) -- the generic strap-configurable serial board. See
// reference/SSM IO-4 2P+2S IO Board.md and docs/devguide/serial-io.md.
//
// The real SSM IO-4 puts TWO independent full-duplex serial channels (Serial A = U9,
// Serial B = U8; an AY-5-1013 / TMS6011 / TR-1602 UART each) plus two parallel-in and two
// parallel-out ports on one S-100 card. We model the SERIAL HALF ONLY: two channels, each
// a chip-less "read a status bit, read/write a data byte" polled port with a bank of
// straps that let it imitate most polled status+data serial cards on the bus. The parallel
// ports are out of scope.
//
// This is NOT a new engine. It is StrapSerialBoard (src/boards/strapserial.h) with TWO
// channels: "a" strapped as MITS SIO Rev 0 at ports 0/1 (the default the SSM 8080 monitor
// expects), and "b" the same shape relocated to ports 2/3 -- exactly the IO-4's default
// 4-port block (Serial A at 0/1, Serial B at 2/3). Every strap on either channel
// (profile / status_port / data_port / dav / tbmt / inverter_gate / baud / connect) is
// still overridable, because the real board is jumpered. Each channel is configured under
// its own unit in the machine file: `[board.unit.a]` and `[board.unit.b]`.
//
#include "boards/strapserial.h"

#include <string>

namespace altair {

class Io4Board : public StrapSerialBoard {
public:
    Io4Board();

    std::string type() const override { return "io4"; }
};

} // namespace altair
