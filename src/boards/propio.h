#pragma once
//
// PropIo -- the S100Computers Console IO Board (a Parallax-Propeller console).
// See reference/Console IO Board.md and docs/devguide/serial-io.md.
//
// This is NOT a new serial engine. It is a thin SUBTYPE of the strap-configurable serial
// engine (StrapSerialBoard, src/boards/strapserial.h) with a SINGLE channel. The Console
// IO Board is a fully jumper-configurable "read a status bit, read/write a data byte"
// polled console with NO UART register map -- the Propeller *is* the terminal -- which is
// exactly what StrapSerialBoard already models. propio only presets its one channel's
// straps to the board's documented worked-example convention (the SD Systems 8024 monitor
// it ships with); every strap (status_port/data_port/bit/inverter_gate, and `connect`) is
// still overridable, because the real board is jumperable. Being single-channel, its straps
// surface as BOARD-LEVEL properties and its one unit is named "serial" -- the historical
// config (`[[board]] type="propio"` / `connect="console"`) is unchanged. The engine, the
// endpoint plumbing, SNAPSHOT/RESTORE, and reflection all come from StrapSerialBoard.
//
#include "boards/strapserial.h"

#include <string>

namespace altair {

// The Console IO Board's documented default straps (reference/Console IO Board.md): status at
// 00H / data at 01H; keyboard(RX)-ready = status bit 1, output(TX)-ready = status bit 2, both
// active high (a 0 in the TX bit means "busy"). Exposed so the test asserts the same values.
SerialStraps propioProfile();

class PropIoBoard : public StrapSerialBoard {
public:
    PropIoBoard();

    std::string type() const override { return "propio"; }
};

} // namespace altair
