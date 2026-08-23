#pragma once
//
// PropIo -- the S100Computers Console IO Board (a Parallax-Propeller console).
// See reference/Console IO Board.md and docs/devguide/serial-io.md.
//
// This is NOT a new serial engine. It is a thin SUBTYPE of the SSM IO-2 serial board
// (Io2Board, src/boards/io2.h). The Console IO Board is a fully jumper-configurable
// "read a status bit, read/write a data byte" polled console with NO UART register map --
// the Propeller *is* the terminal -- which is exactly what Io2Board already models. propio
// only presets io2's straps to the board's documented worked-example convention (the SD
// Systems 8024 monitor it ships with); every strap (status_port/data_port/bit/inverter_gate,
// and `connect`) is still overridable, because the real board is jumperable. The engine, the
// endpoint plumbing, SNAPSHOT/RESTORE, and reflection all come from Io2Board unchanged.
//
#include "boards/io2.h"

#include <string>

namespace altair {

// The Console IO Board's documented default straps (reference/Console IO Board.md): status at
// 00H / data at 01H; keyboard(RX)-ready = status bit 1, output(TX)-ready = status bit 2, both
// active high (a 0 in the TX bit means "busy"). Exposed so the test asserts the same values.
Io2Profile propioProfile();

class PropIoBoard : public Io2Board {
public:
    PropIoBoard();

    std::string type() const override { return "propio"; }
};

} // namespace altair
